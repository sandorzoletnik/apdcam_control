#ifndef __APDCAM10G_PROCESSOR_PYTHON_H__
#define __APDCAM10G_PROCESSOR_PYTHON_H__

#include "processor.h"

namespace apdcam10g
{
    class processor_python : public processor
    {
    public:
        virtual size_t run(size_t from_counter, size_t to_counter)
        {
            // Set the flag signaling the python code that it can run
            daq::instance().python_analysis_start(from_counter, to_counter);

            // Now wait until the python analysis task finishes
            return daq::instance().python_analysis_wait_finish();
        }
    };
}

#endif
