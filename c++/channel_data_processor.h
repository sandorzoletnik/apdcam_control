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
        channel_data_processor(daq *d) : daq_(d) {}

        // Initialization function called before starting the data acquisition
        virtual void init(daq *d) {}

        // The actual analysis task.
        // Arguments:
        // from_counter, to_counter -- the range of data counters [inclusive] (see ring_buffer.h about the counters) which
        //                             is guaranteed to be available within the ring buffers of all channels
        // Returns:
        // The counter of the data, up to which (inclusive) channel signal data from ALL ring buffers can be dicarded
        virtual unsigned int run(unsigned int from_counter, unsigned int to_counter) = 0;
        
    };
}



#endif

