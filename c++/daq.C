#include <exception>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <chrono>

#include "settings.h"
#include "backtrace.h"
#include "tee.h"
#include "daq.h"
#include "utils.h"
#include "error.h"
#include "pstream.h"
#include "processor_diskdump.h"
#include "processor_python.h"
#include "channel_data_extractor.h"
#include "shot_data_layout.h"
#include <memory>
#include <map>

#include <unistd.h>

namespace apdcam10g
{
    using namespace std;

    void throw_int(int signum) {throw signum; }

    void terminate_with_stacktrace() throw()
    {
#ifdef STACKTRACE
        try
        {
            print_backtrace();
        }
        catch(...)
        {
            cerr<<"Unexpected exception caught in 'terminate_with_stacktrace()'"<<endl;
        }
#endif
        abort();
    }

    class flag_locker
    {
    private:
        std::atomic_flag *flag_;
    public:
        flag_locker(std::atomic_flag &flag) : flag_(&flag) {flag.test_and_set();}
        ~flag_locker() {flag_->clear();}
    };

    class file_deleter
    {
    private:
        std::filesystem::path path_;
    public:
        file_deleter(const std::filesystem::path &p) : path_(p) {}
        ~file_deleter() { unlink(path_.c_str()); }
    };

    // A utility class to transform POSIX signals such as SIGSEGV to c++ exceptions, which are then handled
    // in a common way
    class signal2exception
    {
    private:
        static std::map<std::jthread::id,std::string> thread_names_;

        // The signal-handler routine which can be set for POSIX signals. It simply throws an apdcam_error exception
        // with the appropriate message
        static void run(int signum)
            {
                auto s = std::to_string(signum);
                if(signum==SIGKILL) s = "SIGKILL";
                if(signum==SIGTERM) s = "SIGTERM";
                if(signum==SIGSEGV) s = "SIGSEGV";
                output_lock lck;
                APDCAM_ERROR("Signal " + s + " is caught in thread \"" + thread_names_[std::this_thread::get_id()] + "\"");
                signal (signum, SIG_DFL);
                raise (signum);
            }

    public:

        // The below vararg 'set' functions can be used to set the signal handler for the given signals.
        // The first argument is a name that will be associated with the calling thread for the user's
        // convenience, subsequent integer args (any number of them) are signal numbers
        // Usage: signal2exception::set("my_thread_name",SIGSEGV,SIGINT);

        static void set(const std::string &thread_name, int signum)
            {
                thread_names_[std::this_thread::get_id()] = thread_name;
                signal(signum,signal2exception::run);
            }

        template <typename... SIGNUMS>
        static void set(const std::string &thread_name, int signum1, SIGNUMS... signums)
            {
                signal(signum1,signal2exception::run);
                set(thread_name,signums...);
            }
    };

    std::map<std::jthread::id,std::string> signal2exception::thread_names_;

    // ------------------------- daq_settings -----------------------------------------------

    inline bool has_enabled_channel(const std::vector<bool> &mask)
    {
        for(auto f : mask) if(f) return true;
        return false;
    }

    template <typename CHINFO>
    void daq_settings<CHINFO>::dump()     
    {
        using namespace std;
        output_lock lck;
        cerr<<"Interface: "<<interface()<<endl;
        cerr<<"MTU      : "<<mtu_<<endl;
        cerr<<"Octet    : "<<octet_<<endl;
        cerr<<"Max packet size: "<<max_udp_packet_size_<<endl;

        cerr<<"Channel masks: "<<endl;
        {
            std::shared_lock lck(channel_masks_);
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
        }
        cerr<<"Resolutions: [ ";
        {
            std::shared_lock lck(resolution_bits_);
            for(auto r : resolution_bits_) cerr<<r<<" ";
        }
        cerr<<" ]"<<endl;

        cerr<<"Bytes per shot: ";
        {
            std::shared_lock lck(board_bytes_per_shot_);
            for(auto b : board_bytes_per_shot_) cerr<<b<<" ";
        }
        cerr<<endl;

        cerr<<"Chip bytes per shot: ";
        {
            std::shared_lock lck(chip_bytes_per_shot_);
            for(auto &a: chip_bytes_per_shot_)
            {
                cerr<<"[ ";
                for(auto &b: a) cerr<<b<<" ";
                cerr<<"] ";
            }
        }
        cerr<<endl;

        cerr<<"Chip offsets: ";
        {
            std::shared_lock lck(chip_offset_);
            for(auto &a: chip_offset_)
            {
                cerr<<"[ ";
                for(auto &b: a) cerr<<b<<" ";
                cerr<<"] ";
            }
        }
        cerr<<endl;

        cerr<<"All enabled channels: "<<endl;
        {
            std::shared_lock lck(all_enabled_channels_);
            for(auto a: all_enabled_channels_) a->dump();
        }
        cerr<<endl;
        
        cerr<<"Board enabled channels: "<<endl;
        {
            std::shared_lock lck(board_enabled_channels_);
            for(int i=0; i<board_enabled_channels_.size(); ++i)
            {
                cerr<<"Board "<<i<<endl;
                for(auto a: board_enabled_channels_[i]) a->dump();
            }
        }
    }


    template <typename CHINFO>
    daq_settings<CHINFO>::daq_settings()
    {
        {
            std::unique_lock lck(interface_);
            interface_ = std::string("lo");
        }
        {
            std::unique_lock lck(channel_masks_);
            channel_masks_.resize(config::max_boards);
            // By default enable all channels, with all possible ADC boards present
            for(auto &a : channel_masks_) a.resize(config::channels_per_board,true);
        }
        {
            std::unique_lock lck(resolution_bits_);
            resolution_bits_.resize(config::max_boards,14);
        }
    }

    template <typename CHINFO>
    daq_settings<CHINFO>::~daq_settings()
    {
        std::unique_lock lck(all_enabled_channels_);
        for(auto c : all_enabled_channels_) delete c;
    }

    template <typename CHINFO>
    void daq_settings<CHINFO>::print_channel_map(std::ostream &out)
    {
        for(int i_adc=0; i_adc<board_enabled_channels_.size(); ++i_adc)
        {
            out<<endl<<"ADC "<<i_adc<<endl;
            {
                std::shared_lock lck(resolution_bits_);
                out<<"Resolution: "<<resolution_bits_[i_adc]<<endl;
            }

            out<<endl;
            {
                std::shared_lock lck(board_enabled_channels_);
                for(auto c : board_enabled_channels_[i_adc])
                {
                    out<<"board number    : "<<c->board_number<<endl;
                    out<<"chip number     : "<<c->chip_number<<endl;
                    out<<"channel number  : "<<c->channel_number<<endl;
                    out<<"abs. ch. number : "<<c->absolute_channel_number<<endl;
                    out<<"enabl. ch. numb.: "<<c->enabled_channel_number<<endl;
                    out<<"byte offset     : "<<c->byte_offset<<endl;
                    out<<"nbytes          : "<<c->nbytes<<endl;
                    out<<"shift           : "<<c->shift<<endl;
                    out<<endl;
                }
            }
        }
    }

    template <typename CHINFO>
    void daq_settings<CHINFO>::mtu(unsigned int m)
    {
        mtu_ = m;
        const int max_adc_data_length = mtu_ - (packet::ipv4_header+packet::udp_header+packet::cc_streamheader);
        octet_ = max_adc_data_length/8; // INTEGER DIVISION!
        if (octet_ < 1) APDCAM_ERROR("MTU value is too small!" + std::to_string(m));
        max_udp_packet_size_ = 8*octet_ + packet::cc_streamheader;
    }    

