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
        // Get the pointer within the raw buffer, where the data should be received
        std::byte* const ptr = record_ptr->address;

        // Receive the packet from the UDP socket
        const auto received_packet_size = s.recv<S>(ptr,max_udp_packet_size_);

        // If an error occurs (normally: timeout, the camera stops sending), set the 'terminated' flag
        // to inform the consumer that no more data is going to be received, and return
        if(received_packet_size<0)
        {
            terminate();
            return received_packet_size;
        }

        record_ptr->size = received_packet_size;

        // Before the first packet we have not set any timeout: we want the DAQ to wait long before
        // the camera starts sending data. But once it started, it will send data non-stop. It does not
        // send a stop signal, just stops sending. So let's set a timeout: If the camera does not send
        // data for 3 seconds, we will timeout
        // Call it only if we have been called the first time
        if(expected_packet_counter_==0) s.timeout(3);

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
	      add_empty_packet_(empty_record_ptr->address,expected_packet_counter_+i);
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
    void udp_packet_buffer<S>::add_empty_packet_(std::byte *address,unsigned int counter)
    {
      // Fill the next block with zeros
      memset(address,0,max_udp_packet_size_);

      // Set the packet counter in the header to the specified value
      // WARNING, we should set other header data as well!
      byte_converter<6,std::endian::big> packet_counter(address+8);
      packet_counter = counter;
    }

    template unsigned int udp_packet_buffer<apdcam10g::safe>::receive(udp_server &);
    template unsigned int udp_packet_buffer<apdcam10g::unsafe>::receive(udp_server &);

}
