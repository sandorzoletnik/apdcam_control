#ifndef __APDCAM10G_DAQ_SETTINGS_H__
#define __APDCAM10G_DAQ_SETTINGS_H__



/*

  class daq_settings

  This class defines all settings which influence the data transfer between the camera and the PC, 
  providing read/write functions to load/save the settings from/to a file
  This way both the real data recording code and the fake camera can use the same settings.

 */

#include "channel_info.h"
#include "terminal.h"
#include "utils.h"
#include "lockable.h"
#include <vector>
#include <string>
#include <iostream>
#include <atomic>

namespace apdcam10g
{
    // Return true if the mask (vector of booleans) of a given ADC board has at least one true element
    bool has_enabled_channel(const std::vector<bool> &mask);

    template <typename CHINFO>
    class daq_settings
    {
    protected:
        lockable<std::string> interface_;
        const static int ipv4_header_ = 20;
        const static int udp_header_ = 8;
        std::atomic<unsigned int> mtu_ = 0;
        std::atomic<unsigned int> octet_ = 0;

        // The maximum UDP packet size, which is 22 bytes (streamheader) + 8*octet. At the end of a burst or a sequence
        // of transmitted shots, there may be a smaller UDP packet if the shots do not fill an entire one, but
        // all preceding packets will have this maximum size
        std::atomic<unsigned int> max_udp_packet_size_ = 0;

        // A lot of (redundant) information to be able to access and manipulate the memory storage
        // in different ways efficiently
        lockable<std::vector<std::vector<bool>>>              channel_masks_;           // Indices: ADC number, channel number within board. It is initialized for all channels being enabled
        lockable<std::vector<unsigned int>>                   resolution_bits_;         // Index: ADC number. Initialized as 14 for all boards

        // These data members below are not initialized by default. They are calculated by the 'calculate_channel_info' member function
        // which must be called before the daq is started
        lockable<std::vector<unsigned int>>                   board_bytes_per_shot_;    // index is ADC number
        lockable<std::vector<std::vector<unsigned int>>>      chip_bytes_per_shot_;     // indices are ADC number (0..3max) and chip nummber (0..3)
        lockable<std::vector<std::vector<unsigned int>>>      chip_offset_;             // Offset of the first data byte of the chip w.r.t. the board's first data byte, indices are ADC number and chip number

        lockable<std::vector<CHINFO*>>               all_channels_;           // A vector of all possible channels (all physical channels of all present ADC boards). Zero pointer is stored for disabled ones.
                                                                              // The vector index is the absolute channel number from 0 to 127
        lockable<std::vector<CHINFO*>>               all_enabled_channels_;   // A vector of all of the enabled channels. Vector index is a continuously running index, the 'enabled channel index'
        lockable<std::vector<std::vector<CHINFO*>>>  board_enabled_channels_; // An array of enabled channels grouped by ADC boards. First index is ADC board number, second index is the enabled channel index within
                                                                              // the board, i.e. from 0 to maximum 31 (depending on how many channels of the given board are enabled)
        lockable<std::vector<CHINFO*>>               board_last_enabled_channel_;  // The last enabled channel of each board. Vector index is the ADC board number. 

        // Set MTU
        daq_settings &mtu(unsigned int m);

        // A virtual function that creates a "channel info" class. It is overridden in the "daq" class to
        // create a ring buffer for the channels with a given size
        virtual CHINFO *create_channel_info() { return new CHINFO(); }

    public:

        void dump();

        // Initialize with all possible ADC boards being present, with all of their channels being
        // enabled, and all ADC boards' resolution being set to 14 bits
        daq_settings();

        ~daq_settings();

        unsigned int n_adc() const { std::shared_lock lck(board_enabled_channels_); return board_enabled_channels_.size(); }
        unsigned int n_channels() const { std::shared_lock lck(all_channels_); return all_channels_.size(); }

        // get the MTU value (Maximum Transmission Unit, the biggest size of packet that can be sent
        // without fragmentation) used for all sockets
        unsigned int mtu() const { return mtu_; }

        unsigned int octet() const { return octet_; }

        daq_settings &interface(const std::string &i) { std::unique_lock lck(interface_); interface_ = i; return *this; }
        const std::string &interface() const { std::shared_lock lck(interface_); return interface_; }
        daq_settings &get_net_parameters();

        // Set the channel masks
        daq_settings &channel_masks(const std::vector<std::vector<bool>> &m) { std::unique_lock lck(channel_masks_); channel_masks_ = m; return *this; }
        // Get the enabled/disabled status of a given channel
        bool channel_mask(unsigned int i_adc, unsigned int i_channel_of_board) { std::shared_lock lck(channel_masks_); return channel_masks_[i_adc][i_channel_of_board]; }

        // Set the resolutions for all ADC boards (the vector 'r' must have as many elements as there are ADC boards)
        daq_settings &resolution_bits(const std::vector<unsigned int> &r) { resolution_bits_ = r;  return *this; }
        daq_settings &resolution_bits(std::initializer_list<unsigned int> r) {  resolution_bits(std::vector<unsigned int>{r}); return *this; }
        // Get the resolution in bits for the given ADC board (0-based)
        unsigned int resolution_bits(int i_adc) const { return resolution_bits_[i_adc]; }

        // These functions only return meaningful results after calling calculate_channel_info();
        unsigned int board_bytes_per_shot(int i_adc) { return board_bytes_per_shot_[i_adc]; }
        unsigned int chip_bytes_per_shot(int i_adc, int i_chip) { return chip_bytes_per_shot_[i_adc][i_chip]; }
        unsigned int chip_offset(int i_adc, int i_chip) { return chip_offset_[i_adc][i_chip]; }

        // Return the number of all enabled channels
        unsigned int enabled_channels() { return all_enabled_channels_.size(); }

        // Return the number of enabled channels on board 'board_number' (0-based)
//        unsigned int enabled_channels(unsigned int board_number) { return board_enabled_channels_[board_number].size(); }

        // Returns the maximum size of the packets: the CC header + the ADC data 
        // (but not including the UDP header, IPv4 header and Ethernet header)
        // If fewer samples are available at the end of a burst or data recording sequence,
        // the packet will be shorter.
        unsigned int max_udp_packet_size() const { return max_udp_packet_size_; }
        

        // Read/write the configuration into a  file
        void write_settings(const std::filesystem::path &filename);
        bool read_settings(const std::filesystem::path &filename);

        // Calculate the byte/bit offsets of the channels within the ADC bytes of a given sample,
        // and the masks/shifts to extract the values
        // calculates the bytes_per_sample_[...] values as well for each ADC
        // The resolution_bits_[i_adc] array and channel_masks_ must be set before calling this function !!!
        virtual void calculate_channel_info();

        void print_channel_map(std::ostream &out = std::cout);

    };

}

// include the template implementations
#include "daq_settings.Cinc"

#endif


