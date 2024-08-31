#ifndef __APDCAM10G_CHANNEL_DATA_PROCESSOR_H__
#define __APDCAM10G_CHANNEL_DATA_PROCESSOR_H__

#include "error.h"
#include "ring_buffer.h"


namespace apdcam10g
{
    class daq;

    class channel_data_processor
    {
    protected:
        daq *daq_ = 0;
        
    public:
        // Initialization function called before starting the data acquisition
        virtual void init() = 0;
        virtual void finish() = 0;

        void set_daq(daq *d) { daq_ = d; }

        // The actual analysis task.
        // Arguments:
        // from_counter, to_counter -- the range of data counters (from_counter - inclusive, to_counter - exclusive)
        //                             which is guaranteed to be available within the ring buffers of all channels
        // Returns:
        // The first counter of the data that is requested to stay in the buffer. The envelopping thread can remove all data
        // before this counter
        virtual size_t run(size_t from_counter, size_t to_counter) = 0;
        
    };
}



#endif

