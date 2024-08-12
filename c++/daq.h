#ifndef __APDCAM10G_DAQ_H__
#define __APDCAM10G_DAQ_H__

#include <tuple>
#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <thread>
#include <condition_variable>
#include "ring_buffer.h"
#include "udp_packet_buffer.h"
#include "packet.h"
#include "channel_info.h"
#include "typedefs.h"
#include "udp.h"
#include "safeness.h"
#include "daq_settings.h"
#include "channel_data_extractor.h"
#include "channel_data_processor.h"

namespace apdcam10g
{

    // The 'daq' class handles all sockets used for data transfer between the camera and the PC
    class daq : public daq_settings
    {
    private:
        // The period (number of shots) for calling the processor tasks on the channel data. The default 100 means that once there are
        // 100 new shots in the buffer, all processor tasks are triggered and run.
        unsigned int process_period_ = 100;

        bool separate_network_threads_ = true;
        bool separate_extractor_threads_ = false;
      
        std::vector<udp_server>  sockets_;
      
        // A set of buffers to store received UDP packets, transparently handling packet loss
        // (by replacing lost packets with zero-filled packets)
        std::vector<udp_packet_buffer> network_buffers_;
  
        // vector of vector of ring buffers to store data from individual channels
        // First index: adc board (0..3max, but check actual max value)
        // second index: a running index over the ENABLED channels (0..32max)
        // In order to store channel info (which ADC board this channel belongs to, channel number, chip number etc),
        // the 3rd template argument of ring_buffer is channel_info so that we derive from channel_info 
        std::vector<std::vector<ring_buffer<apdcam10g::data_type,apdcam10g::default_safeness,channel_info>>> channel_data_buffers_;

        // A map to index (access) the channel data buffers by absolute channel number. The key is the absolute channel number (0..127max)
        std::map<int,ring_buffer<apdcam10g::data_type>*> channel_data_buffers_map_;

        std::vector<std::jthread>  network_threads_;    // read the UDP packets and produce data in the ring buffer
        std::vector<std::jthread>  extractor_threads_;  // extract the signals of the individual channels and store them in a per-channel ring buffer
        std::jthread               processor_thread_;   // analyze the signals (search for some signature, write to disk, whatever else)
  
        // A vector of channel data extractors, one for each ADC board
        std::vector<channel_data_extractor *> extractors_;  

        // A vector of different channel data processors, doing different tasks (i.e. this vector is not per-channel!)
        std::vector<channel_data_processor *> processors_;

        unsigned int network_buffer_size_ = 500;    // The size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        unsigned int sample_buffer_size_ = 1000000; // The number of channel signal values stored in memory before dumping to disk

    public:

        void flush();

        // Set the process period (the number of shots to trigger the processor tasks to run)
        daq &process_period(unsigned int p) { process_period = p; return *this; }
      
        // Set the buffer size, the number of channel signal values (for each ADC separately) buffered in memory before dumping them to disk
        daq &sample_buffer_size(unsigned int b) { sample_buffer_size_ = b; return *this; }
        unsigned int sample_buffer_size() const { return sample_buffer_size_; }

        // Set the size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        daq &network_buffer_size(unsigned int b) { network_buffer_size_ = b; return *this; }
        unsigned int network_buffer_size() const { return network_buffer_size_; }

        // Set whether the data reception (over the sockets into the ring buffers) and data processing (from the ring buffers towards whatever data consumer)
        // should be done by separate threads per ADC, or single thread.
        daq &separate_threads(bool network, bool extractor) { separate_network_threads_ = network; separate_extractor_threads_ = extractor; return *this; }


        // Set the MAC address in the form of a string: 6 hex numbers (without the 0x prefix) separated by colon
        //        daq &mac(const std::string &m);
        //        daq &mac(std::byte m[6]) { for(int i=0; i<6; ++i) mac_[i] = m[i]; return *this; }
        //        const std::byte *mac() const { return mac_; }

        // Set the IP address in the form of a string: 4 numbers separated by dots
        //        daq &ip(const std::string &a);
        //        daq &ip(std::byte a[4]) { for(int i=0; i<4; ++i) ip_[i] = a[i]; return *this; }
        //        const std::byte *ip() const { return ip_; }

        // The specified safeness is transmitted to the signal extractor
        template <safeness S=default_safeness>
        daq &init (bool dual_sata, const std::vector<std::vector<std::vector<bool>>> &channel_masks, const std::vector<unsigned int> &resolution_bits, version ver);

        // Start the data processing with a given safeness. The specified safeness is transmitted to socket recv
        template <safeness S=default_safeness>
        daq &start(bool wait=false);

        // Request the data producer (reading from udp sockets into the ring buffers) and data consumer (reading from ring buffers and
        // dumping to disk) threads to stop.
        template <safeness S=default_safeness>
        daq &stop(bool wait=true); 

        void dump();
    };
}


// Python-interface functions
#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;
    daq          *create();
    void         destroy(daq *self);
    void         mtu(daq *self, int m);
    void         start(daq *self, bool wait=false);
    void         stop(daq *self, bool wait=true);
    void         initialize(daq *self, bool dual_sata, int n_adc_boards, bool ***channel_masks, unsigned int *resolution_bits, version ver, bool safe);
    void         dump(daq *self);
}
#endif

#endif

