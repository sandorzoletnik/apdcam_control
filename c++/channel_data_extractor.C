#include <fstream>
#include <iostream>
#include "channel_data_extractor.h"
#include "daq.h"
#include "utils.h"
#include "terminal.h"
#include "shot_data_layout.h"

using namespace std;

namespace apdcam10g
{

    template <safeness S>
    channel_data_extractor<S>::channel_data_extractor(daq *d,version ver,unsigned int adc) : daq_(d), adc_(adc) // : data_consumer(adc,ver)
    {
        // Create a version-specific packet handler
        packet_      = packet::create(ver);
        next_packet_ = packet::create(ver);
    }

    template <safeness S>
    channel_data_extractor<S>::~channel_data_extractor()
    {
      delete packet_;
      delete next_packet_;
    }

    template <safeness S>
    int channel_data_extractor<S>::run(udp_packet_buffer<S> &network_buffer, 
                                       const std::vector<ring_buffer<apdcam10g::data_type,channel_info>*> &board_enabled_channels_buffers)

    {

        // Spin-lock wait until we have a packet in the buffer
        while( network_buffer.empty() && !network_buffer.terminated() );

        // If the spin-lock was broken due to the stream being terminated, return
        if(network_buffer.empty()) return 0;

        packet_->data(network_buffer[0].address,network_buffer[0].size);
        if(debug_) cerr<<"****** PACKET: "<<packet_->adc_data_size()<<endl<<endl;

        // The offset of the first byte of a shot within the UDP packet's ADC data. It is signed because
        // it can be negative in case a shot is split between two consecutive packets
        int shot_offset = 0;

        const shot_data_layout layout(daq_->board_bytes_per_shot(adc_),
                                      daq_->resolution_bits(adc_),
                                      board_enabled_channels_buffers);
/*
        if(debug_)
        {
            output_lock lck;
            for(int shot_offset_tmp=0; shot_offset_tmp<packet_->adc_data_size(); shot_offset_tmp += daq_->board_bytes_per_shot(adc_))
            {
                layout.show(packet_->adc_data_start()+shot_offset_tmp, cerr, 0, packet_->adc_data_size()-shot_offset_tmp);
            }
        }
*/
        while(true)
        {
            for(unsigned int i_channel=0; i_channel<board_enabled_channels_buffers.size(); ++i_channel)
            {
                auto c = board_enabled_channels_buffers[i_channel];
                
                // If this value is fully contained in the packet, just get it
                if(shot_offset + c->byte_offset + c->nbytes <= packet_->adc_data_size())
                {
                    c->push(c->get_from_shot(packet_->adc_data_start() + shot_offset));
                }
                else
                {
                    // If the first byte is still in the current packet, i.e. the value 
                    // spills over into the next packet
                    if(shot_offset + c->byte_offset < packet_->adc_data_size())
                    {
                        while(network_buffer.size()<2 && !network_buffer.terminated());
                        if(network_buffer.size()<2)
                        {
                            for(auto c : board_enabled_channels_buffers) c->terminate();
                            return 0;
                        }
                        next_packet_->data(network_buffer[1].address, network_buffer[1].size);
                        if(debug_) cerr<<"****** PACKET: "<<next_packet_->adc_data_size()<<endl<<endl;
                        
                        memcpy(packet_->adc_data_start()+packet_->adc_data_size(), next_packet_->adc_data_start(), 2);
                        auto tmp = c->get_from_shot(packet_->adc_data_start() + shot_offset);
                        c->push(tmp);

                        shot_offset = shot_offset - (int)packet_->adc_data_size();
                        network_buffer.pop();
                        swap(packet_, next_packet_);

/*
                        if(debug_)
                        {
                            for(int shot_offset_tmp = shot_offset; shot_offset_tmp < (int)packet_->adc_data_size(); shot_offset_tmp += daq_->board_bytes_per_shot(adc_))
                            {
                                layout.show(packet_->adc_data_start()+shot_offset_tmp,
                                            cerr,
                                            (shot_offset_tmp > 0 ? 0 : -shot_offset_tmp),
                                            packet_->adc_data_size()-shot_offset_tmp);
                            }
                        }
*/
                    }
                    // The value is entirely in the new packet
                    else
                    {
                        network_buffer.pop();
                        while(network_buffer.empty() && !network_buffer.terminated());
                        if(network_buffer.empty())
                        {
                            for(auto c : board_enabled_channels_buffers) c->terminate();
                            return 0;
                        }
                        packet_->data(network_buffer[0].address, network_buffer[0].size);
                        if(debug_) cerr<<"****** PACKET: "<<packet_->adc_data_size()<<endl<<endl;
                        shot_offset = shot_offset - (int)packet_->adc_data_size();
                        // If it is the first channel, i.e. a new shot starts entirely in the next packet, set the offset to zero
                        if(i_channel==0) shot_offset=0;
                        auto tmp = c->get_from_shot(packet_->adc_data_start() + shot_offset);
                        c->push(tmp);

/*
                        if(debug_)
                        {
                            for(int shot_offset_tmp = shot_offset; shot_offset_tmp < (int)packet_->adc_data_size(); shot_offset_tmp += daq_->board_bytes_per_shot(adc_))
                            {
                                layout.show(packet_->adc_data_start()+shot_offset_tmp,
                                            cerr,
                                            (shot_offset_tmp > 0 ? 0 : -shot_offset_tmp),
                                            packet_->adc_data_size()-shot_offset_tmp);
                            }
                        }
*/
                    }
                }
            }
            // When all channels have been processed (i.e. a full shot), increment shot_offset
            shot_offset += daq_->board_bytes_per_shot(adc_);
        }

        /*
        while(true)
        {
            //cerr<<endl;
            //cerr<<"** channel: "<<i_enabled_channel<<endl;

            // The pointer to the ring buffer corresponding to channel 'i_enabled_channel'
            const auto channel_buffer = board_enabled_channels_buffers[i_enabled_channel];

            // Get the begin (first byte) and end (one beyond the last byte) pointers of the data
            const apdcam10g::byte *value_begin = packet_->adc_data_start() + shot_offset + channel_buffer->byte_offset;
            const apdcam10g::byte *value_end   = value_begin + channel_buffer->nbytes;

            // If the channel value is fully contained in this packet, store it
            if(value_end <= packet_->end())
            {
                //cerr<<"** channel data entirely in this packet"<<endl;
                auto tmp = store_channel_value_(value_begin,channel_buffer);
                //cerr<<"** channel vaule: "<<tmp<<endl;

                // If we have just processed the last enabled channel, it means we finished a shot
                if(++i_enabled_channel == board_enabled_channels_buffers.size())
                {
                    //cerr<<"** we reached end of shot"<<endl;
                    
                    // Reset the channel index back to zero
                    i_enabled_channel = 0;
                    // increment the shot offset 
                    shot_offset += daq_->board_bytes_per_shot(adc_);

                    //cerr<<"** new shot offset: "<<shot_offset<<endl;

                    if(shot_offset >= packet_->adc_data_size())
                    {
                        cerr<<"** we need a new packet"<<endl;

                        // Set the shot offset to the value within the next UDP packet (we anticipate there is
                        // a new packet, if not, we quit anyway)
                        shot_offset -= packet_->adc_data_size();

                        cerr<<"** shot offset: "<<shot_offset<<endl;

                        // Remove the current packet from the buffer
                        network_buffer.pop();

                        // spin-lock wait for a new UDP packet
                        while(network_buffer.empty() && !network_buffer.terminated());
                        if(network_buffer.empty())
                        {
                            for(auto c: board_enabled_channels_buffers) c->terminate();
                            return 0;
                        }

                        packet_->data(network_buffer[0].address, network_buffer[0].size);

                        continue;
                    }
                }

                // The packet was fully processed... (coincidence between the border of a channel value and the border of a packet
                if(value_end == packet_->end())
                {
                    cerr<<"** the packet was fully processed, need a new packet"<<endl;

                    shot_offset = 0;
                    network_buffer.pop();

                    // Wait for the next packet to arrive
                    while(network_buffer.empty() && !network_buffer.terminated());
                    if(network_buffer.empty())
                    {
                        for(auto c: board_enabled_channels_buffers) c->terminate();
                        return 0;
                    }

                    packet_->data(network_buffer[0].address, network_buffer[0].size);

                }
            }
            // Otherwise this channel's data is rolling over into the next UDP packet
            else
            {
                while(network_buffer.size()<2 && !network_buffer.terminated());
                if(network_buffer.size()<2)
                {
                    for(auto c: board_enabled_channels_buffers) c->terminate();
                    return 0;
                }

                next_packet_->data(network_buffer[1].address, network_buffer[1].size);
                
                apdcam10g::byte tmp[3];
                const int nbytes_in_current_packet = packet_->end() - value_begin;
                for(int i=0; i<channel_buffer->nbytes; ++i)
                {
                    if(value_begin+i<packet_->end())
                    {
                        tmp[i] = value_begin[i];
                    }
                    else
                    {
                        tmp[i] = next_packet_->adc_data_start()[i - nbytes_in_current_packet];
                    }
                }
                store_channel_value_(tmp,channel_buffer);
                if(++i_enabled_channel == board_enabled_channels_buffers.size())
                {
                    cerr<<"** we reached the end of a shot..."<<endl;

                    i_enabled_channel = 0;
                    shot_offset += daq_->board_bytes_per_shot(adc_);

                    // should always be true!
                    if(shot_offset >= packet_->adc_data_size()) shot_offset -= packet_->adc_data_size();
                }
                else
                {
                    cerr<<"** the shot continues"<<endl;
                    // The shot started in the current (the 'to be previous') packet, and is rolling over into the next UDP packet.
                    // We will set the 'current packet pointer' to the next one, so the shot_offset must be negative,
                    // since the shot started in the 'to be previous' packet
                    shot_offset = shot_offset - (int)packet_->adc_data_size();
                    cerr<<"** shot_offset = "<<shot_offset<<endl;
                }
                // Make next_packet_ the current one, i.e. copy next_packet_ --> packet_. But we need to keep the other one as well
                // for deletion at the end, and also for next time when we want to query ahead the next packet. So we swap.
                swap(packet_,next_packet_);
            }
        }
        */
        return 0;
    }

    template class channel_data_extractor<safe>;
    template class channel_data_extractor<unsafe>;
}                           