    template <typename CHINFO>
    daq_settings<CHINFO> &daq_settings<CHINFO>::get_net_parameters()
    {
        bool mtu_ok=false, mac_ok=false, ip_ok=false;

        {


            string cmd_string = "ip link show " + interface_;
            ipstream cmd(cmd_string);
            string s;
            while(cmd>>s)
            {
                if(s == "mtu")
                {
                    unsigned int m=0;
                    cmd>>m;
                    mtu(m); // Set MTU and calculate 'octet_'
                    mtu_ok = true;
                }
                /*
                if(s == "link/ether") 
                {
                    cmd>>s;
                    auto ss = split(s,":");
                    if(ss.size() != 6) APDCAM_ERROR("The MAC address returned by the command '" + cmd_string+ "' does not contain 6 bytes");
                    for(int i=0; i<6; ++i) mac_[i] = std::stol(ss[i],0,16);
                    mac_ok = true;
                }
                */
            }
        }

        /*
        {
            string cmd_string = "ip -o address show " + interface_;
            auto cmd = ipstream(cmd_string);
            string s;
            while(cmd>>s)
            {
                if(s=="inet")
                {
                    cmd>>s;
                    ip_ = split(s,"/")[0];
                    ip_ok = true;
                }
            }
        }

        if(!mtu_ok || !mac_ok || !ip_ok)
        {
            string cmd_string = "ifconfig " + interface_;
            ipstream cmd(cmd_string);
            string s;
            while(cmd>>s)
            {
                if(s=="mtu")
                {
                    cmd>>mtu_;
                    mtu_ok = true;
                }
                if(s=="ether") 
                {
                    cmd>>s;
                    auto ss = split(s,":");
                    if(ss.size() != 6) APDCAM_ERROR("The MAC address returned by the command '" + cmd_string+ "' does not contain 6 bytes");
                    for(int i=0; i<6; ++i) mac_[i] = std::stol(ss[i],0,16);
                    mac_ok = true;
                }
                if(s=="inet")
                {
                    cmd>>ip_;
                    ip_ok = true;
                }
            }
        }
        if(!mtu_ok || !mac_ok || !ip_ok) APDCAM_ERROR("Could not determine MTU, MAC or IP");
        */

        output_lock lck;
	if(!mtu_ok)
	{
	  APDCAM_ERROR("Could not determine MTU");
	}
	else
	{
	    {
	      std::shared_lock lck(interface_);
	      cerr<<"Interface: "<<interface_<<endl;
	    }
	    cerr<<"this = "<<this<<endl;
	    cerr<<"&mtu = "<<&mtu_<<endl;
	    cerr<<"&octet = "<<&octet_<<endl;
	    cerr<<"MTU      : "<<mtu_<<endl;
	    cerr<<"OCTET    : "<<octet_<<endl;
	    cerr<<endl;
	}

        return *this;
    }

    template <typename CHINFO>
    void daq_settings<CHINFO>::calculate_channel_info()
    {
        if(resolution_bits_.size() != channel_masks_.size()) 
            APDCAM_ERROR("Resolutions (" + std::to_string(resolution_bits_.size()) + ") and channel masks (" + std::to_string(channel_masks_.size()) + ") have different size");

        std::shared_lock lck1(channel_masks_);
        std::unique_lock lck2(board_bytes_per_shot_);
        std::unique_lock lck3(chip_bytes_per_shot_);
        std::unique_lock lck4(chip_offset_);
        std::unique_lock lck5(all_enabled_channels_);
        std::unique_lock lck6(board_enabled_channels_);

        const unsigned int nof_adc = channel_masks_.size();

        board_bytes_per_shot_.clear();
        board_bytes_per_shot_.resize(nof_adc,0);

        chip_bytes_per_shot_.clear();
        chip_bytes_per_shot_.resize(nof_adc);
        for(auto &v : chip_bytes_per_shot_) v.resize(config::chips_per_board,0);

        chip_offset_.clear();
        chip_offset_.resize(nof_adc);
        for(auto &v : chip_offset_) v.resize(config::chips_per_board,0);

        // Delete all channel_info objects, and clear the vector
        for(auto a : all_enabled_channels_) delete a;
        all_enabled_channels_.clear();

        // Resize the per-board vector, and clear all of its elements
        board_enabled_channels_.resize(nof_adc);
        for(auto &a : board_enabled_channels_) a.clear();

        // Create a slot for all possible channels (even for those not enabled), containing initially
        // zero pointers. This array is indexed by the absolute channel number
        all_channels_.clear();
        all_channels_.resize(nof_adc*config::channels_per_board,0);

        // For each adc board there is a last channel (this is made use of in DAQ)
        // This array is indexed by the ADC board number
        board_last_enabled_channel_.clear();
        board_last_enabled_channel_.resize(nof_adc);

        cerr<<"CHECK THE LOCKS FROM HERE DOWNWARDS IN daq.C"<<endl;

        for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
        {
            board_bytes_per_shot_[i_adc] = 0;

            // Skip those ADC boards which have no channels enabled. All info has been already initialized before this loop so we
            // do not need to do anything else
            if(!has_enabled_channel(channel_masks_[i_adc])) continue;

            for(unsigned int i_chip=0; i_chip<config::chips_per_board; ++i_chip)
            {
                if(i_chip==0) chip_offset_[i_adc][i_chip] = 0;
                else          chip_offset_[i_adc][i_chip] = chip_offset_[i_adc][i_chip-1] + chip_bytes_per_shot_[i_adc][i_chip-1];

                // start accumulating the chip's bytes per shot from zero
                chip_bytes_per_shot_[i_adc][i_chip] = 0;

                // The offset of the given channel in terms of bits w.r.t. the given chip's first bit, a sliding value
                unsigned int channel_bit_offset = 0;

                // Calculate the number of bits used by this chip (a chip is a group of config::channels_per_chip channels). 
                for(unsigned int i_channel_of_chip=0; i_channel_of_chip<config::channels_per_chip; ++i_channel_of_chip)
                {
                    const unsigned int i_channel_of_board = i_chip*config::channels_per_chip + i_channel_of_chip;

                    // skip disabled channels
                    if(!channel_masks_[i_adc][i_channel_of_board]) continue;

                    //CHINFO *chinfo = new CHINFO;
                    CHINFO *chinfo = create_channel_info();
                    
                    chinfo->board_number = i_adc;
                    chinfo->chip_number = i_chip;
                    chinfo->channel_number = i_channel_of_board;
                    chinfo->absolute_channel_number = i_adc*config::chips_per_board*config::channels_per_chip + i_channel_of_board;
                    chinfo->enabled_channel_number = all_enabled_channels_.size();
                    chinfo->byte_offset    = chip_offset_[i_adc][i_chip] + channel_bit_offset/8;

                    // The first bit of this channel's value within the byte, STARTING FROM LEFT, FROM THE MOST SIGNIFICANT BIT
                    const unsigned int startbit = channel_bit_offset%8; // starting from 'left', that is, from the most significant bit

                    // The number of bytes over which this value is distributed
                    chinfo->nbytes = (startbit+resolution_bits_[i_adc])/8 + ((startbit+resolution_bits_[i_adc])%8 ? 1 : 0);
                    chinfo->nbits = resolution_bits_[i_adc];

                    // The right-shift (deduced from the last bit of this value within the last byte)
                    chinfo->shift = 8-((startbit+resolution_bits_[i_adc])%8); 
                    if(chinfo->shift==8) chinfo->shift=0;

                    // Checks
                    if(chinfo->nbytes == 1 && chinfo->shift != 0) APDCAM_ERROR("Bug! With 1 bytes the shift should be 1.");

                    channel_bit_offset += resolution_bits_[i_adc];

                    all_enabled_channels_.push_back(chinfo);
                    board_enabled_channels_[i_adc].push_back(chinfo);
                    all_channels_[chinfo->absolute_channel_number] = chinfo;
                    board_last_enabled_channel_[i_adc] = chinfo;
                }

                // channel_bit_offset here is the number of bits used for this chip. Calculate the number of full bytes
                // which can contain this many bits
                // const int variable named only to clearly indicate what we do. Used only in the next line
                chip_bytes_per_shot_[i_adc][i_chip] = channel_bit_offset/8 + (channel_bit_offset%8 ? 1 : 0);

                // Accumulate the number of bytes by each chip
                board_bytes_per_shot_[i_adc] += chip_bytes_per_shot_[i_adc][i_chip];
            }

            // Round up the number of bytes of an ADC board to an integer multiple of 4 bytes
            if(board_bytes_per_shot_[i_adc]%4 != 0) board_bytes_per_shot_[i_adc] = (board_bytes_per_shot_[i_adc]/4+1)*4;
        }
    }

