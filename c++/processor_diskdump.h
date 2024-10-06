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
      std::vector<std::ofstream> files_;
      size_t next_data_ = 0;

      // The filename pattern of the output files. % is replaced by the absolute channel number
      std::string filename_pattern_ = "channel_data_%.dat";

      // The output directory
      std::string output_dir_ = ".";

      // A unique value 
      std::atomic<unsigned int> sampling_ = 1;

      static unsigned int default_sampling_;

      std::atomic_flag pause_;
      bool previous_pause_;

  public:
      processor_diskdump() 
      {
          sampling_.store(default_sampling_);
      }

      // Set the filename pattern. The pattern must contain the character '%' which is replaced by the
      // absolute channel number. If called, it must be made before 'init()'
      void filename_pattern(const std::string &p)
      {
          filename_pattern_ = p;
      }

      // Set the output directory for the files. If called, it must be made before 'init()'
      void output_dir(const std::string &d)
      {
          output_dir_ = d;
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

      void init();
      
      void pause()
      {
          pause_.test_and_set(std::memory_order_release);
      }

      void resume()
      {
          pause_.clear(std::memory_order_release);
      }

      void finish()
      {
          for(auto &f : files_) f.close();
      }

      size_t run(size_t from_counter, size_t to_counter);
      
  };
}



#endif
