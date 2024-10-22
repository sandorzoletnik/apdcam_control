#ifndef __APDCAM10G_PROCESSOR_DISKDUMP_H__
#define __APDCAM10G_PROCESSOR_DISKDUMP_H__

#include "processor.h"
#include "ring_buffer.h"
#include "config.h"
#include <fstream>

/*
  A processor class that writes the data of all channels to disk, in separate files.
  It makes no sense to have several such objects (in a complicated 

 */
  
namespace apdcam10g
{
  class processor_diskdump : public processor
  {
  private:
      // The output files, one per channel
      std::vector<std::ofstream> files_;

      // The shot counter pointing to the next shot that has not yet been written to disk
      size_t next_data_ = 0;

      // The filename pattern of the output files. % is replaced by the absolute channel number
      std::string filename_pattern_ = "channel_data_%.dat";

      // The output directory
      std::filesystem::path output_dir_ = ".";

      // The default output dir. All processor_diskdump instances will initialize
      // their corresponding value from this value
      static std::filesystem::path default_output_dir_;

      // Value for sub-sampling the data when writing to disk
      std::atomic<unsigned int> sampling_ = 1;

      // The default value for sub-sampling. All processor_diskdump instances created after
      // setting this value will be initialized with this value.
      static unsigned int default_sampling_;

      // A flag to indicate whether dumping the data to disk should be paused (note that the whole
      // data acquisition framework will keep running in order to keep track of the data layout/splitting
      // among UDP packets)
      std::atomic_flag pause_;

      // Storing the previous value of the 'pause' flag so that we can write markers into the
      // output files whenever we switch between paused/resumed states.
      bool previous_pause_;

  public:
      processor_diskdump() : output_dir_(default_output_dir_)
      {
          name_ = "processor_diskdump";
          sampling_.store(default_sampling_);
      }

      // Set the filename pattern. The pattern must contain the character '%' which is replaced by the
      // absolute channel number. If called, it must be made before 'init()'
      void filename_pattern(const std::string &p)
      {
          filename_pattern_ = p;
      }

      // Set the output directory for the files. If called, it must be made before 'init()'
      void output_dir(const std::filesystem::path &d)
      {
          output_dir_ = d;
      }

      // Set the output directory for the files. All processor_diskdump instances created
      // after calling this function will initialize their corresponding value from this value
      static void default_output_dir(const std::filesystem::path &d)
      {
          default_output_dir_ = d;
      }

      // Set the sampling number. This is used for modulo division of the shot number. If the remainder is zero,
      // the given shot is written to the files
      void sampling(unsigned int s)
      {
          sampling_.store(s,std::memory_order_seq_cst);
      }

      // Set the default sampling value. All instances created after this will have their
      // sampling number initialized from this value
      static void default_sampling(unsigned int s)
      {
          default_sampling_ = s;
      }

      // Initialize the task. Called by the DAQ framework.
      void init();
      
      // Pause dumping data to disk. Note that the DAQ system will keep running in order to keep
      // track of data block boundary layouts, data splitting among UDP packets
      void pause()
      {
          pause_.test_and_set(std::memory_order_release);
      }

      // Resume dumping data to disk
      void resume()
      {
          pause_.clear(std::memory_order_release);
      }

      // Mandatory function which is called by the DAQ system. 
      void finish()
      {
          for(auto &f : files_) f.close();
      }

      // Mandatory function which is called by the DAQ system. It does the actual job
      // of simply writing the channel data into disks.
      size_t run(size_t from_counter, size_t to_counter);
      
  };
}



#endif
