#ifndef __APDCAM10G_DAQ_H__
#define __APDCAM10G_DAQ_H__

#include <tuple>
#include <string>
#include <vector>
#include <cstddef>
#include <thread>
#include <fstream>
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
#include "processor.h"
#include "utils.h"

namespace apdcam10g
{
    // The 'daq' class is the topmost element in the DAQ system. It starts "network threads"
    // for each input socket which  dump the UDP packets into ring-buffers associated with each socket.
    // It starts "extraction threads" for each socket to extract the data packed into and split among
    // subsequent UDP packets, and dump this data into ring buffers associated with each channel.
    // It starts a data processor thread that runs a sequence of processor tasks which can be
    // defined by the user. And finally, it starts a command interpreter thread which reads input from
    // the FIFO ~/.apdcam10g/cmd in order to provide a very simple control interface for external programs.

    class daq : public daq_settings
    {
        friend class processor_diskdump;

    private:

        // An atomic flag to indicate whether the python task can run (or is running). Logically it should be a
        // static variable in the scope of processor_python, but on the python side, we will load (ctypes.CDLL)
        // all functions from  the shared library in the 'daq' "scope", so let's do the same here as well. Also,
        // since class 'daq' is a singleton with an on-demand created instance, we can use a simple data member, 
        // no need to worry about global data initialization order, etc
        std::atomic_flag python_analysis_run_;

        // A flag to signal the python processor loop (if there are any python processors added)
        // that the data flow has terminated (nor more data is going to be processed).
        // The python loop is not waiting on this, so it is necessary to wake
        // it up by setting the python_analysis_run_ flag
        std::atomic_flag python_analysis_stop_;

        // a variable into which each python processor's earliers data counter is written,
        // which is required to stay in the buffer/memory
        size_t python_analysis_needs_data_from_ = 0;

        // variables to communicate the available data range to the python processors
        size_t python_analysis_data_available_from_ = 0;
        size_t python_analysis_data_available_to_ = 0;

        // A flag to control debugging output
        bool debug_ = false;

        // Self-explanatory flag
        bool dual_sata_ = false;

        // Firmware version of the camera hardware
        version fw_version_ = version::v1;

        // The period (number of shots) for calling the processor tasks on the channel data. The default 128 means that once there are
        // 128 new shots in the buffer, all processor tasks are triggered and run.
        // It must be a power of 2
        unsigned int process_period_ = 128;

        // The input network sockets to read data from
        std::vector<udp_server>  sockets_;
      
        // A set of buffers to store received UDP packets, transparently handling packet loss
        // (by replacing lost packets with zero-filled packets)
        std::vector<apdcam10g::udp_packet_buffer<default_safeness>*> network_buffers_;

        // In order to store channel info (which ADC board this channel belongs to, channel number, chip number etc),
        // the 3rd template argument of ring_buffer is channel_info so that we derive from channel_info 
        typedef ring_buffer<apdcam10g::data_type,channel_info> channel_data_buffer_t;

        // a vector of nof_adc*channels_per_board buffer pointers. That is, we have a slot for each hardware
        // channel, even if it is not enabled, and will receive no data.
        // Non-enabled channels will be  associated with a null pointer. Rationale: python analysis tasks
        // do not need to 
        // However, no room is allocated for missing entire ADC boards since these would be 0 pointers
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
        std::atomic_flag           network_threads_active_[config::max_boards]; // flags which indicate whether the given threads are active
        std::vector<std::jthread>  extractor_threads_;  // extract the signals of the individual channels and store them in a per-channel ring buffer
        std::atomic_flag           extractor_threads_active_[config::max_boards]; 
        std::jthread               processor_thread_;   // analyze the signals (search for some signature, write to disk, whatever else)
        std::atomic_flag           processor_thread_active_;
        std::jthread               command_thread_;
        std::atomic_flag           command_thread_active_;

        // A vector of channel data extractors, one for each ADC board
        std::vector<apdcam10g::channel_data_extractor<default_safeness>*> extractors_;  

        // A vector of different channel data processors, doing different tasks. That is, each element of this vector is doing a different
        // analysis task on ALL channels of ALL ADC boards
        std::vector<processor *> processors_;

        unsigned int network_buffer_size_ = 1<<10;    // The size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        unsigned int channel_buffer_size_ = 1<<18;   // The number of channel signal values stored in memory before dumping to disk
        unsigned int channel_buffer_extra_size_ = 1<<8; // Extra size at the end of the sample buffers to flatten a flipped-back data range

