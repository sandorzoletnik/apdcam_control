#ifndef __APDCAM10G_DAQ_H__
#define __APDCAM10G_DAQ_H__

#include <tuple>
#include <string>
#include <vector>
#include <cstddef>
#include <thread>
#include <condition_variable>
#include "ring_buffer.h"
#include "udp_packet_buffer.h"
#include "packet.h"
#include "channel_info.h"
#include "typedefs.h"
#include "udp.h"
#include "udp_packet_buffer.h"
#include "safeness.h"
#include "daq_settings.h"
#include "channel_data_extractor.h"
#include "channel_data_processor.h"
#include "utils.h"

namespace apdcam10g
{
    //template <safeness S = default_safeness> class channel_data_extractor;

    // The 'daq' class handles all sockets used for data transfer between the camera and the PC
    class daq : public daq_settings
    {
        friend class channel_data_diskdump;

    private:
        bool debug_ = false;

        bool dual_sata_ = false;

        version fw_version_ = version::v1;

        // The period (number of shots) for calling the processor tasks on the channel data. The default 100 means that once there are
        // 100 new shots in the buffer, all processor tasks are triggered and run.
        // It must be a power of 2
        unsigned int process_period_ = 128;

        std::vector<udp_server>  sockets_;
      
        // A set of buffers to store received UDP packets, transparently handling packet loss
        // (by replacing lost packets with zero-filled packets)
        std::vector<apdcam10g::udp_packet_buffer<default_safeness>*> network_buffers_;
  
        // In order to store channel info (which ADC board this channel belongs to, channel number, chip number etc),
        // the 3rd template argument of ring_buffer is channel_info so that we derive from channel_info 
    public:
        typedef ring_buffer<apdcam10g::data_type,channel_info> channel_data_buffer_t;
    private:
        // a vector of nof_adc*channels_per_board buffer pointers. Non-enabled channels will be
        // associated with a null pointer. No room is allocated for missing entire ADC boards
        // since these would be 0 pointers
        std::vector<channel_data_buffer_t*> all_channels_buffers_;

        // Flattened vector of the pointers to buffers for all enabled channels from all boards
        std::vector<channel_data_buffer_t*> all_enabled_channels_buffers_;

        // Vector of vector of pointers to enabled channels. First index is adc board, second index is
        // a running index over the enabled channels of that board
        std::vector<std::vector<channel_data_buffer_t*>> board_enabled_channels_buffers_;

        // Vector of pointers to the buffers of the last enabled channel of the board. Index is board number
        std::vector<channel_data_buffer_t*> board_last_channel_buffers_;

//        std::vector<std::ranges::subrange<std::vector<channel_data_buffer_t>::iterator>> board_enabled_channels_buffers_;

        std::vector<std::jthread>  network_threads_;    // read the UDP packets and produce data in the ring buffer
        std::vector<std::jthread>  extractor_threads_;  // extract the signals of the individual channels and store them in a per-channel ring buffer
        std::jthread               processor_thread_;   // analyze the signals (search for some signature, write to disk, whatever else)
  
        // A vector of channel data extractors, one for each ADC board
        std::vector<apdcam10g::channel_data_extractor<default_safeness>*> extractors_;  

        // A vector of different channel data processors, doing different tasks. That is, each element of this vector is doing a different
        // analysis task on ALL channels of ALL ADC boards
        std::vector<channel_data_processor *> processors_;

        unsigned int network_buffer_size_ = 1<<10;    // The size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        unsigned int sample_buffer_size_ = 1<<18;   // The number of channel signal values stored in memory before dumping to disk
        unsigned int sample_buffer_extra_size_ = 1<<8; // Extra size at the end of the sample buffers to flatten a flipped-back data range


        // Being a singleton, the constructor is private and the only instance can be accessed
        // via the static daq::instance() function
        daq();

    public:

        // Accessing the singleton instance
        static daq &instance();

        ~daq()
        {
            for(auto p : extractors_) delete p;
            for(auto p : all_enabled_channels_buffers_) delete p;
        }

        channel_data_buffer_t *channel_buffer(unsigned int absolute_channel_number) 
        {
            if(absolute_channel_number>=all_channels_buffers_.size()) return 0;
            return all_channels_buffers_[absolute_channel_number];
        }

        void show_error(const std::string &msg, const std::string &location="")
            {
                output_lock lck;
                cerr<<terminal::red_fg<<"[ERROR] "  <<msg<<" ["<<location<<"]"<<terminal::reset<<endl;
            }
        void show_warning(const std::string &msg, const std::string &location="")
            {
                output_lock lck;
                cerr<<terminal::orange_fg<<"[WARNING] "<<msg<<" ["<<location<<"]"<<terminal::reset<<endl;
            }

        daq &dual_sata(bool d) { dual_sata_ = d; return *this; }
        bool dual_sata() const { return dual_sata_; }

        daq &fw_version(version v) { fw_version_ = v; return *this; }
        version fw_version() const { return fw_version_; }

        daq &clear_processors() { processors_.clear(); return *this; }
        daq &add_processor(channel_data_processor *p) 
            { 
                p->set_daq(this);
                processors_.push_back(p); 
                return *this ;
            }

        void finish();

        void debug(bool d) 
            {
                debug_ = d;
                for(auto e : extractors_) e->debug(d);
            }

        // Set the process period (the number of shots to trigger the processor tasks to run)
        // Must be called before init(...), the value must be a power of 2
        daq &process_period(unsigned int p);
      
        // Set the buffer size, the number of channel signal values (for each ADC separately) buffered in memory before dumping them to disk
        // Must be called before init(...)
        daq &sample_buffer_size(unsigned int b) { sample_buffer_size_ = b; return *this; }
        unsigned int sample_buffer_size() const { return sample_buffer_size_; }
        daq &sample_buffer_extra_size(unsigned int e) { sample_buffer_extra_size_ = e; return *this; }
        unsigned int sample_buffer_extra_size() const { return sample_buffer_extra_size_; }

        // Set the size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        // Must be called before init(...)
        daq &network_buffer_size(unsigned int b) { network_buffer_size_ = b; return *this; }
        unsigned int network_buffer_size() const { return network_buffer_size_; }

        // The specified safeness is transmitted to the signal extractor
        template <safeness S=default_safeness>
        daq &init();

        // Start the data processing with a given safeness. The specified safeness is transmitted to socket recv
        template <safeness S=default_safeness>
        daq &start(bool wait=false);

        // Request the data producer (reading from udp sockets into the ring buffers) and data consumer (reading from ring buffers and
        // dumping to disk) threads to stop.
        template <safeness S=default_safeness>
        daq &stop(bool wait=true); 

        // Wait for all threads to finish, join them
        void wait_finish();

        void dump();
    };

#define CLASS_DAQ_DEFINED

}


// Python-interface functions
#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;
    void get_net_parameters();
    //void         mtu(int m);
    void start(bool wait=false);
    void stop(bool wait=true);
    void version(apdcam10g::version v);
    void dual_sata(bool d);
    void channel_masks(bool **m, int n_adc_boards);
    void resolution_bits(unsigned int *r, int n_adc_boards);
    void add_processor_diskdump();
    void debug(bool d);
    void init(bool safe);
    void write_settings(const char *filename);
    void wait_finish();        
    void dump();

    // Return the channel #absolute_chnanel_number data's ring buffer's size and memory buffer
    // in the 2nd and 3rd argument
    void get_buffer(unsigned int absolute_channel_number, unsigned int *buffersize, apdcam10g::data_type **buffer);
}



#endif

#endif

