#include "udp_packet_buffer.h"

namespace apdcam10g
{
    template <safeness S>
    unsigned int udp_packet_buffer<S>::receive(udp_server &s)
    {
        // First wait until we have room to receive a new packet
        // A single thread should write into this buffer, so we do not make locks around {detecting non-full state AND
        // writing a new packet into it}.
        packets.wait_pop([this]{!packets.full();});

        // Note that if several threads could write to this buffer concurrently, it could happen that after the previous
        // condition, i.e. waiting until space is available in the buffer, another thread could write to the buffer
        // at the time of this comment, and make it full, causing a problem for the subsequent writes.
        // If this was needed, we would need a mutex protecting the entire code of this function

        // Receive a packet into the raw buffer
	std::byte * const ptr = raw_buffer_+raw_buffer_write_offset_;
        const auto received_packet_size = s.recv<S>(ptr,max_udp_packet_size_);

        // Check if no package was lost...  (use packet.h services, not this hard-coded solution!)
        byte_converter<6,std::endian::big> packet_counter(ptr+8);
        if(packet_counter < expected_packet_counter_) APDCAM_ERROR("Packet counter has decreased");

	increment_raw_buffer_write_offset_();

        if(packet_counter == expected_packet_counter_)
        {
            // Push the new memory block's starting address to the ring buffer. The ring buffer offers
            // a notification service, so consumer threads can wait using packets.wait_pop(...) 
            packets.push_back({.address=ptr, .size=received_packet_size});
        }
	else // packet loss
	{
	  // First, create zero-filled packets in the raw buffer (physically AFTER the received packet,
	  // although the time order should have been the opposite), and add them to the ring buffer
	  const unsigned int lost_packets = packet_counter - expected_packet_counter_;
	  for(unsigned int i=0; i<lost_packets; ++i)
	    {
	      // Wait until we have room to store a new packet
	      packets.wait_pop([this]{!packets.full();});
	      // Then create a new zero-filled packet in the raw buffer, and append it to the ring buffer
	      add_empty_packet(expected_packet_counter_+i);
	    }

	  // And only then (to keep time-order), push the received packet to the end of the ring buffer
	  packets.push_back({.address=ptr, .size=received_packet_size});

	}
	expected_packet_counter_ = packet_counter+1;
	return received_packet_size;
    }

    template <safeness S>
    void udp_packet_buffer<S>::add_empty_packet(unsigned int counter)
    {
      std::byte * const ptr = raw_buffer_ + raw_buffer_write_offset_;

      // Fill the next block with zeros
      memset(ptr,0,max_udp_packet_size_);

      // Set the packet counter in the header to the specified value
      // WARNING, we should set other header data as well!
      byte_converter<6,std::endian::big> packet_counter(ptr+8);
      packet_counter = counter;

      // Store this block of memory in the ring buffer
      packets.push_back({.address = ptr, .size=max_udp_packet_size_});

      increment_raw_buffer_write_offset_();
    }
}
