#include "udp_packet_buffer.h"
#include "bytes.h"

namespace apdcam10g
{
    template <safeness S>
    void udp_packet_buffer<S>::resize(unsigned int size_in_packets, unsigned int max_udp_packet_size)
    {
        ring_buffer<udp_packet_record>::resize(size_in_packets);
        
        size_in_packets_ = size_in_packets;
        max_udp_packet_size_ = max_udp_packet_size;
        if(raw_buffer_) delete [] raw_buffer_;
        
        // Allocate the raw buffer. For each packet allocate 2 more bytes so that if there is a channel
        // value which spills over into a next packet (by max 2 bytes), we can copy it to the end of
        // the previous buffer
        raw_buffer_ = new apdcam10g::byte[size_in_packets*(max_udp_packet_size+2)];
        
        // Store the pointers to 'max_udp_packet_size' chunks within the raw buffer into
        // the ring_buffer. 
        for(unsigned int i=0; i<size_in_packets_; ++i)
        {
            if(auto p = future_element(i)) *p = { raw_buffer_ + i*(max_udp_packet_size_+2), 0 };
            else APDCAM_ERROR("This should not happen");
        }

        reset_statistics();
    }


    template <safeness S>
    unsigned int udp_packet_buffer<S>::receive(udp_server &s,std::stop_token &stok)
    {
        // Spin-locked wait to obtain a slot for a free record
        udp_packet_record *record_ptr;
        while((record_ptr=future_element(0))==0 && !stok.stop_requested());

        // If the actual thread running this code was requested to stop,
        // then simply return, and set the 'terminated' flag to true so that consumers can detect
        // this and quit too.
        if(stok.stop_requested())
        {
            terminate();
            return 0;
        }

        // Get the pointer within the raw buffer, where the data should be received
        apdcam10g::byte* const ptr = record_ptr->address;

        // Receive the packet from the UDP socket into this memory location
        const auto received_packet_size = s.recv<S>(ptr,max_udp_packet_size_);

        // If an error occurs (normally: timeout, the camera stops sending), set the 'terminated' flag
        // to inform the consumer that no more data is going to be received, and return
        if(received_packet_size<0)
        {
            terminate();
            return received_packet_size;
        }

        ++received_packets_;

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

        // If we received the expected packet, everything is all right
        if(packet_counter == expected_packet_counter_)
        {
            // Advance the back of the ring buffer, thereby 'publishing' the new available data: one packet
            publish(1);
        }
	else // otherwise, packet loss...
	{
            // First, create zero-filled packets in the raw buffer (physically AFTER the received packet,
            // although the time order should have been the opposite), and add them to the ring buffer
            const unsigned int nof_lost_packets = packet_counter - expected_packet_counter_;
            // We have not yet 'published' the new packet, so we need to have nof_lost_packets+1 free space available

            // Keep track of how many packets have been lost in total (accumulative counter)
            lost_packets_ += nof_lost_packets;

            // Add nof_lost_packet empty packets to the buffer
            udp_packet_record *empty_record_ptr;
            for(unsigned int i=0; i<nof_lost_packets; ++i)
            {
                // Spin-lock wait for a new slot
                while((empty_record_ptr=future_element(i+1))==0 && !stok.stop_requested());

                if(stok.stop_requested())
                {
                    terminate();
                    return 0;
                }

                // Then create a new zero-filled packet in the raw buffer, and append it to the ring buffer
                add_empty_packet_(empty_record_ptr,expected_packet_counter_+i,max_udp_packet_size_);
            }
            
            // Now the empty packets are after the received one. Swap the received one with the last empty to
            // get the order right
            swap(*record_ptr,*empty_record_ptr);
            
            // And finally publish the new data to the consumers
            publish(nof_lost_packets+1);
	}
        
	expected_packet_counter_ = packet_counter+1;
	return received_packet_size;
    }

    template <safeness S>
    void udp_packet_buffer<S>::add_empty_packet_(udp_packet_record *record_ptr,unsigned int counter, unsigned int packet_size)
    {
        // Fill the next block with zeros
        memset(record_ptr->address,0,packet_size);
        record_ptr->size = packet_size;
        
        // Set the packet counter in the header to the specified value
        // WARNING, we should set other header data as well!
        byte_converter<6,std::endian::big> packet_counter(record_ptr->address+8);
        packet_counter = counter;
    }

    template unsigned int udp_packet_buffer<apdcam10g::safe>::receive(udp_server &,std::stop_token &);
    template unsigned int udp_packet_buffer<apdcam10g::unsafe>::receive(udp_server &,std::stop_token &);
    template void udp_packet_buffer<apdcam10g::safe>::resize(unsigned int, unsigned int);
    template void udp_packet_buffer<apdcam10g::unsafe>::resize(unsigned int, unsigned int);

}
