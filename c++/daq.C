#include <iostream>
#include <iomanip>
#include <fstream>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

#include <unistd.h>

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


    class flag_locker
    {
    private:
        std::atomic_flag *flag_;
    public:
        flag_locker(std::atomic_flag &flag) : flag_(&flag) {flag.test_and_set();}
        ~flag_locker() {flag_->clear();}
    };

    class file_deleter
    {
    private:
        std::filesystem::path path_;
    public:
        file_deleter(const std::filesystem::path &p) : path_(p) {}
        ~file_deleter() { unlink(path_.c_str()); }
    };

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
                auto s = std::to_string(signum);
                if(signum==SIGKILL) s = "SIGKILL";
                if(signum==SIGTERM) s = "SIGTERM";
                if(signum==SIGSEGV) s = "SIGSEGV";
                APDCAM_ERROR("Signal " + s + " is caught in thread \"" + thread_names_[std::this_thread::get_id()] + "\"");
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
            channel_data_buffer_t *b = new channel_data_buffer_t(channel_buffer_size_,channel_buffer_extra_size_);
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

        // Make sure these communication flags are set to a correct initicial value
        python_analysis_run_.clear();
        python_analysis_stop_.clear();

        return *this;
    }

    void daq::stop_cmd_thread()
    {
        if(!command_thread_active_.test()) return;
        pthread_kill(command_thread_.native_handle(), SIGTERM);
        unlink(cmd_fifo_name_.c_str());
        command_thread_.join();
    }

    void daq::start_cmd_thread()
    {
        if(command_thread_active_.test()) return;
        command_thread_ = std::jthread( [this](std::stop_token stok)
            {
                flag_locker flk(command_thread_active_);
                const std::string prompt = "[DAQ/CMD] ";

                signal2exception::set("command",SIGSEGV,SIGTERM);
                try
                {
                    
                    // Create the fifo in configdir
                    unlink(cmd_fifo_name_.c_str());
                    if(mkfifo(cmd_fifo_name_.c_str(),0666) != 0) APDCAM_ERROR("Failed to create command fifo '" + cmd_fifo_name_.string() + "'");
                    file_deleter auto_delete_fifo(cmd_fifo_name_);
                    
                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread started, waiting for command in the FIFO "<<cmd_fifo_name_<<endl;
                    }
                    
                    string line;
                    while(!stok.stop_requested())
                    {
                        // Reopen the file repeatedly because if we send commands into the FIFO file by echo, it closes the
                        // file, and fifo.clear() (i.e. clearing all error bits on the ifstream) does not help. 
                        ifstream fifo(cmd_fifo_name_);
                        while(!stok.stop_requested() && getline(fifo,line))
                        {
                            cerr<<prompt<<line<<endl;
                            istringstream inputstr(line);
                            string cmd;
                            inputstr>>cmd;
                            if     (cmd == "diskdump_pause")
                            {
                                cerr<<prompt<<"Pausing diskdump"<<endl;
                                daq::instance().diskdump_pause();
                            }
                            else if(cmd == "diskdump_resume")
                            {
                                cerr<<prompt<<"Resuming diskdump"<<endl;
                                daq::instance().diskdump_resume();
                            }
                            else if(cmd == "diskdump_sampling")
                            {
                                unsigned int s;
                                if(!(inputstr>>s)) 
                                {
                                    cerr<<"Error, integer expected after diskdump_sampling"<<endl;
                                    continue;
                                }
                                cerr<<prompt<<"Setting diskdump sampling of "<<s<<endl;
                                daq::instance().diskdump_sampling(s);
                            }
                            else if(cmd == "stop")
                            {
                                unsigned int timeout_sec;
                                if(!(inputstr>>timeout_sec)) timeout_sec = 0;
                                cerr<<prompt<<"Stopping the DAQ";
                                if(timeout_sec>0) cerr<<" with a timeout of "<<timeout_sec<<" seconds";
                                cerr<<endl;
                                daq::instance().stop(timeout_sec);
                            }
                            else
                            {
                                cerr<<prompt<<"Ignoring bad command: "<<line<<endl;
                            }
                        }
                    }
                }
                catch(apdcam10g::error &d)
                {
                    output_lock lck;
                    cerr<<prompt<<" Terminated: "<<d.message()<<endl;
                }
                unlink(cmd_fifo_name_.c_str());
            });
    }

    template <safeness S>
    daq &daq::start(bool wait)
    {
        write_settings(configdir() / "daq.cnf");

        {
            auto pid_file_name = configdir() / "pid";
            ofstream pid_file(pid_file_name);
            if(!pid_file.good()) APDCAM_ERROR("Could not create PID file " + pid_file_name.string());
            pid_file<<getpid()<<endl;
        }

        network_threads_.clear();
        extractor_threads_.clear();
        for(unsigned int i=0; i<config::max_boards; ++i)
        {
            network_threads_active_[i].clear();
            extractor_threads_active_[i].clear();
        }
        processor_thread_active_.clear();
        
	// start creating the threads from tne end-consumer end, i.e. backwards, so that consumers are ready (should we further sync?)
	// and listening by the time data starts to arrive.
        
        // ----------------------------- processor thread -------------------------------------------
        {
            output_lock lck;
            cerr<<"[DAQ] Starting processor thread"<<endl;
        }
        processor_thread_ = std::jthread( [this](std::stop_token stok)
            {
                flag_locker flk(processor_thread_active_);
                const std::string prompt = "[DAQ/PROC] ";
                {
                    output_lock lck;
                    cerr<<prompt<<"Thread started"<<endl;
                }
                signal2exception::set("processor",SIGSEGV,SIGTERM);
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
                            for(auto a: all_enabled_channels_buffers_)
                            {
                                a->pop_to(needed);
                            }
                        }
                    
                        if(!non_terminated_exists)
                        {
                            python_analysis_stop_.test_and_set();  // Setting this will cause the python processor loop to stop
                            python_analysis_run_.test_and_set();   // This and the subsequent notification wakes up the python processor loop
                            python_analysis_run_.notify_one();     // and it will immediately learn that the stop flag was also set
                            break;
                        }
                    
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
                    flag_locker flk(extractor_threads_active_[i_socket]);
                    const std::string prompt = "[DAQ/EXT/" + std::to_string(i_socket) + "] ";
                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread "<<i_socket<<" started"<<endl;
                    }
                    signal2exception::set("extractor" + std::to_string(i_socket),SIGSEGV,SIGTERM);
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
                            cerr<<prompt<<"buffers' capacity   : "<<channel_buffer_size_<<endl;
                            if(sum/n > channel_buffer_size_/2) cerr<<prompt<<"we recommend increasing the buffer size"<<endl;
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
                        flag_locker flk(network_threads_active_[i_socket]);
                        const std::string prompt = "[DAQ/NET/" + std::to_string(i_socket) + "] ";
                        {
                            output_lock lck;
                            cerr<<prompt<<"Thread "<<i_socket<<" started"<<endl;
                        }
                        signal2exception::set("net" + std::to_string(i_socket),SIGSEGV,SIGTERM);
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

    daq &daq::wait_finish()
    {
        if(command_thread_.joinable()) command_thread_.join();
        if(processor_thread_.joinable()) processor_thread_.join();
        for(auto &t : extractor_threads_) if(t.joinable()) t.join();
        for(auto &t : network_threads_) if(t.joinable()) t.join();
        {
            output_lock lck;
            cerr<<"[DAQ] All threads have been joined"<<endl;
        }
        return *this;
    }

    template <safeness S>
    daq &daq::stop(unsigned int timeout_sec)
    {
        cerr<<"[DAQ] Stopping with timeout "<<timeout_sec<<endl;

        // These threads do not need to be requested to stop because they do so anyway if their input
        // ring_buffers get their 'terminated' flag set. 
        //processor_thread_.request_stop();
        //for(auto &t : extractor_threads_) t.request_stop();

        // Stopping the network threads will cause them to set the network_buffers_[i].terminated flag to true
        // as a consequence of which the channel_data_extractor threads will set the terminated flag of all of
        // the channel data buffers' flags to true, which in turn will cause the processor thread to
        // stop as well
        for(auto &t : network_threads_) t.request_stop();

        if(timeout_sec>0)
        {
            for(unsigned int t=0; t<timeout_sec; ++t)
            {
                sleep(1);
                if(network_threads()==0 && extractor_threads()==0 && processor_threads()==0) return *this;
                //auto [n1,n2,n3] = status();
                //if(n1==0 && n2==0 && n3==0) return *this;
            }
            kill();
        }

        return *this;
    }

    daq &daq::kill()
    {
        cerr<<"[DAQ] Killing all threads"<<endl;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test())   pthread_kill(network_threads_[i_socket].native_handle(), SIGTERM);
            if(extractor_threads_active_[i_socket].test()) pthread_kill(extractor_threads_[i_socket].native_handle(), SIGTERM);
        }
        if(processor_thread_active_.test()) pthread_kill(processor_thread_.native_handle(), SIGTERM);
        if(command_thread_active_.test()) pthread_kill(command_thread_.native_handle(), SIGTERM);

        return *this;
    }

    bool daq::network_thread_active(unsigned int i_stream) const
    {
        if(i_stream>=config::max_boards) return false;
        return network_threads_active_[i_stream].test();
    }
    bool daq::extractor_thread_active(unsigned int i_stream) const
    {
        if(i_stream>=config::max_boards) return false;
        return extractor_threads_active_[i_stream].test();
    }
    bool daq::processor_thread_active() const
    {
        return processor_thread_active_.test();
    }

    unsigned int daq::network_threads() const
    {
        unsigned int nthreads = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test()) ++nthreads;
        }
        return nthreads;
    }
    unsigned int daq::extractor_threads() const
    {
        unsigned int nthreads = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(extractor_threads_active_[i_socket].test()) ++nthreads;
        }
        return nthreads;
    }
    unsigned int daq::processor_threads() const
    {
        if(processor_thread_active_.test()) return 1;
        return 0;
    }

    /*
    tuple<unsigned int, unsigned int, unsigned int> daq::status() const
    {
        unsigned int active_network_threads = 0;
        unsigned int active_extractor_threads = 0;
        unsigned int active_processor_thread = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test()) ++active_network_threads;
            if(extractor_threads_active_[i_socket].test()) ++active_extractor_threads;
            if(processor_thread_active_.test()) active_processor_thread = 1;
        }
        return {active_network_threads, active_extractor_threads, active_processor_thread};
    }
    */

    size_t daq::received_packets(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size()) return 0;
        return network_buffers_[i_stream]->push_counter();
    }
    size_t daq::lost_packets(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size()) return 0;
        return network_buffers_[i_stream]->lost_packets();
    }

    size_t daq::network_buffer_content(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size()) return 0;
        return network_buffers_[i_stream]->size();
    }
    size_t daq::max_network_buffer_content(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size()) return 0;
        return network_buffers_[i_stream]->max_size();
    }

    size_t daq::channel_buffer_content(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_buffers_.size()) return 0;
        if(all_channels_buffers_[i_channel] == 0) return 0;
        return all_channels_buffers_[i_channel]->size();
    }
    size_t daq::max_channel_buffer_content(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_buffers_.size()) return 0;
        if(all_channels_buffers_[i_channel] == 0) return 0;
        return all_channels_buffers_[i_channel]->max_size();
    }
    size_t daq::channel_buffer_content_of_board(unsigned int i_adc) const
    {
        if(i_adc>=network_buffers_.size()) return 0;
        return board_last_channel_buffers_[i_adc]->size();
    }
    size_t daq::max_channel_buffer_content_of_board(unsigned int i_adc) const
    {
        if(i_adc>=network_buffers_.size()) return 0;
        return board_last_channel_buffers_[i_adc]->max_size();
    }
    
    size_t daq::extracted_shots(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_buffers_.size()) return 0;
        return all_channels_buffers_[i_channel]->push_counter();
    }
    size_t daq::extracted_shots_of_board(unsigned int i_adc) const
    {
        if(i_adc>=board_last_channel_buffers_.size()) return 0;
        return board_last_channel_buffers_[i_adc]->push_counter();
    }


    /*
    tuple<size_t,size_t> daq::statistics() const
    {
        size_t n_packets = 0;
        for(unsigned int i=0; i<network_buffers_.size(); ++i)
        {
            const auto c = network_buffers_[i]->push_counter();
            if(i==0 || c<n_packets) n_packets = c;
        }

        size_t n_shots = 0;
        for(unsigned int i=0; i<all_enabled_channels_buffers_.size(); ++i)
        {
            const auto c = all_enabled_channels_buffers_[i]->push_counter();
            if(i==0 || c<n_shots) n_shots = c;
        }

        return {n_packets,n_shots};
    }
    */

    daq &daq::clear_processors()
    {
        for(auto p : processors_) delete p;
        processors_.clear();
        return *this; 
    }

    void daq::diskdump_pause()
    {
        for(auto p : processors_)
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->pause();
        }
    }
    void daq::diskdump_resume()
    {
        for(auto p : processors_)
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->resume();
        }
    }

    void daq::diskdump_sampling(unsigned int s)
    {
        // Set the default write sampling to the given value
        processor_diskdump::default_sampling(s);
        
        // Also set the value for all existing instances of processor_diskdump
        for(auto p : processors_) 
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->sampling(s);
        }
    }

    template daq &daq::stop<safe>(unsigned int timeout_sec);
    template daq &daq::stop<unsafe>(unsigned int timeout_sec);
    template daq &daq::start<safe>(bool);
    template daq &daq::start<unsafe>(bool);
    template daq &daq::init<safe>  ();
    template daq &daq::init<unsafe>();
}


