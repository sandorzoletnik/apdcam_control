#include <iostream>
#include <fstream>

#include "daq.h"
#include "utils.h"
#include "error.h"
#include "pstream.h"
#include "channel_data_extractor.h"
#include <memory>

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

    void daq::finish()
    {
        for(auto p : processors_) p->finish();
    }

    template <safeness S>
    daq &daq::init(bool dual_sata, const std::vector<std::vector<bool>> &channel_masks, const std::vector<unsigned int> &resolution_bits, version ver)
    {

        if(mtu_ == 0) APDCAM_ERROR("MTU has not been set");
        if(channel_masks.size() != resolution_bits.size()) APDCAM_ERROR("Masks and resolution sizes do not agree");

        channel_masks_ = channel_masks;
        resolution_bits_ = resolution_bits;

        const unsigned int nof_adc = channel_masks.size();

        if(mtu_==0) APDCAM_ERROR("MTU has not yet been specified in daq::initialize");
        if(dual_sata && nof_adc>2) APDCAM_ERROR("Dual sata is set with more than two ADC boards present");

        // Resize the socket vector to have as many elements as there are ADC boards
        sockets_.clear(); // delete any previously open socket, if any
        sockets_.resize(nof_adc);

        // Resize the network buffer vector to have as many elements as there are ADC boards. Initialize their buffer size
        regenerate(network_buffers_,nof_adc,network_buffer_size_,max_udp_packet_size_);

        // Resize the extractors vector to have as many elements as there are ADC boards
//        regenerate(extractors_,nof_adc,x.......); implement regenerate with invokable.
        regenerate_by_func(extractors_, nof_adc, [this,ver](unsigned int i_adc){return new channel_data_extractor<default_safeness>(this,ver,i_adc);});

//        extractors_.clear();
//        extractors_.resize(nof_adc,{this,ver});

        // Calculate the all_enabled_channels_info / board_enabled_channels_info vectors (ranges), and the
        // number of all enabled channels
        calculate_channel_info();

        all_channels_buffers_.clear();

        // Resize the channels buffers, so that the sub-ranges (per ADC board) can be set up on the fly (i.e.
        // the 'all_enabled_channels_buffers_' vector is not resized anymore, which would invalidate its sub-ranges)
        regenerate(all_enabled_channels_buffers_,nof_adc*config::channels_per_board,0);

        // The per-board channels vector (in fact: subrange of the 'all_enabled_channels_buffers_')
        board_enabled_channels_buffers_.clear();
        board_enabled_channels_buffers_.resize(nof_adc);

        board_last_channel_buffers_.resize(nof_adc);

        all_channels_buffers_.clear();
        all_channels_buffers_.resize(nof_adc*config::channels_per_board,0);

        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            const int port_index = (dual_sata ? i_adc*2 : i_adc);
	    cerr<<"Listening on port "<<config::ports[i_adc*2]<<endl;

	    sockets_[i_adc].open(config::ports[port_index]);

            // Loop over all enabled channels of this board
            for(auto c : board_enabled_channels_info_[i_adc])
            {
                channel_data_buffer_t *b = new channel_data_buffer_t(sample_buffer_size_,sample_buffer_extra_size_);
                b->copy_values(*c);
                all_enabled_channels_buffers_[c->absolute_channel_number] = b;
                board_enabled_channels_buffers_[i_adc].push_back(b);
                board_last_channel_buffers_[i_adc] = b;
            }
        }

        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            cerr<<"ADC"<<i_adc<<" bytes per sample: "<<board_bytes_per_shot_[i_adc]<<endl;
        }

        for(auto p : processors_) p->init();

        return *this;
    }


    template <safeness S>
    daq &daq::start(bool wait)
    {
        network_threads_.clear();
        extractor_threads_.clear();

	// Start creating the threads from tne end-consumer end, i.e. backwards, so that consumers are ready (should we ensure
	// syncing?) and listening by the time data starts to arrive.


        // ----------------------------- Processor thread -------------------------------------------
        {
            processor_thread_ = std::jthread( [this](std::stop_token stok)
                {
                    try
                    {
                        for(unsigned int to_counter=process_period_; !stok.stop_requested(); to_counter += process_period_)
                        {
                            size_t common_pop_counter=0;
                            size_t common_push_counter=0;

                            // Check the last channels of each board, and spin-lock wait until they have a required new number
                            // of entries, or are terminated
                            bool non_terminated_exists = false;
                            for(int i=0; i<board_last_channel_buffers_.size(); ++i)
                            {
                                const channel_data_buffer_t *b = board_last_channel_buffers_[i];
                                bool terminated;
                                size_t push_counter;
                                while( (push_counter=b->push_counter())<to_counter && (terminated=b->terminated())==false );
                                // Re-query to capture the case when between the two AND-ed conditions in the while loop there were new
                                // entries added to the ring_buffer, and it was terminated as well.
                                if(terminated) push_counter = b->push_counter();
                                else non_terminated_exists = true;
                                if(i==0 || push_counter<common_push_counter) common_push_counter = push_counter;
                                
                                size_t pop_counter = b->pop_counter();
                                if(pop_counter > common_pop_counter) common_pop_counter = pop_counter;
                            }

                            if(common_push_counter > common_pop_counter)
                            {
                                // The counter to indicate the first element that is required to stay in the buffers
                                // by any of the processors
                                unsigned int needed = common_push_counter;
                                for(auto p : processors_) 
                                {
                                    const unsigned int this_processor_needs = p->run(common_pop_counter, common_push_counter);
                                    if(this_processor_needs > common_push_counter) APDCAM_ERROR("Processor returned too high needed value");
                                    if(this_processor_needs < needed) needed = this_processor_needs;
                                }
                                
                                // After all tasks have run, and reported what the earliest element in the buffers that they
                                // need for further processing, clear the buffers up to this
                                for(auto a: all_enabled_channels_buffers_) a->pop_to(needed);
                            }

                            if(!non_terminated_exists) break;
                        }
                    }
                    catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                });
        }

        // ------------------- Channel data extractor threads ------------------------------------------
        if(separate_extractor_threads_)  // Start one thread for each socket
        {
            for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
            {
                extractor_threads_.push_back(std::jthread( [this, i_socket](std::stop_token stok)
                    {
                        try
                        {
                            while(!stok.stop_requested())
			    {
				if(extractors_[i_socket]->run(network_buffers_[i_socket],board_enabled_channels_buffers_[i_socket]) < 0) break;
			    }
                        }
                        catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                    }));
            }
        }
        else
        {
            extractor_threads_.push_back(std::jthread( [this](std::stop_token stok)
                {
                    try
                    {
                        while(!stok.stop_requested())
                        {
                            int n_active = 0;
                            for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
                            {
                                // If input is terminated, 'run' returns a negative number. Otherwise (zero or larger returned value) the
                                // input socket is still active
                                if(extractors_[i_socket]->run(network_buffers_[i_socket],board_enabled_channels_buffers_[i_socket]) >= 0) ++n_active;
                            }
                            if(n_active == 0) break;
                        }
                    }
                    catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                }));
        }


        // -------------------------------- Network reading thread(s) ----------------------------------
        // These threads do nothing but continuously read UDP packets into the udp packet buffers 'network_buffers_'
        if(separate_network_threads_)  // Start one thread for each socket
        {
            for(unsigned int i=0; i<sockets_.size(); ++i)
            {
                // Make the corresponding socket blocking. This thread is reading only from one socket, so
                // we can safely be blocked until data is available 
                sockets_[i].blocking(true);

                network_threads_.push_back(std::jthread( [this, i](std::stop_token stok)
                    {
                        try
                        {
                            while(!stok.stop_requested())
                            {
			      // We should set a timeout for the receive, in which case we break the loop
			      // (the camera does not give any signal of having finished the data stream,
			      // it simply ceases sending data)
			      // Implement a "first" flag. If this is true, timeout can be long (waiting for the first event),
			      // but when it's false, we should not wait too much. 
			      const auto received_packet_size = network_buffers_[i]->receive(sockets_[i]);

			      // Reached the end of the stream
			      if(received_packet_size != max_udp_packet_size_ || network_buffers_[i]->terminated()) break;
                            }
                        }
                        catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                    }));
            }
        }
        else
        {
            // First, make all input sockets non-blocking, because we continuously loop
            // over all of them, and if there is no data in one of them, that should not block
            // the loop from trying to read from the others
            for(unsigned int i=0; i<sockets_.size(); ++i) sockets_[i].blocking(false);

            network_threads_.push_back(std::jthread( [this](std::stop_token stok)
                {
                    try
                    {
                        std::vector<bool> socket_active(sockets_.size(),true);
		      
                        while(!stok.stop_requested())
                        {
                            unsigned int n_active_sockets = 0;
                            for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
                            {
                                if(!socket_active[i_socket] || network_buffers_[i_socket]->terminated()) continue;
                                // We should set a timeout here as well.
                                const auto received_packet_size = network_buffers_[i_socket]->receive(sockets_[i_socket]);
                                
                                if(received_packet_size != max_udp_packet_size_) socket_active[i_socket] = false;
                                else ++n_active_sockets;
                            }
                            
                            // stop the thread if there are no more active sockets 
                            if(n_active_sockets == 0) break;
                        }
                    }
                    catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                }));
        }


	// Create 
	{
	  processor_thread_ = std::jthread([this](std::stop_token stok)
					   {
					     try
					       {
						 while(!stok.stop_requested())
						   {
						     cerr<<"processor thread is not yet ipmlemented"<<endl;
						   }
					       }
					     catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
					   });
	}

        sleep(1);

        if(wait)
        {
            for(auto &t : network_threads_) if(t.joinable()) t.join();
            for(auto &t : extractor_threads_) if(t.joinable()) t.join();
	    if(processor_thread_.joinable()) processor_thread_.join();
        }

        return *this;
    }

    template <safeness S>
    daq &daq::stop(bool wait)
    {
        processor_thread_.request_stop();
        for(auto &t : extractor_threads_) t.request_stop();
        for(auto &t : network_threads_) t.request_stop();
        if(wait)
        {
            if(processor_thread_.joinable()) processor_thread_.join();
            for(auto &t : extractor_threads_) if(t.joinable()) t.join();
            for(auto &t : network_threads_) if(t.joinable()) t.join();
        }
        return *this;
    }

    template daq &daq::stop<safe>(bool);
    template daq &daq::stop<unsafe>(bool);
    template daq &daq::start<safe>(bool);
    template daq &daq::start<unsafe>(bool);
    template daq &daq::init<safe>  (bool, const std::vector<std::vector<bool>>&, const std::vector<unsigned int>&, version);
    template daq &daq::init<unsafe>(bool, const std::vector<std::vector<bool>>&, const std::vector<unsigned int>&, version);
}

#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;
    daq         *create() { return new daq; }
    void         destroy(daq *self) { delete self; }
    void         mtu(daq *self, int m) { self->mtu(m); }
    void         start(daq *self, bool wait) { self->start(wait); }
    void         stop(daq *self, bool wait) { self->stop(wait); }
    void         init(daq *self, bool dual_sata, int n_adc_boards, bool **channel_masks, unsigned int *resolution_bits, version ver, bool is_safe)
    {
        std::vector<std::vector<bool>> chmasks(n_adc_boards);
        std::vector<unsigned int> rbits(n_adc_boards);
        for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
        {
            chmasks[i_adc_board].resize(4);
            rbits[i_adc_board] = resolution_bits[i_adc_board];
            for(unsigned int i_board_channel=0; i_board_channel<config::channels_per_board; ++i_board_channel)
            {
                chmasks[i_adc_board].push_back(channel_masks[i_adc_board][i_board_channel]);
            }
        }

        if(is_safe) self->init<safe>(dual_sata,chmasks,rbits,ver);
        else        self->init<unsafe>(dual_sata,chmasks,rbits,ver);
    }
}
#endif
