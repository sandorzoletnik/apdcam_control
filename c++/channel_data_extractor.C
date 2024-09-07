#include <fstream>
#include <iostream>
#include "channel_data_extractor.h"
#include "daq.h"

using namespace std;

namespace apdcam10g
{

    template <safeness S>
    channel_data_extractor<S>::channel_data_extractor(daq *d,version ver,unsigned int adc) : daq_(d), adc_(adc) // : data_consumer(adc,ver)
    {
        // Create a version-specific packet handler
        packet_ = packet::create(ver);
        next_packet_ = packet::create(ver);
    }

    template <safeness S>
    channel_data_extractor<S>::~channel_data_extractor()
    {
      delete packet_;
      delete next_packet_;
    }

    template <safeness S>
    int channel_data_extractor<S>::run(udp_packet_buffer<S> *network_buffer, 
                                       const std::vector<ring_buffer<apdcam10g::data_type,channel_info>*> &channel_data_buffers)

    {
        // Wait for new packages
        size_t npackets;
        bool terminated;
        while( (npackets=network_buffer->size()) == 0 && (terminated=network_buffer->terminated())==false );

        // Re-query the size: between the evaluation of the two conidtions within while there could be
        // a new partial package arriving (i.e. setting size>0) which triggers the terminate flag.
        npackets = network_buffer->size();

        // If there are indeed no new packets, then it must have been the 'terminated' flag which caused
        // quitting the while loop. In principle all previous packets must have contained entire shots,
        // and have been removed. We can terminate the output queues
        if(npackets==0)
        {
            for(auto c : channel_data_buffers) c->terminate();
            return -1;
        }

        const unsigned int board_bytes_per_shot = daq_->board_bytes_per_shot(adc_);

        unsigned int removed_packets = 0;

        // Strategy: We try to consume as many complete shots as possible. If we fully process one UDP packet, we remove it, if there
        // remains some incomplete show, the UDP packet is kept, but the internal offset 'shot_offset_within_adc_data_' will indicate
        // where this incomplete shot starts within the UDP packet, so next time we will start from there.

        for(int ipacket=0; ipacket<npackets; )
        {
	    {
                const auto p = (*network_buffer)[0];  // Get the front element
                packet_->data(p.address,p.size);
	    }

            // Loop over the shots stored in this packet
            while(true)
            {
                // if the entire shot fits into this packet
                if(packet_->adc_data_start()+shot_offset_within_adc_data_+board_bytes_per_shot <= packet_->end())
                {
                    // In the loop below, 'c' is a channel data ring buffer that inherits from channel_info, so it contains all
                    // information on the channels (mask, byte offset, channel numbers etc)
                    for(auto c : channel_data_buffers) c->push(this->get_channel_value_(packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset,c));

                    // Advance the offset within the packet by the length of one shot
                    shot_offset_within_adc_data_ += board_bytes_per_shot;
                    
                    // if by this we finished processing the given UDP packet, then reset the offset to zero, and
                    // remove this UDP packet from the buffer
                    // (the > should never happen in this condition, only =, but let's be sure)
                    if(packet_->adc_data_start()+shot_offset_within_adc_data_ >= packet_->end())
                    {
                        // Remove the packet from the ring buffer
                        network_buffer->pop();
                        ++ipacket;
                        ++removed_packets;
                        // Set the pointer-within-packet to zero
                        shot_offset_within_adc_data_ = 0;
                        break;
                    }

                    // If we have not reached the end of the packet, then go to the next shot
                    continue;
                }

                // If we are here, then the current shot did not fit entirely into this packet, but extends into the next one
                // Check if there is a next packet.
                if(ipacket+1<npackets)
                {
                    // Initialize a next packet within the buffer, without actually incrementing the read pointer
                    // of the ring buffer
  		    {
                        const auto p = (*network_buffer)[1];
                        next_packet_->data(p.address,p.size);
		    }
                                     
                    // Loop over the channel data buffers. Remember that these are inheriting from channel_info so we can obtain all channel-related information
                    // such as absolute number etc. 
                    for(auto c : channel_data_buffers)
                    {
                        // If all bytes of this channel still fit into this packet
                        if(packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset+c->nbytes <= packet_->end())
                        {
                            c->push(this->get_channel_value_(packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset,c));
                        }
                        
                        // If not all bytes of this channel are within the current packet, but it starts in this packet,
                        // then it's a split value. Most complicated case
                        else if(packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset < packet_->end())
                        {
                            // Create a temporary buffer (we need max. 3 bytes) which will continuously store the
                            // bytes of this channel value
                            std::byte tmp[3];
                            for(unsigned int i=0; i<c->nbytes; ++i)
                            {
                                if(packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset+i < packet_->end()) tmp[i] = packet_->adc_data_start()[shot_offset_within_adc_data_+c->byte_offset+i];
                                else tmp[i] = next_packet_->adc_data_start()[shot_offset_within_adc_data_+c->byte_offset+i-packet_->adc_data_size()];
                                c->push(this->get_channel_value_(tmp,c));
                            }
                        }
                        
                        // If this channel data is entirely in the next packet
                        else
                        {
                            c->push(this->get_channel_value_(next_packet_->adc_data_start()+shot_offset_within_adc_data_+c->byte_offset-packet_->adc_data_size(), c));
                        }
                    }
                    
                    // We can safely assume that if a shot was extending into the next packet, it is entirely contained there,
                    // and does not extend into a further packet. 
                    // Remove the packet from the ring buffer
                    network_buffer->pop();
                    ++ipacket;
                    ++removed_packets;

                    // Set the pointer-within-packet to zero
                    shot_offset_within_adc_data_ = shot_offset_within_adc_data_ + board_bytes_per_shot - packet_->adc_data_size();

                    // Break looping over the samples within the current packet, go to the next one (which has already been partially processed,
                    // but this is taken care of by shot_offset_within_adc_data_ being set to some positive offset)
                    break; 
                }

                break;
            }
        }

        return removed_packets;
    }

    template class channel_data_extractor<safe>;
    template class channel_data_extractor<unsafe>;
}                           