#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;

    void start_cmd_thread()
    {
        daq::instance().start_cmd_thread();
    }
    void stop_cmd_thread()
    {
        daq::instance().stop_cmd_thread();
    }

    unsigned int n_adc()
    {
        return daq::instance().n_adc();
    }
    unsigned int n_channels()
    {
        return daq::instance().n_channels();
    }

    void         start(bool wait) 
    { 
        try
        {
            daq::instance().start(wait); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }
    void         stop(bool wait) 
    { 
        try
        {
            daq::instance().stop(wait); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }
    void         kill_all() 
    { 
        try
        {
            daq::instance().kill(); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }
    void         version(apdcam10g::version v) 
    { 
        try
        {
            daq::instance().fw_version(apdcam10g::version(v)); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }
    void         dual_sata(bool d) { daq::instance().dual_sata(d); }

    void get_net_parameters()
    {
        try
        {
            daq::instance().get_net_parameters();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }

    void channel_masks(bool **m, int n_adc_boards)
    {
        try
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
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }

    void resolution_bits(unsigned int *r, int n_adc_boards)
    {
        try
        {
            std::vector<unsigned int> res(n_adc_boards);
            for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
            {
                res[i_adc_board] = r[i_adc_board];
            }
            daq::instance().resolution_bits(res);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }

    void init(bool is_safe)
    {
        try
        {
            if(is_safe) daq::instance().init<safe>();
            else        daq::instance().init<unsafe>();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    }

    void network_buffer_size(unsigned int bufsize)
        try
        {
            daq::instance().network_buffer_size(bufsize);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }
    void channel_buffer_size(unsigned int bufsize)
        try
        {
            daq::instance().channel_buffer_size(bufsize);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }

    unsigned int get_network_buffer_size()
    {
        return daq::instance().network_buffer_size();
    }
    unsigned int get_channel_buffer_size()
    {
        return daq::instance().channel_buffer_size();
    }

    unsigned int get_mtu()
    {
        return daq::instance().mtu();
    }
    unsigned int get_octet()
    {
        return daq::instance().octet();
    }


    void debug(bool d)
    {
        daq::instance().debug(d);
    }

    void add_processor_diskdump()
    {
        try
        {
            daq::instance().add_processor(new processor_diskdump);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }            
    }

    void add_processor_python()
    {
        try
        {
            daq::instance().add_processor(new processor_python);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }            
    }

    void write_settings(const char *filename)
    {
        try
        {
            daq::instance().write_settings(filename);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }            
    }

    void wait_finish()
        try
        {
            daq::instance().wait_finish();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }            

    void dump()
    {
        try
        {
            daq::instance().dump();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }            
    }

    void get_buffer(unsigned int absolute_channel_number, unsigned int *buffersize, apdcam10g::data_type **buffer)
        try
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
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    

    void python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter)
        try
        {
            daq::instance().python_analysis_wait_for_data(from_counter, to_counter);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
    

    void python_analysis_done(size_t from_counter)
        try
        {
            daq::instance().python_analysis_done(from_counter);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    

    void test()
    {
        cerr<<"daq::test"<<endl;
    }

    size_t received_packets(unsigned int i_stream)
    {
        return daq::instance().received_packets(i_stream);
    }
    size_t lost_packets(unsigned int i_stream)
    {
        return daq::instance().lost_packets(i_stream);
    }
    size_t network_buffer_content(unsigned int i_stream)
    {
        return daq::instance().network_buffer_content(i_stream);
    }
    size_t max_network_buffer_content(unsigned int i_stream)
    {
        return daq::instance().max_network_buffer_content(i_stream);
    }
    size_t channel_buffer_content(unsigned int i_channel)
    {
        return daq::instance().channel_buffer_content(i_channel);
    }
    size_t max_channel_buffer_content(unsigned int i_channel)
    {
        return daq::instance().max_channel_buffer_content(i_channel);
    }
    size_t channel_buffer_content_of_board(unsigned int i_adc)
    {
        return daq::instance().channel_buffer_content_of_board(i_adc);
    }
    size_t max_channel_buffer_content_of_board(unsigned int i_adc)
    {
        return daq::instance().max_channel_buffer_content_of_board(i_adc);
    }
    size_t extracted_shots(unsigned int i_channel)
    {
        return daq::instance().extracted_shots(i_channel);
    }
    size_t extracted_shots_of_board(unsigned int i_adc)
    {
        return daq::instance().extracted_shots_of_board(i_adc);
    }

    unsigned int network_threads()
    {
        return daq::instance().network_threads();
    }
    unsigned int extractor_threads()
    {
        return daq::instance().extractor_threads();
    }
    unsigned int processor_threads()
    {
        return daq::instance().processor_threads();
    }
    
    bool network_thread_active(unsigned int i_stream)
    {
        return daq::instance().network_thread_active(i_stream);
    }
    bool extractor_thread_active(unsigned int i_stream)
    {
        return daq::instance().extractor_thread_active(i_stream);
    }
    bool processor_thread_active()
    {
        return daq::instance().processor_thread_active();
    }
/*
    void statistics(unsigned int *n_packets, unsigned int *n_shots)
        try
        {
            auto [np,ns] = daq::instance().statistics();
            *n_packets = np;
            *n_shots   = ns;
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
    

    void status(unsigned int *n_active_network_threads, unsigned int *n_active_extractor_threads, unsigned int *n_active_processor_thread)
        try
        {
            auto [net,ext,proc] = daq::instance().status();
            *n_active_network_threads = net;
            *n_active_extractor_threads = ext;
            *n_active_processor_thread = proc;
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
*/  

    void clear_processors()
        try
        {
            daq::instance().clear_processors();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
    

    void diskdump_sampling(unsigned int s)
        try
        {
            daq::instance().diskdump_sampling(s);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
    

    bool python_analysis_stop()
    {
        try
        {
            return daq::instance().python_analysis_stop();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Exception was thrown"<<endl; }    
        return false;
    }

}





#endif
