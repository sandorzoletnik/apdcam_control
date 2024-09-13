#ifndef __APDCAM10G_CHANNEL_INFO_H__
#define __APDCAM10G_CHANNEL_INFO_H__

#include "typedefs.h"
#include "bytes.h"

namespace apdcam10g
{

    /*
      This class stores information about how the channel data is encoded in the byte stream of a given ADC board,
      taking into account which channels are enabled, and what is the bit-resolution
     */

    struct channel_info
    {
        // The number (0..3) of the ADC board of this channel
        unsigned int board_number;

        // The number (0..3) of the chip within the ADC board
        unsigned int chip_number;

        // The channel number (0..31 inclusive) within the ADC board
        unsigned int channel_number;

        // The absolute channel number (0..127 inclusive)
        unsigned int absolute_channel_number;

        // An index running from 0 over the enabled channels. 
        unsigned int enabled_channel_number;

        // The offset of the first byte (full or partial) of this channel w.r.t. the ADC board's data, 
        // i.e. the first byte of the first (enabled) channel of the first chip of a given shot.
        unsigned int byte_offset;
        
        // The number of bytes that this value is extending over. Possible values are
        // 1 - if resolution is 8-bit
        // 2
        // 3 - if resolution is 12-bit
        unsigned int nbytes;

        // The number of bits
        unsigned int nbits;

        // The amount of bitwise right-shift (i.e. towards least significant bit)
        unsigned int shift;

        data_type get_from_shot(const apdcam10g::byte *shot_buffer)
            {
                const apdcam10g::byte * const ptr = shot_buffer + byte_offset;
                switch(nbytes)
                {
                    // For 1 and 2 bytes we fit into data_type. However, if the value reaches over 3 bytes, we need to use a larger integer to represent it
                    // before shifting down to the least significant bit.
                case 2: return ((data_type(ptr[0])<<8 | data_type(ptr[1])) >> shift) & make_mask<data_type>(nbits,0); 
                case 3: return data_type( (data_envelope_type(ptr[0])<<16 | data_envelope_type(ptr[1])<<8 | data_envelope_type(ptr[2])) >> shift) & make_mask<data_type>(nbits,0); 
                case 1: return(data_type(ptr[0])>>shift) & make_mask<data_type>(nbits,0); 
                default: APDCAM_ERROR("Bug! Number of bytes should be 1, 2 or 3");
                }
            }

        void set_in_shot(apdcam10g::byte *shot_buffer, data_type value)
            {
                // Create an envelope-type value (i.e. that type which can hold the value even after bit-shifting),
                // and shift it
                data_envelope_type val = data_envelope_type(value) << shift;
                
                // Pointer to the right-most (least significant) byte
                apdcam10g::byte *ptr = shot_buffer + byte_offset + nbytes - 1;

                int remaining_bits = nbits;
                int actual_shift = shift;

                // We iterate from right to left, i.e. from the least significant byte to the most significant one
                for(int i_byte = nbytes-1; i_byte >= 0; --i_byte)
                {
                    // The number of bits that woudl fit into the current byte
                    const int actual_bits = std::min(8-actual_shift,remaining_bits);
                    const auto mask = make_mask<apdcam10g::byte>(actual_bits,actual_shift);
                    shot_buffer[byte_offset+i_byte] = ( shot_buffer[byte_offset+i_byte] & ~mask ) | ( (apdcam10g::byte)val & mask );
                    actual_shift = 0;
                    remaining_bits -= actual_bits;
                    // We have processed one byte from the value, so shift it out
                    val >>= 8;
                }
            }

    };
}

#endif
