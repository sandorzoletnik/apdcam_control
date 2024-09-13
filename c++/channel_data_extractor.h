#ifndef __APDCAM10G_CHANNEL_DATA_EXTRACTOR_H__
#define __APDCAM10G_CHANNEL_DATA_EXTRACTOR_H__

#include "config.h"
#include "udp_packet_buffer.h"
#include "packet.h"
#include "typedefs.h"
#include "channel_info.h"

#include <ranges>

/*

  A worker class derived from data_consumer, which consumes APDCAM data (UDP packets) from the ring buffer,
  and extracts the channel-by-channel data into linear buffers (per channel). Once these buffers are full,
  they are dumped to disk. The linear buffers are protected from swapping by mlock.

  One instance of this class handles one data stream at one port, i.e. one ADC
  
 */


namespace apdcam10g
{
    class daq;

    template <safeness S=default_safeness>
    class channel_data_extractor 
    {
    private:
        
        bool debug_ = false;

        channel_data_extractor(const channel_data_extractor<S> &);

        // A pointer to a daq object to query packet size, number of bytes per ADC chip, per ADC board, etc
        daq *daq_ = 0;

        // The ADC number, the data of which this consumer will be processing
        unsigned int adc_ = 0;
        
        packet *packet_ = 0, *next_packet_ = 0;

        // The offset within the packet in the ring buffer, where the next shot
        // will start. 
        unsigned int shot_offset_within_adc_data_ = 0;

        /*
        // Append the value to the array of values of the given channel in the buffer, and if the buffer is full,
        // automatically write it to the corresponding file, and empty the buffer
        inline void store_channel_data_(unsigned int channel_number, data_type value)
            {
                channel_data_[channel_number][channel_data_size_[channel_number]++] = value;
                if(channel_data_size_[channel_number] == channel_data_capacity_) flush(channel_number);
            }
        */
        
        // Get the channel value from the byte array pointed to by 'ptr', which is the first (potentially incomplete) byte
        // of the encoded channel value. 
        apdcam10g::data_type store_channel_value_(const apdcam10g::byte *ptr, ring_buffer<data_type,channel_info> *c);

    public:
        // Constructor
        // adc -- ADC number (0..3)
        // ver -- version (from version.h)
        // sample_buffer_size -- buffer capacity for the channel samples, before writing to file
        channel_data_extractor(daq *d, version ver, unsigned int adc);

        ~channel_data_extractor();

        // Option/property setter functions - they return self-reference so that we can chain like this:
        // channel_data_extractor e;
        // e.adc(0).debug(true);

        channel_data_extractor &adc(unsigned int adc_board_number) { adc_ = adc_board_number;  return *this; }

        channel_data_extractor & debug(bool d) { debug_ = d; return *this; }

        // Extract the channel data from the available packets in the network ring buffer,
        // and remove those packets from the ring buffer which have been processed.
        // This function blocks the calling thread until new packets arrive.
        // Returns the number of removed packets, or a negative number if the input is terminated
        int run(udp_packet_buffer<S> &network_buffer, 
                 const std::vector<ring_buffer<apdcam10g::data_type,channel_info>*> &channel_data_buffers);
    };

    

}


#include "daq.h"

#ifdef CLASS_DAQ_DEFINED
namespace apdcam10g
{
    template <safeness S>
    inline apdcam10g::data_type channel_data_extractor<S>::store_channel_value_(const apdcam10g::byte *ptr, ring_buffer<data_type,channel_info> *c)
    {
        data_type value;
        switch(c->nbytes)
        {
            // For 1 and 2 bytes we fit into data_type. However, if the value reaches over 3 bytes, we need to use a larger integer to represent it
            // before shifting down to the least significant bit.
        case 2: value = ((data_type(ptr[0])<<8 | data_type(ptr[1])) >> c->shift) & make_mask<data_type>(daq_->resolution_bits(adc_),0); break;
        case 3: value = data_type( (data_envelope_type(ptr[0])<<16 | data_envelope_type(ptr[1])<<8 | data_envelope_type(ptr[2])) >> c->shift) & make_mask<data_type>(daq_->resolution_bits(adc_),0); break;
        case 1: value = (data_type(ptr[0])>>c->shift) & make_mask<data_type>(daq_->resolution_bits(adc_),0); break;
        default: APDCAM_ERROR("Bug! Number of bytes should be 1, 2 or 3");
        }
        c->push(value);
        return value;
    }
}
#endif


#endif

