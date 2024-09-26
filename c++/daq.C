#include <iostream>
#include <iomanip>
#include <fstream>
#include <signal.h>

#include "daq.h"
#include "utils.h"
#include "error.h"
#include "pstream.h"
#include "processor_diskdump.h"
#include "processor_python.h"
#include "channel_data_extractor.h"
#include "shot_data_layout.h"
#include <memory>
#include <map>

//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <string.h>
//#include <sys/types.h>
//#include <sys/socket.h>
//#include <netinet/in.h>


namespace apdcam10g
{
    using namespace std;

    // A utility class to transform POSIX signals such as SIGSEGV to c++ exceptions, which are then handled
    // in a common way
    class signal2exception
    {
    private:
        static std::map<std::jthread::id,std::string> thread_names_;

        // The signal-handler routine which can be set for POSIX signals. It simply throws an apdcam_error exception
        // with the appropriate message
        static void run(int signum)
            {
                APDCAM_ERROR("Signal " + std::to_string(signum) + " is caught in thread \"" + thread_names_[std::this_thread::get_id()] + "\"");
                //signal (signum, SIG_DFL);
                //raise (signum);
            }


    public:

        // The below vararg 'set' functions can be used to set the signal handler for the given signals.
        // The first argument is a name that will be associated with the calling thread for the user's
        // convenience, subsequent integer args (any number of them) are signal numbers
        // Usage: signal2exception::set("my_thread_name",SIGSEGV,SIGINT);

        static void set(const std::string &thread_name, int signum)
            {
                thread_names_[std::this_thread::get_id()] = thread_name;
                signal(signum,signal2exception::run);
            }

        template <typename... SIGNUMS>
        static void set(const std::string &thread_name, int signum1, SIGNUMS... signums)
            {
                signal(signum1,signal2exception::run);
                set(thread_name,signums...);
            }
    };

    std::map<std::jthread::id,std::string> signal2exception::thread_names_;

    daq::daq()
    {
        
    }

    bool daq::python_analysis_stop()
    {
        return python_analysis_stop_.test(std::memory_order_acquire);
    }
    void daq::python_analysis_stop(bool b)
    {
        if(b) python_analysis_stop_.test_and_set(std::memory_order_release);
        else python_analysis_stop_.clear();
    }