    template <typename CHINFO>
    void daq_settings<CHINFO>::write_settings(const std::filesystem::path &filename)
    {
        //Json::Value settings_root;
        settings the_settings;
        the_settings["n_adc"] = channel_masks_.size();
        for(unsigned int i_adc=0; i_adc<channel_masks_.size(); ++i_adc)
        {
            //settings_root["resolution_bits"][i_adc] = resolution_bits_[i_adc];
            the_settings["resolution_bits/" + std::to_string(i_adc)] = resolution_bits_[i_adc];
            //settings_root["resolution_bits"][i_adc].setComment(Json::String(("// ADC " + std::to_string(i_adc)).c_str()),Json::commentAfterOnSameLine);
            for(unsigned int i_board_channel=0; i_board_channel<config::channels_per_board; ++i_board_channel)
            {
                const bool b = channel_masks_[i_adc][i_board_channel];
                //settings_root["channel_masks"][i_adc][i_board_channel] = b;
                the_settings["channel_masks/" + std::to_string(i_adc) + "/" + std::to_string(i_board_channel)] = b;
                //settings_root["channel_masks"][i_adc][i_board_channel].setComment(Json::String(("// Channel " + std::to_string(i_board_channel)).c_str()),Json::commentAfterOnSameLine);
            }
            //settings_root["channel_masks"][i_adc].setComment(Json::String(("// ADC " + std::to_string(i_adc)).c_str()),Json::commentBefore);
        }
        ofstream file(filename);
        //file<<settings_root<<endl;
        file<<the_settings;
    }
    
    template <typename CHINFO>
    bool daq_settings<CHINFO>::read_settings(const std::filesystem::path &filename)
    {
        ifstream file(filename);
        if(!file) return false;
        //Json::Value settings_root;
        settings the_settings;
        //file>>settings_root;
        file>>the_settings;
//        for(auto key: {"resolution_bits","channel_masks"})
//        {
//            if(!settings_root.isMember(key)) APDCAM_ERROR(string("Value '") + key + "' is not stored in the file '" + filename + "'");
//        }

        //const int n_adc = settings_root["channel_masks"].size();
        const int n_adc = the_settings["n_adc"];

//        if(settings_root["resolution_bits"].size() != n_adc) APDCAM_ERROR("The arrays 'channel_masks' and 'resolution_bits' must have the same size in file '" + filename + "'");

        channel_masks_.resize(n_adc);
        for(auto &a : channel_masks_) a.resize(config::channels_per_board);
        resolution_bits_.resize(n_adc);

        for(unsigned int i_adc=0; i_adc<n_adc; ++i_adc)
        {
            //resolution_bits_[i_adc] = settings_root["resolution_bits"][i_adc].asInt();
            resolution_bits_[i_adc] = the_settings["resolution_bits/" + std::to_string(i_adc)];
            for(unsigned int i_board_channel=0; i_board_channel<config::channels_per_board; ++i_board_channel)
            {
                //channel_masks_[i_adc][i_board_channel] = settings_root["channel_masks"][i_adc][i_board_channel].asBool();
                channel_masks_[i_adc][i_board_channel] = the_settings["channel_masks/" + std::to_string(i_adc) + "/" + std::to_string(i_board_channel)];
            }
        }

        calculate_channel_info();
        return true;
    }
    template class daq_settings<ring_buffer<apdcam10g::data_type,channel_info>>;
    template class daq_settings<channel_info_with_generator>;
    

    // ------------------------- daq -------- -----------------------------------------------
    
    daq::daq()
    {

        // the class 'daq' is a singleton, so we make global initialization here

      //        tee(std::cout,configdir() / "cout");
      //        tee(std::cerr,configdir() / "cerr");

        std::set_terminate(terminate_with_stacktrace);
        
        // Convert segmentation violation and termination signals to exceptions
        signal2exception::set("DAQ-main",SIGSEGV,SIGTERM);

	get_net_parameters();
    }

    std::string daq::section_start(std::string text)
    {
        text = " START: " + text + " ";
        const int L = 120;
        const int l = text.size();
        const int L1 = std::max((L-l)/2,2);
        const int L2 = std::max(L-L1-l,2);
        string result;
        for(unsigned int i=0; i<L1; ++i) result += '=';
        result += text;
        for(unsigned int i=0; i<L2; ++i) result += '=';
        return result;
    }

    std::string daq::section_end(std::string text)
    {
        text = " END: " + text + " ";
        const int L = 120;
        const int l = text.size();
        const int L1 = std::max((L-l)/2,2);
        const int L2 = std::max(L-L1-l,2);
        string result;
        for(unsigned int i=0; i<L1; ++i) result += '=';
        result += text;
        for(unsigned int i=0; i<L2; ++i) result += '=';
        return result;
    }

    
    

    bool daq::python_analysis_stop()
    {
        return python_analysis_stop_.test(std::memory_order_acquire);
    }
    
    void daq::python_analysis_stop(bool b)
    {
        if(b) python_analysis_stop_.test_and_set(std::memory_order_release);
        else python_analysis_stop_.clear();
    }

