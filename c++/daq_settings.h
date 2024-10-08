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
#include <vector>
#include <string>
#include <iostream>

namespace apdcam10g
{
    class daq_settings
    {
    protected:
        std::string interface_ = "lo";
        const static int ipv4_header_ = 20;
        const static int udp_header_ = 8;
        unsigned int mtu_ = 0;
        unsigned int octet_ = 0;

        // The maximum UDP packet size, which is 22 bytes (streamheader) + 8*octet. At the end of a burst or a sequence
        // of transmitted shots, there may be a smaller UDP packet if the shots do not fill an entire one, but
        // all preceding packets will have this maximum size
        unsigned int max_udp_packet_size_ = 0;

        // A lot of (redundant) information to be able to access and manipulate the memory storage
        // in different ways efficiently
        std::vector<std::vector<bool>>              channel_masks_;           // Indices: ADC number, channel number within board. It is initialized for all channels being enabled
        std::vector<unsigned int>                   resolution_bits_;         // Index: ADC number. Initialized as 14 for all boards

        // These data members below are not initialized by default. They are calculated by the 'calculate_channel_info' member function
        // which must be called before the daq is started
        std::vector<unsigned int>                   board_bytes_per_shot_;    // index is ADC number
        std::vector<std::vector<unsigned int>>      chip_bytes_per_shot_;     // indices are ADC number (0..3max) and chip nummber (0..3)
        std::vector<std::vector<unsigned int>>      chip_offset_;             // Offset of the first data byte of the chip w.r.t. the board's first data byte, indices are ADC number and chip number

        std::vector<channel_info*>              all_enabled_channels_info_;
        std::vector<std::vector<channel_info*>> board_enabled_channels_info_; // First index is ADC board number, second index is the enabled channel index

    public:
        void dump()
            {
                using namespace std;
                output_lock lck;
                cerr<<"Interface: "<<interface_<<endl;
                cerr<<"MTU      : "<<mtu_<<endl;
                cerr<<"Octet    : "<<octet_<<endl;
                cerr<<"Max packet size: "<<max_udp_packet_size_<<endl;

                cerr<<"Channel masks: "<<endl;
                for(int i=0; i<channel_masks_.size(); ++i)
                {
                    for(int j=0; j<channel_masks_[i].size(); ++j) 
                    {
                        if(channel_masks_[i][j]) cerr<<terminal::green_bg<<terminal::black_fg;
                        cerr<<j;
                        if(channel_masks_[i][j]) cerr<<terminal::reset;
                        cerr<<"  ";
                    }
                    cerr<<endl;
                }
                cerr<<"Resolutions: [ ";
                for(auto r : resolution_bits_) cerr<<r<<" ";
                cerr<<" ]"<<endl;
                cerr<<"Bytes per shot: ";
                for(auto b : board_bytes_per_shot_) cerr<<b<<" ";
                cerr<<endl;
                cerr<<"Chip bytes per shot: ";
                for(auto &a: chip_bytes_per_shot_)
                {
                    cerr<<"[ ";
                    for(auto &b: a) cerr<<b<<" ";
                    cerr<<"] ";
                }
                cerr<<endl;

                cerr<<"Chip offsets: ";
                for(auto &a: chip_offset_)
                {
                    cerr<<"[ ";
                    for(auto &b: a) cerr<<b<<" ";
                    cerr<<"] ";
                }
                cerr<<endl;

                cerr<<"All enabled channels: "<<endl;
                for(auto a: all_enabled_channels_info_) a->dump();

                cerr<<endl;
                cerr<<"Board enabled channels: "<<endl;
                for(int i=0; i<board_enabled_channels_info_.size(); ++i)
                {
                    cerr<<"Board "<<i<<endl;
                    for(auto a: board_enabled_channels_info_[i]) a->dump();
                }

            }

        // Initialize with all possible ADC boards being present, with all of their channels being
        // enabled, and all ADC boards' resolution being set to 14 bits
        daq_settings();

        ~daq_settings();

        // set/get the MTU value (Maximum Transmission Unit, the biggest size of packet that can be sent
        // without fragmentation) used for all sockets
        daq_settings &mtu(unsigned int m);
        unsigned int mtu() const { return mtu_; }

        daq_settings &interface(const std::string &i) { interface_ = i; return *this; }
        const std::string &interface() const { return interface_; }
        daq_settings &get_net_parameters();

        // Set the channel masks
        daq_settings &channel_masks(const std::vector<std::vector<bool>> &m) { channel_masks_ = m; return *this; }
        // Get the enabled/disabled status of a given channel
        bool channel_mask(unsigned int i_adc, unsigned int i_channel_of_board) { return channel_masks_[i_adc][i_channel_of_board]; }

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
        unsigned int enabled_channels() { return all_enabled_channels_info_.size(); }

        // Return the number of enabled channels on board 'board_number' (0-based)
//        unsigned int enabled_channels(unsigned int board_number) { return board_enabled_channels_info_[board_number].size(); }

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
        void calculate_channel_info();

        void print_channel_map(std::ostream &out = std::cout);

    };

}


#endif