        std::filesystem::path cmd_fifo_name_ = configdir() / "cmd";

        // Being a singleton, the constructor is private and the only instance can be accessed
        // via the static daq::instance() function
        daq();

    public:

        unsigned int n_adc() const { return network_buffers_.size(); }
        unsigned int n_channels() const { return all_channels_buffers_.size(); }

        // A function to query the status of the python_analysis_stop_ flag. It will be called from the python
        // code within the processor loop to terminate if this is true
        bool python_analysis_stop();

        // Setting the phthon_analysis_stop_ flag (from C++) to stop the python analysis loop. 
        void python_analysis_stop(bool b);

        // To be called from the C++ code. Wait for the python analysis to finish. Block the calling thread until then.
        // It returns the 'need_data_from' value that the python analysis task communicated to the C++ code
        size_t python_analysis_wait_finish();

        // To be called from the python code. Block until we are allowed to run because
        // there is new data in the buffer
        // Return the available data range in the two arguments
        void python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter);

        // To be called from C++
        // Set the available data range such that the python task can query it (via the 
        // above 'python_analysis_wait_for_data function') and
        // set the flag which signals the python thread to run the analysis task
        void python_analysis_start(size_t from_counter, size_t to_counter); 

        // To be called from python. Clear the flag to signal the C++ backend that the analysis
        // task is done. Also, communicate from which shot the python analysis requires data to
        // remain in the buffer
        void python_analysis_done(size_t need_data_from);

        // Accessing the singleton instance
        static daq &instance();

        ~daq()
        {
            for(auto p : extractors_) delete p;
            for(auto p : all_enabled_channels_buffers_) delete p;
            unlink((configdir() / "pid").c_str());
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

        daq &clear_processors();

        daq &add_processor(processor *p) 
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

        // Pause all diskdump processors (they will process the data but will not write it to disk)
        void diskdump_pause();
        
        // Resume all diskdump processors (writing to disk continues)
        void diskdump_resume();

        // Set a sampling number for all diskdump processors. That is, only shot numbers being an integer multiple
        // of 's' will be written to disk (to save space). Currently it writes no markers in the file so the user
        // can not reconstruct the shot numbers from the file. This should be further developed. 
        void diskdump_sampling(unsigned int s);

        // Set the process period (the number of shots to trigger the processor tasks to run)
        // Must be called before init(...), the value must be a power of 2
        daq &process_period(unsigned int p);
      
        // Set the buffer size, the number of channel signal values (for each ADC separately) buffered in memory before dumping them to disk
        // Must be called before init(...)
        daq &channel_buffer_size(unsigned int b) { channel_buffer_size_ = b; return *this; }
        unsigned int channel_buffer_size() const { return channel_buffer_size_; }
        daq &channel_buffer_extra_size(unsigned int e) { channel_buffer_extra_size_ = e; return *this; }
        unsigned int channel_buffer_extra_size() const { return channel_buffer_extra_size_; }

        // Set the size of the network input ring buffer size in terms of UDP packets (real mamory is MTU*this_value measured in bytes)
        // Must be called before init(...)
        daq &network_buffer_size(unsigned int b) { network_buffer_size_ = b; return *this; }
        unsigned int network_buffer_size() const { return network_buffer_size_; }

        void start_cmd_thread();
        void stop_cmd_thread();

        // The specified safeness is transmitted to the signal extractor
        template <safeness S=default_safeness>
        daq &init();

        // Start the data processing with a given safeness. The specified safeness is transmitted to socket recv
        template <safeness S=default_safeness>
        daq &start(bool wait=false);

        // Stops the DAQ process by gently interrupting the network input threads. These will then raise the 'terminated' flag in
        // the network buffers, the data extractor threads will thereby be notified and stop, but they will also raise the
        // 'terminated' flags in the channel data buffers, causing finally the processor thread to also terminate. So a soft
        // stop in the network reader threads will propagate through all threads, and stop them.
        // If the timeout argument is larger than zero, this function will block until all threads are finihsed, but latest
        // the given time in seconds, and then gracelessly terminate all worker threads
        template <safeness S=default_safeness>
        daq &stop(unsigned int timeout_sec=0);

        // Gracelessly kill the DAQ threads, sending them the KILL signal.
        // Normally it should not be used, since 'stop' can do it gently
        daq &kill();

        // Wait for all threads to finish, join them
        daq &wait_finish();

        void dump();

        // Returns the number of received packets of the given stream (0-based)
        size_t received_packets(unsigned int i_stream) const;

        // Returns the number of lost packets of the given stream
        size_t lost_packets(unsigned int i_stream) const;

        bool network_thread_active(unsigned int i_stream) const;
        bool extractor_thread_active(unsigned int i_stream) const;
        bool processor_thread_active() const;

        unsigned int network_threads() const;
        unsigned int extractor_threads() const;
        unsigned int processor_threads() const;

        size_t network_buffer_content(unsigned int i_stream) const;
        size_t max_network_buffer_content(unsigned int i_stream) const;
        
        size_t channel_buffer_content(unsigned int i_channel) const;
        size_t max_channel_buffer_content(unsigned int i_channel) const;

        // Returns the actual content of the buffer of the last chnanel of the given board
        // (other channels have the same number of entries in their buffer, or one more)
        size_t channel_buffer_content_of_board(unsigned int i_adc) const;

        // Returns the maximum content of the buffer of the last channel of the given board
        // since the start of the dadta acqvisition
        size_t max_channel_buffer_content_of_board(unsigned int i_adc) const;

        size_t extracted_shots(unsigned int i_channel) const;
        size_t extracted_shots_of_board(unsigned int i_adc) const;
    };

#define CLASS_DAQ_DEFINED


}


