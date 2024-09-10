#ifndef __APDCAM10G__CHANNEL_DATA_DISKDUMP_H__
#define __APDCAM10G__CHANNEL_DATA_DISKDUMP_H__

#include "channel_data_processor.h"
#include "ring_buffer.h"
#include "config.h"
#include <fstream>

namespace apdcam10g
{
  class channel_data_diskdump : public channel_data_processor
  {
  private:
      std::vector<std::ofstream> files_;
      size_t next_data_ = 0;
  public:
      channel_data_diskdump(daq *d) : channel_data_processor(d) {}
      channel_data_diskdump() {}

      void init()
      {
          files_.clear();
          files_.resize(daq_->all_enabled_channels_buffers_.size());
          for(unsigned int i=0; i<daq_->all_enabled_channels_buffers_.size(); ++i)
          {
              const std::string filename = "channel_data_" + std::to_string(daq_->all_enabled_channels_buffers_[i]->absolute_channel_number) + ".dat";
              files_[i].open(filename);
          }
      }

      void finish()
      {
          for(auto &f : files_) f.close();
      }

      size_t run(size_t from_counter, size_t to_counter)
      {
          size_t start = std::max(from_counter, next_data_);
          for(unsigned int i_enabled_channel=0; i_enabled_channel<daq_->all_enabled_channels_buffers_.size(); ++i_enabled_channel)
          {
              daq::channel_data_buffer_t *c = daq_->all_enabled_channels_buffers_[i_enabled_channel];
              for(size_t i=start; i<to_counter; ++i) files_[i_enabled_channel] << (*c)(i)<<endl;
          }
          return (next_data_ = to_counter);
      }
  };
}



#endif