    void daq::python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter)
    {
        python_analysis_run_.wait(false,std::memory_order_acquire);
        *from_counter = python_analysis_data_available_from_;
        *to_counter   = python_analysis_data_available_to_;
    }

    size_t daq::python_analysis_wait_finish()
    {
        python_analysis_run_.wait(true,std::memory_order_acquire);
        return python_analysis_needs_data_from_;
    }

    void daq::python_analysis_start(size_t from_counter, size_t to_counter)
    {
        python_analysis_data_available_from_ = from_counter;
        python_analysis_data_available_to_   = to_counter;
        python_analysis_run_.test_and_set(std::memory_order_release);
        python_analysis_run_.notify_one();
    }

    void daq::python_analysis_done(size_t need_data_from)
    {
        python_analysis_needs_data_from_ = need_data_from;
        python_analysis_run_.clear(std::memory_order_release);
        python_analysis_run_.notify_one();
    }

    daq &daq::instance()
    {
        static daq the_daq;
        return the_daq;
    }

    void daq::finish()
    {
        for(auto p : processors_) p->finish();
    }

    daq &daq::process_period(unsigned int p)
    { 
        if( (p-1)&p != 0)
        {
            show_error("Process period must be a power of 2","daq::process_period");
            return *this;
        }
        process_period_ = p; 
        return *this; 
    }

    template <safeness S>
    daq &daq::init()
    {
        // Calculate the all_enabled_channels_info / board_enabled_channels_info vectors (ranges), and the
        // number of all enabled channels
        calculate_channel_info();

        if(mtu_ == 0) APDCAM_ERROR("MTU has not been set");

        const unsigned int nof_adc = channel_masks_.size();

        if(mtu_==0) APDCAM_ERROR("MTU has not yet been specified in daq::initialize");
        if(dual_sata_ && nof_adc>2) APDCAM_ERROR("Dual sata is set with more than two ADC boards present");

        // Resize the socket vector to have as many elements as there are ADC boards
        sockets_.clear(); // delete any previously open socket, if any
        sockets_.resize(nof_adc);

        // Resize the network buffer vector to have as many elements as there are ADC boards. Initialize their buffer size
        cerr<<"[DAQ] Network buffers : "<<network_buffer_size_<<" packets of size "<<max_udp_packet_size_<<endl<<endl;
        regenerate(network_buffers_,nof_adc,network_buffer_size_,max_udp_packet_size_);

        // Resize the extractors vector to have as many elements as there are ADC boards
        regenerate_by_func(extractors_, nof_adc, [this](unsigned int i_adc){return new channel_data_extractor<default_safeness>(this,fw_version_,i_adc);});


        // Resize the channels buffers, so that the sub-ranges (per ADC board) can be set up on the fly (i.e.
        // the 'all_enabled_channels_buffers_' vector is not resized anymore, which would invalidate its sub-ranges)
        regenerate(all_enabled_channels_buffers_,all_enabled_channels_info_.size(),0);

        // The per-board channels vector (in fact: subrange of the 'all_enabled_channels_buffers_')
        board_enabled_channels_buffers_.clear();
        board_enabled_channels_buffers_.resize(nof_adc);

        board_last_channel_buffers_.resize(nof_adc);

        // Resize all_channels_buffers_ to the maximum possible number of channels, and set all elements to
        // a zero pointer
        all_channels_buffers_.clear();
        all_channels_buffers_.resize(nof_adc*config::channels_per_board,0);

        // Open the input ports
        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            const int port_index = (dual_sata_ ? i_adc*2 : i_adc);
            cerr<<"[DAQ] ====== ADC Board #"<<i_adc<<" ======"<<endl;
	    cerr<<"[DAQ] Port            : "<<config::ports[i_adc*2]<<endl;
	    sockets_[i_adc].open(config::ports[port_index]);
            cerr<<"[DAQ] Bytes per shot  : "<<board_bytes_per_shot_[i_adc]<<endl;
            cerr<<"[DAQ] Enabled channels: ";
            for(auto c : board_enabled_channels_info_[i_adc]) cerr<<c->channel_number<<" ";
            cerr<<endl;
            if(debug_)
            {
                shot_data_layout layout(board_bytes_per_shot_[i_adc], resolution_bits_[i_adc], board_enabled_channels_info_[i_adc]);
                layout.prompt("[DAQ]");
                cerr<<"[DAQ] ---- SHOT DATA LAYOUT ----"<<endl;
                layout.show();
                cerr<<"[DAQ] --------------------------"<<endl;
            }
            cerr<<endl;
        }
        
        for(unsigned int i=0; i<all_enabled_channels_info_.size(); ++i)
        {
            const channel_info *ci = all_enabled_channels_info_[i];
            channel_data_buffer_t *b = new channel_data_buffer_t(sample_buffer_size_,sample_buffer_extra_size_);
            b->copy_values(*ci);
            all_enabled_channels_buffers_[i] = b;
            board_enabled_channels_buffers_[ci->board_number].push_back(b);
            board_last_channel_buffers_[ci->board_number] = b;
            all_channels_buffers_[ci->absolute_channel_number] = b;
        }

        for(auto p : processors_) p->init();

        // Call the debug(...) function with the saved value (seems useless...) in order to set the corresponding flags
        // of the worker objects created since the last call
        debug(debug_);

        return *this;
    }


    template <safeness S>
    daq &daq::start(bool wait)
    {
        network_threads_.clear();
        extractor_threads_.clear();
        
	// start creating the threads from tne end-consumer end, i.e. backwards, so that consumers are ready (should we further sync?)
	// and listening by the time data starts to arrive.
        
        // ----------------------------- processor thread -------------------------------------------
        {
            output_lock lck;
            cerr<<"[DAQ] Starting processor thread"<<endl;
        }
        processor_thread_ = std::jthread( [this](std::stop_token stok)
            {
                const std::string prompt = "[DAQ/PROC ]";
                {
                    output_lock lck;
                    cerr<<prompt<<"Thread started"<<endl;
                }
                signal2exception::set("processor",SIGSEGV);
                try
                {
                    for(unsigned int to_counter=process_period_; !stok.stop_requested(); )
                    {
                        size_t common_pop_counter=0;
                        size_t common_push_counter=0;
                        
                        // check the last channels of each board, and spin-lock wait until they have a required new number
                        // of entries, or are terminated
                        bool non_terminated_exists = false;
                        for(int i=0; i<board_last_channel_buffers_.size(); ++i)
                        {
                            const channel_data_buffer_t *b = board_last_channel_buffers_[i];
                            bool terminated;
                            size_t push_counter;
                            while( (push_counter=b->push_counter())<to_counter && (terminated=b->terminated())==false );
                        
                            // re-query to capture the case when between the two and-ed conditions in the while loop there were new
                            // entries added to the ring_buffer, and it was terminated as well.
                            if(terminated)
                            {
                                push_counter = b->push_counter();
                            }
                            else non_terminated_exists = true;
                            if(i==0 || push_counter<common_push_counter) common_push_counter = push_counter;
                        
                            size_t pop_counter = b->pop_counter();
                            if(pop_counter > common_pop_counter) common_pop_counter = pop_counter;
                        }
                    
                        if(common_push_counter > common_pop_counter)
                        {
                            // the counter to indicate the first element that is required to stay in the buffers
                            // by any of the processors
                            unsigned int needed = common_push_counter;
                            for(auto p : processors_) 
                            {
                                const unsigned int this_processor_needs = p->run(common_pop_counter, common_push_counter);
                                if(this_processor_needs > common_push_counter) APDCAM_ERROR("processor returned too high needed value");
                                if(this_processor_needs < needed) needed = this_processor_needs;
                            }
                        
                            // after all tasks have run, and reported what the earliest element in the buffers that they
                            // need for further processing, clear the buffers up to this
                            for(auto a: all_enabled_channels_buffers_) a->pop_to(needed);
                        }
                    
                        if(!non_terminated_exists) break;
                    
                        to_counter = common_push_counter + process_period_;
                    }
                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread finished"<<endl;
                    }
                }
                catch(apdcam10g::error &e) 
                { 
                    for(auto a: all_enabled_channels_buffers_) a->terminate();
                    cerr<<prompt<<e.full_message()<<endl; 
                }
        });


        // ------------------- channel data extractor threads ------------------------------------------
        {
            output_lock lck;
            cerr<<"[DAQ] Starting extractor threads"<<endl;
        }
        
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            extractor_threads_.push_back(std::jthread( [this, i_socket](std::stop_token stok)
                {
                    const std::string prompt = "[DAQ/EXT/" + std::to_string(i_socket) + "] ";
                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread "<<i_socket<<" started"<<endl;
                    }
                    signal2exception::set("extractor" + std::to_string(i_socket),SIGSEGV);
                    try
                    {
                        extractors_[i_socket]->run(*network_buffers_[i_socket],board_enabled_channels_buffers_[i_socket]);

                        // Write a summary of what happened
                        {
                            output_lock lck;
                            cerr<<prompt<<"Thread "<<i_socket<<" finished"<<endl;
                            cerr<<prompt<<"---------- Data extraction summary  ------------------"<<endl;
                            double sum=0, n=0, max=0;
                            for(auto b : board_enabled_channels_buffers_[i_socket])
                            {
                                ++n;
                                sum += b->mean_size();
                                const auto m = b->max_size();
                                if(m>max) max=m;
                            }
                            cerr<<prompt<<"average buffer size : "<<sum/n<<endl;
                            cerr<<prompt<<"max buffer size     : "<<max<<endl;
                            cerr<<prompt<<"buffers' capacity   : "<<sample_buffer_size_<<endl;
                            if(sum/n > sample_buffer_size_/2) cerr<<prompt<<"we recommend increasing the buffer size"<<endl;
                            cerr<<endl;
                        }
                    }
                    catch(apdcam10g::error &e) 
                    { 
                        for(auto a : board_enabled_channels_buffers_[i_socket]) a->terminate();
                        cerr<<prompt<<e.full_message()<<endl; 
                    }
                }));
        }



        // -------------------------------- network reading thread(s) ----------------------------------
        // these threads do nothing but continuously read udp packets into the udp packet buffers 'network_buffers_'
        {
            output_lock lck;
            cerr<<"[DAQ] Starting network threads"<<endl;
        }
        {
            for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
            {
                // make the corresponding socket blocking. this thread is reading only from one socket, so
                // we can safely be blocked until data is available 
                sockets_[i_socket].blocking(true);

                network_threads_.push_back(std::jthread( [this, i_socket](std::stop_token stok)
                    {
                        const std::string prompt = "[DAQ/NET/" + std::to_string(i_socket) + "] ";
                        {
                            output_lock lck;
                            cerr<<prompt<<"Thread "<<i_socket<<" started"<<endl;
                        }
                        signal2exception::set("net" + std::to_string(i_socket),SIGSEGV);
                        try
                        {
                            while(!stok.stop_requested())
                            {
                                // The 'receive' function of the UDP packet buffer automatically takes care of lost packets
                                // and inserts them with zero fill. We provide the stop_token 'stok' to this function
                                // so that it can monitor eventual stop requests within the spin-lock waiting for new packets
                                const auto received_packet_size = network_buffers_[i_socket]->receive(sockets_[i_socket],stok);

                                // Reached the end of the stream. Both a partial packet, and the 'terminated' flag indicate
                                // that the camera stopped sending more data
                                if(received_packet_size != max_udp_packet_size_ || network_buffers_[i_socket]->terminated()) break;
                            }

                            // Close the socket, no more data is accepted
                            {
                                output_lock lck;
                                cerr<<prompt<<"Closing socket on port "<<sockets_[i_socket].port()<<endl;
                            }
                            sockets_[i_socket].close();

                            // If we have quit the while loop due to stop_requested, we need to set the terminated flag
                            // to indicate no more data coming down from the network. If not, we set it again at no harm.
                            network_buffers_[i_socket]->terminate();

                            // Write a summary
                            {
                                output_lock lck;
                                cerr<<prompt<<"Thread "<<i_socket<<" finished"<<endl;
                                cerr<<prompt<<"---------- Network summary ---------------------------"<<endl;
                                cerr<<prompt<<"Received packets    : "<<network_buffers_[i_socket]->received_packets()<<endl;
                                cerr<<prompt<<"Lost packets        : "<<network_buffers_[i_socket]->lost_packets()<<endl;
                                cerr<<prompt<<"Average buffer size : "<<network_buffers_[i_socket]->mean_size()<<endl;
                                cerr<<prompt<<"Maximum buffer size : "<<network_buffers_[i_socket]->max_size()<<endl;
                                cerr<<prompt<<"Buffer capacity     : "<<network_buffer_size_<<endl;
                                if(network_buffers_[i_socket]->mean_size() > network_buffer_size_/2) cerr<<prompt<<"WE RECOMMEND INCREASING THE BUFFER SIZE"<<endl;
                                cerr<<endl;
                            }

                        }
                        catch(apdcam10g::error &e) 
                        { 
                            network_buffers_[i_socket]->terminate();
                            cerr<<prompt<<e.full_message()<<endl; 
                        }
                    }));
            }
        }

        {
            output_lock lck;
            cerr<<"[DAQ] All threads have been started"<<endl;
        }

        if(wait)
        {
            sleep(1);
            wait_finish();
        }

        return *this;
    }

    void daq::dump()
    {
        daq_settings::dump();
    }

    void daq::wait_finish()
    {
        if(processor_thread_.joinable()) processor_thread_.join();
        for(auto &t : extractor_threads_) if(t.joinable()) t.join();
        for(auto &t : network_threads_) if(t.joinable()) t.join();
        {
            output_lock lck;
            cerr<<"[DAQ] All threads have been joined"<<endl;
        }
    }

    template <safeness S>
    daq &daq::stop(bool wait)
    {
        // These threads do not need to be requested to stop because they do so anyway if their input
        // ring_buffers get their 'terminated' flag set. 
        //processor_thread_.request_stop();
        //for(auto &t : extractor_threads_) t.request_stop();

        // Stopping the network threads will cause them to set the network_buffers_[i].terminated flag to true
        // as a consequence of which the channel_data_extractor threads will set the terminated flag of all of
        // the channel data buffers' flags to true, which in turn will cause the processor thread to
        // stop as well
        for(auto &t : network_threads_) t.request_stop();

        if(wait) wait_finish();
        return *this;
    }

    daq &daq::clear_processors()
    {
        for(auto p : processors_) delete p;
        processors_.clear();
        return *this; 
    }

    template daq &daq::stop<safe>(bool);
    template daq &daq::stop<unsafe>(bool);
    template daq &daq::start<safe>(bool);
    template daq &daq::start<unsafe>(bool);
    template daq &daq::init<safe>  ();
    template daq &daq::init<unsafe>();
}


