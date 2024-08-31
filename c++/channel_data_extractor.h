#ifndef __APDCAM10G_CHANNEL_DATA_EXTRACTOR_H__
#define __APDCAM10G_CHANNEL_DATA_EXTRACTOR_H__

#include "config.h"
#include "udp_packet_buffer.h"

/*

  A worker class derived from data_consumer, which consumes APDCAM data (UDP packets) from the ring buffer,
  and extracts the channel-by-channel data into linear buffers (per channel). Once these buffers are full,
  they are dumped to disk. The linear buffers are protected from swapping by mlock.

  One instance of this class handles one data stream at one port, i.e. one ADC
  
 */

#include "daq.h"

namespace apdcam10g
{
    template <safeness S>
    class channel_data_extractor 
    {
    private:
        
        // A pointer to a daq object to query packet size, number of bytes per ADC chip, per ADC board, etc
        daq *daq_ = 0;

        // The ADC number, the data of which this consumer will be processing
        unsigned int adc_ = 0;
        
        packet *packet_ = 0, *next_packet_ = 0;

        unsigned int packet_counter_ = 0;

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
        inline data_type get_channel_value_(std::byte *ptr, const channel_info &c)
            {
                switch(c.nbytes)
                {
                    // For 1 and 2 bytes we fit into data_type. However, if the value reaches over 3 bytes, we need to use a larger integer to represent it
                    // before shifting down to the least significant bit.
                case 2: return ((data_type(ptr[0])<<8 | data_type(ptr[1])) >> c.shift) & make_mask<data_type>(0,daq_->resolution_bits(adc_));
                case 3: return data_type( (data_envelope_type(ptr[0])<<16 | data_envelope_type(ptr[1])<<8 | data_envelope_type(ptr[2])) >> c.shift) & make_mask<data_type>(0,daq_->resolution_bits(adc_));
                case 1: return (data_type(ptr[0])>>c.shift) & make_mask<data_type>(0,daq_->resolution_bits(adc_));
                default: APDCAM_ERROR("Bug! Number of bytes should be 1, 2 or 3");
                }
                return 0;
            }
        
    public:
        // Constructor
        // adc -- ADC number (0..3)
        // ver -- version (from version.h)
        // sample_buffer_size -- buffer capacity for the channel samples, before writing to file
        channel_data_extractor(daq *d,unsigned int adc, version ver);

        ~channel_data_extractor();

        void init() {}

        // Extract the channel data from the available packets in the network ring buffer,
        // and remove those packets from the ring buffer which have been processed.
        // This function blocks the calling thread until new packets arrive
        unsigned int run(udp_packet_buffer<default_safeness> &network_buffer, std::vector<daq::channel_data_buffer_t> &channel_data_buffer);
    };
}


#endif