// -----------  Python-interface functions ----------------------------
#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;
    void get_net_parameters();

    unsigned int n_adc();
    unsigned int n_channels();

    void start(bool wait=false);
    void stop(bool wait=true);
    void kill_all();  // both 'kill' and 'abort' would conflict with existing standard C library functions
    void version(apdcam10g::version v);
    void dual_sata(bool d);
    void channel_masks(bool **m, int n_adc_boards);
    void resolution_bits(unsigned int *r, int n_adc_boards);
    void add_processor_diskdump();
    void add_processor_python();
    void debug(bool d);
    void init(bool safe);
    void write_settings(const char *filename);
    void wait_finish();        
    void dump();
    void test();
    void clear_processors();

    // Set the network buffer and sample buffer sizes (capacities). It does not take immediate effect, only
    // when daq::init is called
    void network_buffer_size(unsigned int bufsize);
    void channel_buffer_size(unsigned int bufsize); 

    unsigned int get_network_buffer_size();
    unsigned int get_channel_buffer_size();

    unsigned int get_mtu();
    unsigned int get_octet();

    size_t received_packets(unsigned int i_stream);
    size_t lost_packets(unsigned int i_stream);

    size_t network_buffer_content(unsigned int i_stream);
    size_t max_network_buffer_content(unsigned int i_stream);
    size_t channel_buffer_content(unsigned int i_channel);
    size_t max_channel_buffer_content(unsigned int i_channel);
    size_t channel_buffer_content_of_board(unsigned int i_stream);
    size_t max_channel_buffer_content_of_board(unsigned int i_stream);

    // Returns the number of extracted shots for the given channel (absolute channel number)
    size_t extracted_shots(unsigned int i_channel);

    // REturns hte number of extracted shots for the last enabled channel of the
    // given board (previous channels of the same board have at least this many
    // shots extracted)
    size_t extracted_shots_of_board(unsigned int i_stream);

    unsigned int network_threads();
    unsigned int extractor_threads();
    unsigned int processor_threads();

    bool network_thread_active(unsigned int i_stream);
    bool extractor_thread_active(unsigned int i_stream);
    bool processor_thread_active();

    void diskdump_sampling(unsigned int s);

    void start_cmd_thread();
    void stop_cmd_therad();

    // Return the number of acquired UDP packets and extracted shots
//    void statistics(unsigned int *n_packets, unsigned int *n_shots);

    // Query the status of the DAQ process, in particular the number of active network, extractor and processor threads
//    void status(unsigned int *n_active_network_threads, unsigned int *n_active_extractor_threads, unsigned int *n_active_processor_thread);


    // Return the channel #absolute_chnanel_number data's ring buffer's size and memory buffer
    // in the 2nd and 3rd argument
    void get_buffer(unsigned int absolute_channel_number, unsigned int *buffersize, apdcam10g::data_type **buffer);

    // To be called from a python analysis task: block the calling thread until data for the next python
    // analysis task becomes available
    void python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter);

    // From a python analysis task: set/communicate the earliest data counter to the C++ DAQ backend
    // that the task still requires to stay in the ring buffers, and set a flag to inform the C++
    // DAQ backend that the python task has finished analyzing the data
    void python_analysis_done(size_t from_counter);

    bool python_analysis_stop();


}



#endif

#endif

