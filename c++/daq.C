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

    daq::~daq()
    {
        for(auto e : extractors_) delete e;
    }

    template <safeness S>
    daq &daq::init(bool dual_sata, const std::vector<std::vector<std::vector<bool>>> &channel_masks, const std::vector<unsigned int> &resolution_bits, version ver)
    {
        if(mtu_ == 0) APDCAM_ERROR("MTU has not been set");
        if(channel_masks.size() != resolution_bits.size()) APDCAM_ERROR("Masks and resolution sizes do not agree");

        channel_masks_ = channel_masks;
        resolution_bits_ = resolution_bits;

        sockets_.clear(); // delete any previously open socket, if any

        const int nof_adc = channel_masks.size();

        if(mtu_==0) APDCAM_ERROR("MTU has not yet been specified in daq::initialize");
        if(dual_sata && nof_adc>2) APDCAM_ERROR("Dual sata is set with more than two ADC boards present");

        sockets_.resize(nof_adc);
	network_buffers_.resize(nof_adc);
        channel_data_buffers_.resize(nof_adc);
        channel_data_buffers_map_.clear();
        for(auto e: extractors_) delete e;
        extractors_.resize(nof_adc);

        calculate_channel_info();

        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            const int port_index = (dual_sata ? i_adc*2 : i_adc);
	    cerr<<"Listening on port "<<ports_[i_adc*2]<<endl;
	    sockets_[i_adc].open(ports_[port_index]);

	    network_buffers_[i_adc].resize(network_buffer_size_,max_udp_packet_size);

            extractors_[i_adc] = new channel_data_extractor<S>(this,i_adc,ver,sample_buffer_size_);

            channel_data_buffers_[i_adc].resize(channelinfo_[i_adc].size());

            // Now loop over all enabled channels of this ADC board
            for(unsigned int i_enabled_channel=0; i_enabled_channel<channelinfo_[i_adc].size(); ++i_enabled_channel)
            {
                // Resize the corresponding ring buffer
                channel_data_buffers_[i_adc][i_enabled_channel].resize(sample_buffer_size_,2*process_period_);
                // Copy the inherited properties channel_info::XXX
                channel_data_buffers_[i_adc][i_enabled_channel] = channelinfo_[i_adc][i_enabled_channel];
                // And index the set of ring buffers by absolute channel number
                channel_data_buffers_map_[channelinfo_[i_adc][i].absolute_channel_number] = std::addressof(channel_data_buffers_[i_adc][i]);
            }
        }

        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            cerr<<"ADC"<<i_adc<<" bytes per sample: "<<board_bytes_per_shot_[i_adc]<<endl;
        }

        for(auto e : extractors_) e->init();
        for(auto p : processors_) p->init();

        return *this;
    }

    void daq::flush()
    {
      cerr<<"daq::flush is not yet implemented"<<endl;
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
                        for(unsigned int to_counter=process_period; !stok.stop_requested(); to_counter += process_period_)
                        {
                            unsigned int from_counter = 0;
                            // Wait until all channel data buffers have 'process_period_' new elements. Also calculate
                            // the biggest front_counter of all buffers (i.e. the start of the counter-range that is guaranteed
                            // to be available in all buffers)
                            for(auto &a: channel_data_buffers_)
                            {
                                for(auto &b: a)
                                {
                                    b.wait_for_counter(to_counter);
                                    if(b.front_counter() > from_counter) from_counter = b.front_counter();
                                }
                            }
                            // The counter to indicate the first element that is required to stay in the buffers
                            // by any of the processors
                            unsigned int need = to_counter;
                            for(auto p : processors_) 
                            {
                                const unsigned int this_processor_needs = p->run(from_counter, to_counter);
                                if(this_processor_needs < need) need = this_processor_needs;
                            }

                            // After all tasks have run, and reported what the earliest element in the buffers that they
                            // need for further processing, clear the buffers up to this
                            for(auto &a: channel_data_buffers_)
                            {
                                for(auto &b: a)
                                {
                                    if(need > b.front_counter()) b.pop_front(need - b.front_counter());
                                    else if(need < b.front_counter()) APDCAM_ERROR("One of the tasks returned a too small need: " + std::to_string(need));
                                }
                            }
                        }
                    }
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
				extractors_[i_socket]->run(network_buffers_[i_socket],channel_data_buffers_[i_socket]);
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
                            for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
                            {
                                extractors_[i_socket]->run(network_buffers_[i_socket],channel_data_buffers_[i_socket]);
                            }
                        }
                    }
                    catch(apdcam10g::error &e) { cerr<<e.full_message()<<endl; }
                }));
        }


        // Start the producer (reading from the UDP sockets) threads
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
			      const auto received_packet_size = network_buffers_[i].receive(sockets_[i]);

			      // Reached the end of the stream
			      if(received_packet_size != max_udp_packet_size_) break;
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
			      if(!socket_active[i_socket]) continue;
			      // We should set a timeout here as well.
			      const auto received_packet_size = network_buffers_[i].receive(sockets_[i]);
			      
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
        for(auto &t : producer_threads_) t.request_stop();
        for(auto &t : consumer_threads_) t.request_stop();
        if(wait)
        {
            for(auto &t : producer_threads_) if(t.joinable()) t.join();
            for(auto &t : consumer_threads_) if(t.joinable()) t.join();
        }
        return *this;
    }

    template daq &daq::stop<safe>(bool);
    template daq &daq::stop<unsafe>(bool);
    template daq &daq::start<safe>(bool);
    template daq &daq::start<unsafe>(bool);
    template daq &daq::initialize<safe>  (bool, const std::vector<std::vector<std::vector<bool>>>&, const std::vector<unsigned int>&, version);
    template daq &daq::initialize<unsafe>(bool, const std::vector<std::vector<std::vector<bool>>>&, const std::vector<unsigned int>&, version);
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
    void         initialize(daq *self, bool dual_sata, int n_adc_boards, bool ***channel_masks, unsigned int *resolution_bits, version ver, bool is_safe)
    {
        std::vector<std::vector<std::vector<bool>>> chmasks(n_adc_boards);
        std::vector<unsigned int> rbits(n_adc_boards);
        for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
        {
            chmasks[i_adc_board].resize(4);
            rbits[i_adc_board] = resolution_bits[i_adc_board];
            for(unsigned int i_chip=0; i_chip<4; ++i_chip)
            {
                chmasks[i_adc_board][i_chip].resize(8);
                for(unsigned int i_channel=0; i_channel<8; ++i_channel)
                {
                    chmasks[i_adc_board][i_chip][i_channel] = channel_masks[i_adc_board][i_chip][i_channel];
                }
            }
        }

        if(is_safe) self->initialize<safe>(dual_sata,chmasks,rbits,ver);
        else        self->initialize<unsafe>(dual_sata,chmasks,rbits,ver);
    }
    void         dump(daq *self) { self->dump(); }
}
#endif
