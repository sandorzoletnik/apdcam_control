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
        apdcam10g::byte *address; // Memory address where the data is stored
        unsigned int size;        // Size of the packetin bytes
    };

    template <safeness S=default_safeness>
    class udp_packet_buffer : public ring_buffer<udp_packet_record>
    {
    private:
        // A raw buffer for the storage of the packets, of size integer times 'max_udp_packet_size_'
        apdcam10g::byte   *raw_buffer_ = 0;

        // Number of packets the buffer can store
        unsigned int size_in_packets_ = 0;

        // Maximum size of UDP packets (all packets but the last one are of this size)
        unsigned int max_udp_packet_size_ = 0;
        
        // Counter of the UDP packet expected during the next receive
        unsigned int expected_packet_counter_ = 0;
        
        // Add an empty packet (filled with zeroes) to the buffer, with the specified counter
        void add_empty_packet_(udp_packet_record *, unsigned int counter, unsigned int packet_size);

        unsigned int lost_packets_ = 0;
        unsigned int received_packets_ = 0;

    public:
        udp_packet_buffer() {}
        udp_packet_buffer(unsigned int size_in_packets, unsigned int max_udp_packet_size) 
        {
            resize(size_in_packets,max_udp_packet_size);
        }

        // Reset the statistics (max and average buffer fill size, and the number of lost/received packets)
        void reset_statistics() override
        {
            ring_buffer<udp_packet_record>::reset_statistics();
            received_packets_ = lost_packets_ = 0;
        }

        // Returns the number of lost packets since the last 'reset_statistics' call
        unsigned int lost_packets() const { return lost_packets_; }

        // Returns the number of received packets since the last 'reset_statistics' call
        unsigned int received_packets() const { return received_packets_; }

        // Resize the buffer's capacity, and reset the statistics counters
        void resize(unsigned int size_in_packets, unsigned int max_udp_packet_size);
        
        ~udp_packet_buffer()
        {
            if(raw_buffer_) delete [] raw_buffer_;
        }

        // Receive a UDP packet from the server. Detect missing packets (by the packet_counter field of the CC header),
        // and if packets are lost, substitute them with dummy packets containing all zeros.
        // This function MUST NOT be called simultaneously from concurrent threads!
        // It returns the number of bytes received, or a negative number on error.
        unsigned int receive(udp_server &s,std::stop_token &stok);

    };
}

#endif