    void daq::python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter)
    {
        python_analysis_run_.wait(false,std::memory_order_acquire);
        *from_counter = python_analysis_data_available_from_;
        *to_counter   = python_analysis_data_available_to_;
    }

    size_t daq::python_analysis_wait_finish()
    {
        python_analysis_run_.wait(true,std::memory_order_acquire);
        return python_analysis_needs_data_from_;
    }

    void daq::python_analysis_start(size_t from_counter, size_t to_counter)
    {
        python_analysis_data_available_from_ = from_counter;
        python_analysis_data_available_to_   = to_counter;
        python_analysis_run_.test_and_set(std::memory_order_release);
        python_analysis_run_.notify_one();
    }

    void daq::python_analysis_done(size_t need_data_from)
    {
        python_analysis_needs_data_from_ = need_data_from;
        python_analysis_run_.clear(std::memory_order_release);
        python_analysis_run_.notify_one();
    }

    std::mutex daq::init_mutex_;

    // Creating the daq::instance_ as a static variable. Probably no problem in initialization order
    // because it is only accessible via the daq::instance() function. The daq::instance() function is
    // only available after loading the shared library, and during loading the shared library the
    // static variables are (very probably) initialized
  //    daq daq::instance_;

  daq *daq::instance_ = 0;

    daq &daq::instance()
    {
      if(instance_==0) instance_ = new daq;
      return *instance_;
    }

    void daq::finish()
    {
        for(auto p : processors_) p->finish();
    }

    daq &daq::process_period(unsigned int p)
    { 
        if( (p-1)&p != 0)
        {
            show_error("Process period must be a power of 2","daq::process_period");
            return *this;
        }
        process_period_ = p; 
        return *this; 
    }

    template <safeness S>
    daq &daq::init()
    {
        try
        {
            // Calculate the all_enabled_channels_info / board_enabled_channels_info vectors (ranges), and the
            // number of all enabled channels
            calculate_channel_info();

            if(mtu_ == 0) APDCAM_ERROR("MTU has not been set");

            // Calculate and store the number of ADC boards from the channel mask's size.
            unsigned int nof_adc = 0;
            {
                std::shared_lock lck(channel_masks_);
                nof_adc = channel_masks_.size();
            }

            if(mtu_==0) APDCAM_ERROR("MTU has not yet been specified in daq::initialize");
            if(dual_sata_ && nof_adc>2) APDCAM_ERROR("Dual sata is set with more than two ADC boards present");

            // Resize the socket vector to have as many elements as there are ADC boards
            sockets_.clear(); // delete any previously open socket, if any
            sockets_.resize(nof_adc);

            // Resize the network buffer vector to have as many elements as there are ADC boards. Initialize their buffer size
            {
                output_lock lck;
                cerr<<"[DAQ] Network buffers : "<<network_buffer_size_<<" packets of size "<<max_udp_packet_size_<<endl<<endl;
            }
            //regenerate(network_buffers_,nof_adc,network_buffer_size_,max_udp_packet_size_);
            regenerate_by_func(network_buffers_,nof_adc,[this](unsigned int i_adc){return has_enabled_channel(channel_masks_[i_adc]) ? new udp_packet_buffer<default_safeness>(network_buffer_size_,max_udp_packet_size_) : 0; });

            // Resize the extractors vector to have as many elements as there are ADC boards
            //regenerate_by_func(extractors_, nof_adc, [this](unsigned int i_adc){return new channel_data_extractor<default_safeness>(this,fw_version_,i_adc);});
//            regenerate_by_func(extractors_, nof_adc, [this](unsigned int i_adc){return has_enabled_channel(channel_masks_[i_adc]) ? new channel_data_extractor<default_safeness>(this,fw_version_,i_adc) : 0; });

            // Open the input ports
            {
                output_lock lck;
                for(unsigned int i_adc=0; i_adc<nof_adc; ++i_adc)
                {
                    output_lock lck;
                
                    const int port_index = (dual_sata_ ? i_adc*2 : i_adc);
                    cerr<<"[DAQ] ====== ADC Board #"<<i_adc<<" ======"<<endl;
                    cerr<<"[DAQ] Port            : "<<config::ports[i_adc*2]<<endl;

                    // Do not open an input port for those ADC boards which have no channels emabled
                    if(!has_enabled_channel(channel_masks_[i_adc]))
                    {
                        cerr<<"[DAQ] No channels are enabled for this ADC board, socket is not opened"<<endl;
                        continue;
                    }
                
                    sockets_[i_adc].open(config::ports[port_index]);
                    cerr<<"[DAQ] Bytes per shot  : "<<board_bytes_per_shot_[i_adc]<<endl;
                    cerr<<"[DAQ] Enabled channels: ";
                    for(auto c : board_enabled_channels_[i_adc]) cerr<<c->channel_number<<" ";
                    cerr<<endl;
                    if(debug_)
                    {
                        shot_data_layout layout(board_bytes_per_shot_[i_adc], resolution_bits_[i_adc], board_enabled_channels_[i_adc]);
                        layout.prompt("[DAQ]");
                        cerr<<"[DAQ] ---- SHOT DATA LAYOUT ----"<<endl;
                        layout.show();
                        cerr<<"[DAQ] --------------------------"<<endl;
                    }
                    cerr<<endl;
                }
            }

            for(auto p : processors_) p->init();

            // Call the debug(...) function with the saved value (seems useless...) in order to set the corresponding flags
            // of the worker objects created since the last call
            debug(debug_);

            // Make sure these communication flags are set to a correct initicial value
            python_analysis_run_.clear();
            python_analysis_stop_.clear();
        }
        CATCH_ALL();
        return *this;
    }

    void daq::stop_cmd_thread()
    {

        if(!command_thread_active_.test()) return;
        pthread_kill(command_thread_.native_handle(), SIGTERM);

        // Necessary? file_deleter created upon thread startup should do the job...
        unlink(cmd_fifo_name_.c_str());
    }

    std::string daq::cmd_help_text()
    {
        return
R"(
One can interact with the running DAQ process via commands that are sent to the process by the following commnad:
apdcam-daq -c <command>
It does nothing else but write the command into the named pipe ~/.apdcam10g/cmd, so if you prefer, you can also
do this low-level stuff directly.

The following commands are accepted and interpreted by the DAQ process:

diskdump_pause
    Pause dumping the data to disk. The DAQ and all other data processor tasks
    keep running in the same way as before.

diskdump_resume
    Resume writing data to disk.

diskdump_sampling <n>
    Change the sampling rate of writing the channel data to disk.
    Every nth sample is written, the others are not. (Is it sunchronized
    among the different channels? CHECK!

stop [timeout]
    Stop the DAQ in a soft way (instructing the network reader threads to stop reading and finish
    their data queue towards the processors). If timeout (in seconds, integer) is provided, the threads
    are killed gracelessly after these many seconds.

)";
            
            
    }

    void daq::start_cmd_thread()
    {
        if(command_thread_active_.test()) return;
        command_thread_ = std::jthread( [this](std::stop_token stok)
            {
                flag_locker flk(command_thread_active_);
                const std::string prompt = "[DAQ/CMD] ";

//                signal2exception::set("command",SIGSEGV);
                
                signal(SIGTERM,throw_int);

                try
                {
                    
                    // Create the fifo in configdir
                    unlink(cmd_fifo_name_.c_str()); // Remove fifo if it exists accidentally
                    if(mkfifo(cmd_fifo_name_.c_str(),0666) != 0) APDCAM_ERROR("Failed to create command fifo '" + cmd_fifo_name_.string() + "'");

                    // A bookkeeping object to ensure that the fifo is removed when this thread exits in whatever way,
                    // and this object is destroyed (automatically)
                    file_deleter auto_delete_fifo(cmd_fifo_name_);

                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread started, waiting for command in the FIFO "<<cmd_fifo_name_<<endl;
                    }

                    string line;
                    while(!stok.stop_requested())
                    {
                        // Reopen the file repeatedly because if we send commands into the FIFO file by echo, it closes the
                        // file, and fifo.clear() (i.e. clearing all error bits on the ifstream) does not help. 
                        ifstream fifo(cmd_fifo_name_);

//                        cerr<<prompt<<"Waiting for command in fifo..."<<endl;

                        while(!stok.stop_requested() && getline(fifo,line))
                        {
                            auto now = std::chrono::system_clock::now();
                            std::time_t now_time = std::chrono::system_clock::to_time_t(now);
                            cerr<<prompt<<std::ctime(&now_time)<<" "<<line<<endl;
                            istringstream inputstr(line);
                            string cmd;
                            inputstr>>cmd;
                            if     (cmd == "diskdump_pause")
                            {
                                cerr<<prompt<<"Pausing diskdump"<<endl;
                                daq::instance().diskdump_pause();
                            }
                            else if(cmd == "diskdump_resume")
                            {
                                cerr<<prompt<<"Resuming diskdump"<<endl;
                                daq::instance().diskdump_resume();
                            }
                            else if(cmd == "diskdump_sampling")
                            {
                                unsigned int s;
                                if(!(inputstr>>s)) 
                                {
                                    cerr<<"Error, integer expected after diskdump_sampling"<<endl;
                                    continue;
                                }
                                cerr<<prompt<<"Setting diskdump sampling of "<<s<<endl;
                                daq::instance().diskdump_sampling(s);
                            }
                            else if(cmd == "stop")
                            {
                                unsigned int timeout_sec;
                                if(!(inputstr>>timeout_sec)) timeout_sec = 0;
                                cerr<<prompt<<"Stopping the DAQ";
                                if(timeout_sec>0) cerr<<" with a timeout of "<<timeout_sec<<" seconds";
                                cerr<<endl;
                                daq::instance().stop(timeout_sec);
                            }
                            else
                            {
                                cerr<<prompt<<"Ignoring bad command: "<<line<<endl;
                            }
                        }
                    }
                }
                catch(apdcam10g::error &d)
                {
                    output_lock lck;
                    cerr<<prompt<<" Terminated: "<<d.message()<<endl;
                }
                catch(int i)  // upon receiving SIGTERM, an integer is thrown. Catch it and do nothing, just exist
                {
                }

                unlink(cmd_fifo_name_.c_str());
            });
    }

    template <safeness S>
    daq &daq::start(bool wait)
    {
        try
        {
            {
                output_lock lck;
                cerr<<"[DAQ] STARTING..."<<endl;
            }

            // First make sure that there are enabled channels at all. If not, report, and return
            bool enabled_channels_exist = false;
            for(auto &m : channel_masks_)
            {
                if(has_enabled_channel(m))
                {
                    enabled_channels_exist = true;
                    break;
                }
            }
            if(!enabled_channels_exist)
            {
                output_lock lck;
                cerr<<"[DAQ] No channels are enabled. We do nothing"<<endl;
                return *this;
            }

            write_settings(configdir() / "daq.cnf");

            {
                auto pid_file_name = configdir() / "pid";
                ofstream pid_file(pid_file_name);
                if(!pid_file.good()) APDCAM_ERROR("Could not create PID file " + pid_file_name.string());
                pid_file<<getpid()<<endl;
            }

            network_threads_.clear();
            extractor_threads_.clear();
            for(unsigned int i=0; i<config::max_boards; ++i)
            {
                network_threads_active_[i].clear();
                extractor_threads_active_[i].clear();
            }
            processor_thread_active_.clear();
        
            // start creating the threads from tne end-consumer end, i.e. backwards, so that consumers are ready (should we further sync?)
            // and listening by the time data starts to arrive.
        
            // ----------------------------- processor thread -------------------------------------------
            {
                output_lock lck;
                cerr<<"[DAQ] Starting processor thread"<<endl;
            }
            processor_thread_ = std::jthread( [this](std::stop_token stok)
                {
                    flag_locker flk(processor_thread_active_);
                    const std::string prompt = "[DAQ/PROC] ";
                    {
                        output_lock lck;
                        cerr<<prompt<<"Thread started"<<endl;
                    }
                    signal2exception::set("processor",SIGSEGV,SIGTERM);
                    try
                    {
                        // Loop until we receive a stop request (we break the loop if no more data is coming, see below)
                        // Processor tasks are called only when 'process_period_' new shots have arrived from all channels.
                        // to_counter is the running counter (shot-number) for which we need to wait (from each channels).
                        // It is incremented at the end of this loop
                        for(unsigned int to_counter=process_period_; !stok.stop_requested(); )
                        {
                            // Limits of the range of shots which are simultaneously available in all channel buffers
                            size_t common_pop_counter=0;
                            size_t common_push_counter=0;
                        
                            // check the last channels of each board, and spin-lock wait until they have a required new number
                            // of entries, or are terminated

                            // A flag to indicated whether there is a non-terminated buffer (stream of channel data) 
                            bool non_terminated_exists = false;

                            // The channel signals are stored sequentially in the UDP packets. So it is sufficient to
                            // check for the last channel of each board (i.e. in each stream), if it has arrived, all
                            // other channels of the same board have also arrived. We loop over all boards sequentially,
                            // and wait until we get the desired shot from this board. Then go to next board.
                            for(int i_adc=0; i_adc<board_last_enabled_channel_.size(); ++i_adc)
                            {
                                const channel_data_buffer_t *b = board_last_enabled_channel_[i_adc];  // just make a shorthand alias
                                bool terminated;
                                size_t push_counter;
                                // Spin-lock wait until the shot with the desired last channels of this board
                                while( (push_counter=b->push_counter())<to_counter && (terminated=b->terminated())==false );
                        
                                // If the buffer is terminated, re-query its push_counter (i.e. the last shot number pushed into this buffer)
                                // to capture the case when there were new shots added to the ring_buffer between the two statements (AND-ed)
                                // within the spin-lock while loop
                                if(terminated) push_counter = b->push_counter();
                                // Otherwise, if not terminated, set the flag that there are non-terminated buffers
                                else non_terminated_exists = true;  

                                // Make sure that 'common_push_counter' is the smallest among the last shot numbers of all ADC boards
                                // (i.e. we take the smallest interval of available shots from all ADC boards)
                                if(i_adc==0 || push_counter<common_push_counter) common_push_counter = push_counter;
                        
                                // Make sure that 'common_pop_counter' is the largest among the ADC boards,
                                // i.e. we take the smallest interval of available shots from all ADC boards
                                size_t pop_counter = b->pop_counter();
                                if(pop_counter > common_pop_counter) common_pop_counter = pop_counter;
                            }


                            bool data_in_buffer = false;
                            // is this 'if' needed? We waited for the shot 'to_counter' which has been incremented
                            // since its last value. Probably not
                            if(common_push_counter > common_pop_counter) 
                            {
                                // the counter to indicate the first element that is required to stay in the buffers
                                // by any of the processors
                                unsigned int need_shots_from = common_push_counter;
                                for(auto p : processors_) 
                                {
                                    const unsigned int this_processor_needs = p->run(common_pop_counter, common_push_counter);
                                    if(this_processor_needs > common_push_counter) APDCAM_ERROR("processor returned too high needed value");
                                    if(this_processor_needs < need_shots_from) need_shots_from = this_processor_needs;
                                }
                        
                                // after all tasks have run, and reported what the earliest element in the buffers that they
                                // need for further processing, clear the buffers up to this
                                for(auto a: all_enabled_channels_)
                                {
                                    a->pop_to(need_shots_from);
                                    if(!a->empty()) data_in_buffer = true;
                                }
                            }

                            // If there are no more non-terminated data streams (i.e. all streams are terminated)
                            // and they are also all empty, signal the python thread to stop, and break the infinite loop
                            if(!non_terminated_exists && !data_in_buffer)
                            {
                                python_analysis_stop_.test_and_set();  // Setting this will cause the python processor loop to stop
                                python_analysis_run_.test_and_set();   // This and the subsequent notification wakes up the python processor loop
                                python_analysis_run_.notify_one();     // and it will immediately learn that the stop flag was also set
                                break;
                            }

                            // Set the shot number that we will be waiting for in the next round. common_push_counter
                            // is the last shot nummber that all processor tasks have processed. Wait for 'process_period_'
                            // new shots. 
                            to_counter = common_push_counter + process_period_;
                        }
                        {
                            output_lock lck;
                            cerr<<prompt<<"Thread finished"<<endl;
                        }
                    }
                    catch(apdcam10g::error &e) 
                    { 
                        for(auto a: all_enabled_channels_) a->terminate();
                        cerr<<prompt<<e.full_message()<<endl; 
                    }
                });


            // ------------------- channel data extractor threads ------------------------------------------
            {
                output_lock lck;
                cerr<<"[DAQ] Starting extractor threads"<<endl;
            }
        
            for(unsigned int i_adc=0; i_adc<channel_masks_.size(); ++i_adc)
            {
                // Do not start a thread for those ADC boards which have no channels enabled
                if(!has_enabled_channel(channel_masks_[i_adc])) continue;

                extractor_threads_.push_back(std::jthread( [this, i_adc](std::stop_token stok)
                    {
                        flag_locker flk(extractor_threads_active_[i_adc]);
                        const std::string prompt = "[DAQ/EXT/" + std::to_string(i_adc) + "] ";
                        {
                            output_lock lck;
                            cerr<<prompt<<"Thread "<<i_adc<<" started"<<endl;
                        }
                        signal2exception::set("extractor" + std::to_string(i_adc),SIGSEGV,SIGTERM);
                        try
                        {
                            channel_data_extractor extractor(this,fw_version_,i_adc);
                            
//                            extractors_[i_adc]->run(*network_buffers_[i_adc],board_enabled_channels_[i_adc]);
                            extractor.run(*network_buffers_[i_adc],board_enabled_channels_[i_adc]);

                            // Write a summary of what happened
                            {
                                output_lock lck;
                                cerr<<prompt<<"Thread "<<i_adc<<" finished"<<endl;
                                cerr<<prompt<<"---------- Data extraction summary  ------------------"<<endl;
                                double sum=0, n=0, max=0;
                                for(auto b : board_enabled_channels_[i_adc])
                                {
                                    ++n;
                                    sum += b->mean_size();
                                    const auto m = b->max_size();
                                    if(m>max) max=m;
                                }
                                cerr<<prompt<<"average buffer size : "<<sum/n<<endl;
                                cerr<<prompt<<"max buffer size     : "<<max<<endl;
                                cerr<<prompt<<"buffers' capacity   : "<<channel_buffer_size_<<endl;
                                if(sum/n > channel_buffer_size_/2) cerr<<prompt<<"we recommend increasing the buffer size"<<endl;
                                cerr<<endl;
                            }
                        }
                        catch(apdcam10g::error &e) 
                        { 
                            for(auto a : board_enabled_channels_[i_adc]) a->terminate();
                            cerr<<prompt<<e.full_message()<<endl; 
                        }
                    }));
            }



            // -------------------------------- network reading thread(s) ----------------------------------
            // these threads do nothing but continuously read udp packets into the udp packet buffers 'network_buffers_'
            {
                output_lock lck;
                cerr<<"[DAQ] Starting network threads"<<endl;
            }
            {
                for(unsigned int i_adc=0; i_adc<channel_masks_.size(); ++i_adc)
                {
                    // Do not start a thread for those ADC boards which have no channels enabled
                    if(!has_enabled_channel(channel_masks_[i_adc])) continue;

                    // make the corresponding socket blocking. this thread is reading only from one socket, so
                    // we can safely be blocked until data is available 
                    sockets_[i_adc].blocking(true);

                    network_threads_.push_back(std::jthread( [this, i_adc](std::stop_token stok)
                        {
                            flag_locker flk(network_threads_active_[i_adc]);
                            const std::string prompt = "[DAQ/NET/" + std::to_string(i_adc) + "] ";
                            {
                                output_lock lck;
                                cerr<<prompt<<"Thread "<<i_adc<<" started"<<endl;
                            }
                            signal2exception::set("net" + std::to_string(i_adc),SIGSEGV,SIGTERM);
                            try
                            {
                                while(!stok.stop_requested())
                                {
                                    // The 'receive' function of the UDP packet buffer automatically takes care of lost packets
                                    // and inserts them with zero fill. We provide the stop_token 'stok' to this function
                                    // so that it can monitor eventual stop requests within the spin-lock waiting for new packets
                                    const auto received_packet_size = network_buffers_[i_adc]->receive(sockets_[i_adc],stok);

                                    // Reached the end of the stream. Both a partial packet, and the 'terminated' flag indicate
                                    // that the camera stopped sending more data
                                    if(received_packet_size != max_udp_packet_size_ || network_buffers_[i_adc]->terminated()) break;
                                }

                                // Close the socket, no more data is accepted
                                {
                                    output_lock lck;
                                    cerr<<prompt<<"Closing socket on port "<<sockets_[i_adc].port()<<endl;
                                }
                                sockets_[i_adc].close();

                                // If we have quit the while loop due to stop_requested, we need to set the terminated flag
                                // to indicate no more data coming down from the network. If not, we set it again at no harm.
                                network_buffers_[i_adc]->terminate();

                                // Write a summary
                                {
                                    output_lock lck;
                                    cerr<<prompt<<"Thread "<<i_adc<<" finished"<<endl;
                                    cerr<<prompt<<"---------- Network summary ---------------------------"<<endl;
                                    cerr<<prompt<<"Received packets    : "<<network_buffers_[i_adc]->received_packets()<<endl;
                                    cerr<<prompt<<"Lost packets        : "<<network_buffers_[i_adc]->lost_packets()<<endl;
                                    cerr<<prompt<<"Average buffer size : "<<network_buffers_[i_adc]->mean_size()<<endl;
                                    cerr<<prompt<<"Maximum buffer size : "<<network_buffers_[i_adc]->max_size()<<endl;
                                    cerr<<prompt<<"Buffer capacity     : "<<network_buffer_size_<<endl;
                                    if(network_buffers_[i_adc]->mean_size() > network_buffer_size_/2) cerr<<prompt<<"WE RECOMMEND INCREASING THE BUFFER SIZE"<<endl;
                                    cerr<<endl;
                                }

                            }
                            catch(apdcam10g::error &e) 
                            { 
                                network_buffers_[i_adc]->terminate();
                                cerr<<prompt<<e.full_message()<<endl; 
                            }
                        }));
                }
            }

            {
                output_lock lck;
                cerr<<"[DAQ] All threads have been started"<<endl;
            }

            if(wait)
            {
                sleep(1);
                wait_finish();
            }

            return *this;
        }
        catch(apdcam10g::error &e) { e.print(); }
        catch(...) { cerr<<"Unhandled exception thrown in daq::start()"<<endl; }
        return *this;
    }

    void daq::dump()
    {
        daq_settings::dump();
    }

    daq &daq::wait_finish()
    {
        if(processor_thread_.joinable()) processor_thread_.join();
        {        
            output_lock lck;
            cerr<<"[DAQ] Processor thread joined"<<endl;
        }
        for(auto &t : extractor_threads_) if(t.joinable()) t.join();
        {
            output_lock lck;
            cerr<<"[DAQ] Extractor threads joined"<<endl;
        }
        for(auto &t : network_threads_) if(t.joinable()) t.join();
        {
            output_lock lck;
            cerr<<"[DAQ] Network threads joined"<<endl;
        }

        // Do not join the command threa. The command thread is terminated by stop_cmd_thread() which sends
        // it the SIGTERM signal. If trying to join it afterwards, it seems to restart automatically... (?)

        finish();

        return *this;
    }

    template <safeness S>
    daq &daq::stop(unsigned int timeout_sec)
    {
        cerr<<"[DAQ] Stopping with timeout "<<timeout_sec<<endl;

        // These threads do not need to be requested to stop because they do so anyway if their input
        // ring_buffers get their 'terminated' flag set. 
        //processor_thread_.request_stop();
        //for(auto &t : extractor_threads_) t.request_stop();

        // Stopping the network threads will cause them to set the network_buffers_[i].terminated flag to true
        // as a consequence of which the channel_data_extractor threads will set the terminated flag of all of
        // the channel data buffers' flags to true, which in turn will cause the processor thread to
        // stop as well
        for(auto &t : network_threads_) t.request_stop();

        if(timeout_sec>0)
        {
            for(unsigned int t=0; t<timeout_sec; ++t)
            {
                sleep(1);
                if(network_threads()==0 && extractor_threads()==0 && processor_threads()==0) return *this;
                //auto [n1,n2,n3] = status();
                //if(n1==0 && n2==0 && n3==0) return *this;
            }
            kill();
        }

        return *this;
    }

    daq &daq::kill()
    {
        cerr<<"[DAQ] Killing all threads"<<endl;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test())   pthread_kill(network_threads_[i_socket].native_handle(), SIGTERM);
            if(extractor_threads_active_[i_socket].test()) pthread_kill(extractor_threads_[i_socket].native_handle(), SIGTERM);
        }
        if(processor_thread_active_.test()) pthread_kill(processor_thread_.native_handle(), SIGTERM);
        if(command_thread_active_.test()) pthread_kill(command_thread_.native_handle(), SIGTERM);

        return *this;
    }

    bool daq::network_thread_active(unsigned int i_stream) const
    {
        if(i_stream>=config::max_boards) return false;
        return network_threads_active_[i_stream].test();
    }
    bool daq::extractor_thread_active(unsigned int i_stream) const
    {
        if(i_stream>=config::max_boards) return false;
        return extractor_threads_active_[i_stream].test();
    }
    bool daq::processor_thread_active() const
    {
        return processor_thread_active_.test();
    }

    unsigned int daq::network_threads() const
    {
        unsigned int nthreads = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test()) ++nthreads;
        }
        return nthreads;
    }
    unsigned int daq::extractor_threads() const
    {
        unsigned int nthreads = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(extractor_threads_active_[i_socket].test()) ++nthreads;
        }
        return nthreads;
    }
    unsigned int daq::processor_threads() const
    {
        if(processor_thread_active_.test()) return 1;
        return 0;
    }

    /*
    tuple<unsigned int, unsigned int, unsigned int> daq::status() const
    {
        unsigned int active_network_threads = 0;
        unsigned int active_extractor_threads = 0;
        unsigned int active_processor_thread = 0;
        for(unsigned int i_socket=0; i_socket<sockets_.size(); ++i_socket)
        {
            if(network_threads_active_[i_socket].test()) ++active_network_threads;
            if(extractor_threads_active_[i_socket].test()) ++active_extractor_threads;
            if(processor_thread_active_.test()) active_processor_thread = 1;
        }
        return {active_network_threads, active_extractor_threads, active_processor_thread};
    }
    */

    size_t daq::received_packets(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size() || network_buffers_[i_stream]==0) return 0;
        return network_buffers_[i_stream]->push_counter();
    }
    size_t daq::lost_packets(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size() || network_buffers_[i_stream]==0) return 0;
        return network_buffers_[i_stream]->lost_packets();
    }

    size_t daq::network_buffer_content(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size() || network_buffers_[i_stream]==0) return 0;
        return network_buffers_[i_stream]->size();
    }
    size_t daq::max_network_buffer_content(unsigned int i_stream) const
    {
        if(i_stream>=network_buffers_.size() || network_buffers_[i_stream]==0) return 0;
        return network_buffers_[i_stream]->max_size();
    }

    size_t daq::channel_buffer_content(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_.size() || all_channels_[i_channel]==0) return 0;
        return all_channels_[i_channel]->size();
    }
    size_t daq::max_channel_buffer_content(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_.size() || all_channels_[i_channel]==0) return 0;
        return all_channels_[i_channel]->max_size();
    }
    size_t daq::channel_buffer_content_of_board(unsigned int i_adc) const
    {
        if(i_adc>=network_buffers_.size()) return 0;
        if(i_adc > board_last_enabled_channel_.size())
        {
            cerr<<"It seems that no channels are enabled for this board"<<endl;
            return 0;
        }
        if(board_last_enabled_channel_[i_adc] == 0)
        {
            cerr<<"It seems that no channels are enabled for this board"<<endl;
            return 0;
        }
        return board_last_enabled_channel_[i_adc]->size();
    }
    size_t daq::max_channel_buffer_content_of_board(unsigned int i_adc) const
    {
        if(i_adc>=network_buffers_.size() || board_last_enabled_channel_[i_adc]==0) return 0;
        return board_last_enabled_channel_[i_adc]->max_size();
    }
    
    size_t daq::extracted_shots(unsigned int i_channel) const
    {
        if(i_channel>=all_channels_.size() || all_channels_[i_channel]==0) return 0;
        return all_channels_[i_channel]->push_counter();
    }
    size_t daq::extracted_shots_of_board(unsigned int i_adc) const
    {
        if(i_adc>=board_last_enabled_channel_.size() || board_last_enabled_channel_[i_adc]==0) return 0;
        return board_last_enabled_channel_[i_adc]->push_counter();
    }


    /*
    tuple<size_t,size_t> daq::statistics() const
    {
        size_t n_packets = 0;
        for(unsigned int i=0; i<network_buffers_.size(); ++i)
        {
            const auto c = network_buffers_[i]->push_counter();
            if(i==0 || c<n_packets) n_packets = c;
        }

        size_t n_shots = 0;
        for(unsigned int i=0; i<all_enabled_channels_buffers_.size(); ++i)
        {
            const auto c = all_enabled_channels_buffers_[i]->push_counter();
            if(i==0 || c<n_shots) n_shots = c;
        }

        return {n_packets,n_shots};
    }
    */

    daq &daq::clear_processors()
    {
        for(auto p : processors_) delete p;
        processors_.clear();
        return *this; 
    }

    void daq::diskdump_pause()
    {
        for(auto p : processors_)
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->pause();
        }
    }
    void daq::diskdump_resume()
    {
        for(auto p : processors_)
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->resume();
        }
    }

    void daq::diskdump_sampling(unsigned int s)
    {
        // Set the default write sampling to the given value
        processor_diskdump::default_sampling(s);
        
        // Also set the value for all existing instances of processor_diskdump
        for(auto p : processors_) 
        {
            if(processor_diskdump *d = dynamic_cast<processor_diskdump*>(p)) d->sampling(s);
        }
    }

    template daq &daq::stop<safe>(unsigned int timeout_sec);
    template daq &daq::stop<unsafe>(unsigned int timeout_sec);
    template daq &daq::start<safe>(bool);
    template daq &daq::start<unsafe>(bool);
    template daq &daq::init<safe>  ();
    template daq &daq::init<unsafe>();
}


