#include "udp_packet_buffer.h"
#include "bytes.h"

namespace apdcam10g
{
    template <safeness S>
    unsigned int udp_packet_buffer<S>::receive(udp_server &s)
    {
        // Spin-locked wait to obtain a slot for a free record
        udp_packet_record *record_ptr;
        while((record_ptr=future_element(0))==0);
        std::byte* const ptr = record_ptr->address;
        
        const auto received_packet_size = s.recv<S>(ptr,max_udp_packet_size_);
        record_ptr->size = received_packet_size;

        // Check if no package was lost...  (use packet.h services, not this hard-coded solution!)
        byte_converter<6,std::endian::big> packet_counter(ptr+8);
        if(packet_counter < expected_packet_counter_) APDCAM_ERROR("Packet counter has decreased");

        if(packet_counter == expected_packet_counter_)
        {
            // Advance the back of the ring buffer, thereby 'publishing' the new available data
            publish(1);
        }
	else // packet loss
	{
	  // First, create zero-filled packets in the raw buffer (physically AFTER the received packet,
	  // although the time order should have been the opposite), and add them to the ring buffer
	  const unsigned int lost_packets = packet_counter - expected_packet_counter_;
          // We have not yet 'published' the new packet, so we need to have lost_packets+1 free space available

          udp_packet_record *empty_record_ptr;
	  for(unsigned int i=0; i<lost_packets; ++i)
          {
              // Spin-lock wait for a new slot
              while((empty_record_ptr=future_element(i+1))==0);

	      // Then create a new zero-filled packet in the raw buffer, and append it to the ring buffer
	      add_empty_packet(empty_record_ptr->address,expected_packet_counter_+i);
          }

          // Now the empty packets are after the received one. Swap the received one to the end
          swap(*record_ptr,*empty_record_ptr);

	  // And finally publish the new data to the consumers
          publish(lost_packets+1);
	}
	expected_packet_counter_ = packet_counter+1;
	return received_packet_size;
    }

    template <safeness S>
    void udp_packet_buffer<S>::add_empty_packet(std::byte *address,unsigned int counter)
    {
      // Fill the next block with zeros
      memset(address,0,max_udp_packet_size_);

      // Set the packet counter in the header to the specified value
      // WARNING, we should set other header data as well!
      byte_converter<6,std::endian::big> packet_counter(address+8);
      packet_counter = counter;
    }
}
