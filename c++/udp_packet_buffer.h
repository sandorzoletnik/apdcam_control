#ifndef __APDCAM10G_UDP_PACKET_BUFFER_H__
#define __APDCAM10G_UDP_PACKET_BUFFER_H__

#include "safeness.h"
#include "ring_buffer.h"
#include <condition_variable>
#include <mutex>

namespace apdcam10g
{
    template <safeness S=default_safeness>
    class udp_packet_buffer
    {
    private:
        // A raw buffer
        std::byte   *raw_buffer_ = 0;

      unsigned int size_in_packets_ = 0;
        unsigned int max_udp_packet_size_ = 0;

        // Counter of the UDP package expected during the next receive
        unsigned int expected_packet_counter_ = 0;

      // Increment the write offset by the packet size, and reset to zero if running over the buffer's length
      void increment_raw_buffer_write_offset_()
      {
	raw_buffer_write_offset_ += max_udp_packet_size_;
	if(raw_buffer_write_offset_ >= size_in_packets_*max_udp_packet_size_) raw_buffer_write_offset_ = 0;
      }

    public:

        ~udp_packet_buffer()
        {
            if(raw_buffer_) delete [] raw_buffer_;
        }

        void resize(unsigned int size_in_packets, unsigned int max_udp_packet_size)
        {
            max_udp_packet_size_ = max_udp_packet_size;
	    size_in_packets_ = size_in_packets;
            if(raw_buffer_) delete [] raw_buffer_;
            raw_buffer_ = new std::byte[size_in_packets*max_udp_packet_size];
            packets.resize(size_in_packets);
        }

        struct packet_record 
        {
            std::byte *address;
            unsigned int size;
        };

        // A ring buffer of pointers, each pointing to the first byte of a UDP packet
        ring_buffer<packet_record> packets;

        // Receive a UDP packet from the server. Detect missing packets (by the packet_counter field of the CC header),
        // and if packets are lost, substitute them with dummy packets containing all zeros.
        // This function should not be called simultaneously from concurrent threads!
        unsigned int receive(udp_server &s);

      // Add an empty packet (filled with zeroes) to the buffer, with the specified counter
      void add_empty_packet(unsigned int counter);

    };
}

#endif