#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;
    //void         mtu(int m) { daq::instance().mtu(m); }
    void         start(bool wait) { daq::instance().start(wait); }
    void         stop(bool wait) { daq::instance().stop(wait); }
    void         version(apdcam10g::version v) { daq::instance().fw_version(apdcam10g::version(v)); }
    void         dual_sata(bool d) { daq::instance().dual_sata(d); }

    void get_net_parameters()
    {
        daq::instance().get_net_parameters();
    }

    void channel_masks(bool **m, int n_adc_boards)
    {
        std::vector<std::vector<bool>> chmasks(n_adc_boards);
        for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
        {
            for(unsigned int i_board_channel=0; i_board_channel<config::channels_per_board; ++i_board_channel)
            {
                chmasks[i_adc_board].push_back(m[i_adc_board][i_board_channel]);
            }
        }
        daq::instance().channel_masks(chmasks);
    }

    void resolution_bits(unsigned int *r, int n_adc_boards)
    {
        std::vector<unsigned int> res(n_adc_boards);
        for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
        {
            res[i_adc_board] = r[i_adc_board];
        }
        daq::instance().resolution_bits(res);
    }

    void init(bool is_safe)
    {
        if(is_safe) daq::instance().init<safe>();
        else        daq::instance().init<unsafe>();
    }

    void debug(bool d)
    {
        daq::instance().debug(d);
    }

    void add_processor_diskdump()
    {
        daq::instance().add_processor(new processor_diskdump);
    }

    void add_processor_python()
    {
        daq::instance().add_processor(new processor_python);
    }

    void write_settings(const char *filename)
    {
        daq::instance().write_settings(filename);
    }

    void wait_finish()
    {
        daq::instance().wait_finish();
    }

    void dump()
    {
        daq::instance().dump();
    }

    void get_buffer(unsigned int absolute_channel_number, unsigned int *buffersize, apdcam10g::data_type **buffer)
    {
        auto b = daq::instance().channel_buffer(absolute_channel_number);
        if(b)
        {
            *buffersize = b->capacity();
            *buffer = b->raw_buffer();
            return;
        }
        *buffersize = 0;
        *buffer = 0;
    }

    void python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter)
    {
        daq::instance().python_analysis_wait_for_data(from_counter, to_counter);
    }

    void python_analysis_done(size_t from_counter)
    {
        daq::instance().python_analysis_done(from_counter);
    }

    void test()
    {
        cerr<<"daq::test"<<endl;
    }

    void clear_processors()
    {
        daq::instance().clear_processors();
    }

    bool python_analysis_stop()
    {
        return daq::instance().python_analysis_stop();
    }

}





#endif
