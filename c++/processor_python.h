#ifndef __APDCAM10G_PROCESSOR_PYTHON_H__
#define __APDCAM10G_PROCESSOR_PYTHON_H__

#include "processor.h"

namespace apdcam10g
{
    class processor_python : public processor
    {
    public:

        // The 'run' method simply raises the semaphore for the next python analysist task, and then waits
        //until the python code releases the same semaphore. It then returns the value that the python
        // code communicated back to us, the counter of the next shot needed. 
        // The mentioned semaphore (hidden inside the daq class) is common among eventual multiple python tasks.
        // The correspondence is realized by the fact that for each python analysis task in the python code
        // there is a corresponding processor_python class created on the C++ side, and the python and C++ codes
        // loop over these sequences in sync.
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
