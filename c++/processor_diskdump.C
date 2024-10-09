#include "processor_diskdump.h"
#include "daq.h"

namespace apdcam10g
{
    unsigned int processor_diskdump::default_sampling_ = 1;
    std::filesystem::path processor_diskdump::default_output_dir_ = ".";

    void processor_diskdump::init()
    {
          // Close all files, if there are any
          for(auto &f : files_) f.close();
          files_.clear();

          // Create a file for each enabled channel
          files_.resize(daq_->all_enabled_channels_buffers_.size());

          // Reset the pause flag to false.
          pause_.clear();
          previous_pause_ = false;

          // Open the files for each channel
          auto p = filename_pattern_.find('%');
          if(p==string::npos) APDCAM_ERROR("The filename pattern does not contain the character %");
          for(unsigned int i=0; i<daq_->all_enabled_channels_buffers_.size(); ++i)
          {
              const std::filesystem::path filename =
                  output_dir_ / 
                  filename_pattern_.substr(0,p) + 
                  std::to_string(daq_->all_enabled_channels_buffers_[i]->absolute_channel_number) +
                  filename_pattern_.substr(p+1);
              files_[i].open(filename);
          }
          // The next data (shot) to be processed is #0
          next_data_ = 0;
    }

    size_t processor_diskdump::run(size_t from_counter, size_t to_counter)
    {
          size_t start = std::max(from_counter, next_data_);
          for(size_t i=start; i<to_counter; ++i)
          {
              const bool p = pause_.test(std::memory_order_acquire);
              if(p != previous_pause_)
              {
                  // If we have just changed into paused mode, add an empty line to the file
                  if(p) for(auto &f : files_) f<<endl;
                  // Otherwise add a comment with the shot number
                  else for(auto &f : files_) f<<"# resume: "<<i<<endl;
                  previous_pause_ = p;
              }

              // Write out only if we are not paused
              if(!p)
              {
                  // Load the sampling value in each loop because it may change while we are running.
                  // Skip those shots which are not to be written
                  const unsigned int s = sampling_.load(std::memory_order_seq_cst);
                  if(i%s != 0) continue;
                  
                  for(unsigned int i_enabled_channel=0; i_enabled_channel<daq_->all_enabled_channels_buffers_.size(); ++i_enabled_channel)
                  {
                      daq::channel_data_buffer_t *c = daq_->all_enabled_channels_buffers_[i_enabled_channel];
                      files_[i_enabled_channel] << (*c)(i)<<endl;
                  }
              }
          }
          return (next_data_ = to_counter);
    }
    

}