#ifdef FOR_PYTHON
extern "C"
{
    using namespace apdcam10g;

    void start_cmd_thread()
    {
        try
        {
            daq::instance().start_cmd_thread();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled exception"<<endl; }
    }
    void stop_cmd_thread()
    {
        try
        {
            daq::instance().stop_cmd_thread();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled exception"<<endl; }
            
    }

    unsigned int n_adc()
    {
        return daq::instance().n_adc();
    }
    unsigned int n_channels()
    {
        return daq::instance().n_channels();
    }

    void         start(bool wait) 
    { 
        try
        {
            daq::instance().start(wait); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled exception"<<endl; }
    }
    void         stop(bool wait) 
    { 
        try
        {
            daq::instance().stop(wait); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }
    void         kill_all() 
    { 
        try
        {
            daq::instance().kill(); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }
    void         version(apdcam10g::version v) 
    { 
        try
        {
            daq::instance().fw_version(apdcam10g::version(v)); 
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }
    void         dual_sata(bool d) { daq::instance().dual_sata(d); }


    void channel_masks(bool **m, int n_adc_boards)
    {
        try
        {
            std::vector<std::vector<bool>> chmasks(n_adc_boards);
            for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
            {
                for(unsigned int i_board_channel=0; i_board_channel<config::channels_per_board; ++i_board_channel)
                {
                    chmasks[i_adc_board].push_back(m[i_adc_board][i_board_channel]);
                }
            }
            daq::instance().channel_masks(chmasks);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }

    void resolution_bits(unsigned int *r, int n_adc_boards)
    {
        try
        {
            std::vector<unsigned int> res(n_adc_boards);
            for(unsigned int i_adc_board=0; i_adc_board<n_adc_boards; ++i_adc_board)
            {
                res[i_adc_board] = r[i_adc_board];
            }
            daq::instance().resolution_bits(res);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }

    void init(bool is_safe)
    {
        try
        {
            if(is_safe) daq::instance().init<safe>();
            else        daq::instance().init<unsafe>();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    }

    void network_buffer_size(unsigned int bufsize)
        try
        {
            daq::instance().network_buffer_size(bufsize);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }
    void channel_buffer_size(unsigned int bufsize)
        try
        {
            daq::instance().channel_buffer_size(bufsize);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }

    unsigned int get_network_buffer_size()
    {
        return daq::instance().network_buffer_size();
    }
    unsigned int get_channel_buffer_size()
    {
        return daq::instance().channel_buffer_size();
    }

    unsigned int get_mtu()
    {
        return daq::instance().mtu();
    }
    unsigned int get_octet()
    {
        return daq::instance().octet();
    }


    void debug(bool d)
    {
        daq::instance().debug(d);
    }

    void add_processor_diskdump(unsigned int process_period, unsigned int sampling)
    {
        try
        {
            auto d = new processor_diskdump();
            d->period(process_period);
            d->sampling(sampling);
            daq::instance().add_processor(d);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            
    }

    void add_processor_python()
    {
        try
        {
            daq::instance().add_processor(new processor_python);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            
    }

    void write_settings(const char *filename)
    {
        try
        {
            daq::instance().write_settings(filename);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            
    }

    void interface(const char *i)
    {
        try
        {
            daq::instance().interface(i);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            
    }

    void wait_finish()
        try
        {
            daq::instance().wait_finish();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            

    void dump()
    {
        try
        {
            daq::instance().dump();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }            
    }

    void get_buffer(unsigned int absolute_channel_number, unsigned int *buffersize, apdcam10g::data_type **buffer)
    {
        try
        {
            auto b = daq::instance().channel_buffer(absolute_channel_number);
            if(b)
            {
                *buffersize = b->capacity();
                *buffer = b->raw_buffer();
                return;
            }
            *buffersize = 0;
            *buffer = 0;
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
    }

    void python_analysis_wait_for_data(size_t *from_counter, size_t *to_counter)
    {
        try
        {
            daq::instance().python_analysis_wait_for_data(from_counter, to_counter);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
    }

    void python_analysis_done(size_t from_counter)
    {
        try
        {
            daq::instance().python_analysis_done(from_counter);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
    }

    void test()
    {
        cerr<<"daq::test"<<endl;
    }

    size_t received_packets(unsigned int i_stream)
    {
        return daq::instance().received_packets(i_stream);
    }
    size_t lost_packets(unsigned int i_stream)
    {
        return daq::instance().lost_packets(i_stream);
    }
    size_t network_buffer_content(unsigned int i_stream)
    {
        return daq::instance().network_buffer_content(i_stream);
    }
    size_t max_network_buffer_content(unsigned int i_stream)
    {
        return daq::instance().max_network_buffer_content(i_stream);
    }
    size_t channel_buffer_content(unsigned int i_channel)
    {
        return daq::instance().channel_buffer_content(i_channel);
    }
    size_t max_channel_buffer_content(unsigned int i_channel)
    {
        return daq::instance().max_channel_buffer_content(i_channel);
    }
    size_t channel_buffer_content_of_board(unsigned int i_adc)
    {
        return daq::instance().channel_buffer_content_of_board(i_adc);
    }
    size_t max_channel_buffer_content_of_board(unsigned int i_adc)
    {
        return daq::instance().max_channel_buffer_content_of_board(i_adc);
    }
    size_t extracted_shots(unsigned int i_channel)
    {
        return daq::instance().extracted_shots(i_channel);
    }
    size_t extracted_shots_of_board(unsigned int i_adc)
    {
        return daq::instance().extracted_shots_of_board(i_adc);
    }

    unsigned int network_threads()
    {
        return daq::instance().network_threads();
    }
    unsigned int extractor_threads()
    {
        return daq::instance().extractor_threads();
    }
    unsigned int processor_threads()
    {
        return daq::instance().processor_threads();
    }
    
    bool network_thread_active(unsigned int i_stream)
    {
        return daq::instance().network_thread_active(i_stream);
    }
    bool extractor_thread_active(unsigned int i_stream)
    {
        return daq::instance().extractor_thread_active(i_stream);
    }
    bool processor_thread_active()
    {
        return daq::instance().processor_thread_active();
    }
/*
    void statistics(unsigned int *n_packets, unsigned int *n_shots)
        try
        {
            auto [np,ns] = daq::instance().statistics();
            *n_packets = np;
            *n_shots   = ns;
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
    

    void status(unsigned int *n_active_network_threads, unsigned int *n_active_extractor_threads, unsigned int *n_active_processor_thread)
        try
        {
            auto [net,ext,proc] = daq::instance().status();
            *n_active_network_threads = net;
            *n_active_extractor_threads = ext;
            *n_active_processor_thread = proc;
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
*/  

    void clear_processors()
        try
        {
            daq::instance().clear_processors();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    

  void default_output_dir(const char *dirname)
    try
    {
      processor_diskdump::default_output_dir(dirname);
    }
    catch(apdcam10g::error &e) {e.print();}
    catch(...) { cerr<<"Unhandled expection"<<endl; }    
  

    void diskdump_sampling(unsigned int s)
        try
        {
            daq::instance().diskdump_sampling(s);
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
    

    bool python_analysis_stop()
    {
        try
        {
            return daq::instance().python_analysis_stop();
        }
        catch(apdcam10g::error &e) {e.print();}
        catch(...) { cerr<<"Unhandled expection"<<endl; }    
        return false;
    }

}





#endif
