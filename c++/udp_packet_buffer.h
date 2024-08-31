#ifndef __APDCAM10G_UDP_PACKET_BUFFER_H__
#define __APDCAM10G_UDP_PACKET_BUFFER_H__

#include "safeness.h"
#include "ring_buffer.h"
#include "udp.h"
#include <condition_variable>
#include <mutex>

namespace apdcam10g
{
    struct udp_packet_record 
    {
        std::byte *address;
        unsigned int size;
    };


    template <safeness S=default_safeness>
    class udp_packet_buffer : ring_buffer<udp_packet_record>
    {
    private:
        // A raw buffer
        std::byte   *raw_buffer_ = 0;

        unsigned int size_in_packets_ = 0;
        unsigned int max_udp_packet_size_ = 0;
        
        // Counter of the UDP package expected during the next receive
        unsigned int expected_packet_counter_ = 0;
        
    public:
        udp_packet_buffer() {}
        udp_packet_buffer(unsigned int size_in_packets, unsigned int max_udp_packet_size)
            {
                resize(size_in_packets, max_udp_packet_size);
            }
        
        ~udp_packet_buffer()
        {
            if(raw_buffer_) delete [] raw_buffer_;
        }

        void resize(unsigned int size_in_packets, unsigned int max_udp_packet_size)
        {
	    size_in_packets_ = size_in_packets;
            max_udp_packet_size_ = max_udp_packet_size;
            if(raw_buffer_) delete [] raw_buffer_;
            raw_buffer_ = new std::byte[size_in_packets*max_udp_packet_size];

            ring_buffer<udp_packet_record>::resize(size_in_packets);

            // Store the pointers to 'max_udp_packet_size' chunks within the raw buffer into
            // the ring_buffer. 
            for(unsigned int i=0; i<size_in_packets_; ++i)
            {
                future_element(i) = { raw_buffer_ + i*max_udp_packet_size_, 0 };
            }
        }

        // Receive a UDP packet from the server. Detect missing packets (by the packet_counter field of the CC header),
        // and if packets are lost, substitute them with dummy packets containing all zeros.
        // This function should not be called simultaneously from concurrent threads!
        unsigned int receive(udp_server &s);

        // Add an empty packet (filled with zeroes) to the buffer, with the specified counter
        void add_empty_packet(std::byte *address, unsigned int counter);

    };
}

#endif
