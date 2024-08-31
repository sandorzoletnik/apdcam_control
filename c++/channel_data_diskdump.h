#ifndef __APDCAM10G__CHANNEL_DATA_DISKDUMP_H__
#define __APDCAM10G__CHANNEL_DATA_DISKDUMP_H__

#include "signal_processor.h"
#include "ring_buffer.h"
#include "config.h"

namespace apdcam10g
{
  class channel_data_diskdump : public channel_data_processor
  {
  private:
      std::vector<std::ofstream> files_;
      size_t next_data_ = 0;
  public:
      channel_data_diskdump(daq *d) : channel_data_processor(d) {}

      void init()
      {
          files_.clear();
          files_.resize(daq_->enabled_channels());
          for(auto &board_channels : daq_->channel_data_buffers_)
          {
              for(auto &c : board_channels)
              {
                  files_[c.absolute_channel_number].open("channel_data_" + std::to_string(c.absolute_channel_number));
              }
          }
      }

      void finish()
      {
          for(auto &f : files_) f.close();
      }

      size_t run(size_t from_counter, size_t to_counter)
      {
          size_t start = std::max(from_counter, next_data_);
          for(auto &board_channels: daq_->channel_data_buffers_)
          {
              for(auto &c : board_channels)
              {
                  for(size_t i=start; i<to_counter; ++i) files_[c.absolute_channel_number] << c(i)<<endl;
              }
          }
          return (next_data_ = to_counter);
      }
  };
}



#endif
