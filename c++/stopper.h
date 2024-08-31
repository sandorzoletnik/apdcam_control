#ifndef __APDCAM10G_STOPPER_H__
#define __APDCAM10G_STOPPER_H__

#include <time.h>
#include <iostream>

namespace apdcam10g
{
    class stopper
    {
    private:
        clock_t start_;
    public:
        stopper(bool start_now = true)
        {
            if(start_now) start();
        }
        void start()
        {
            start_ = clock();
        }
        double operator()() const
        {
            return (clock()-start_)/double(CLOCKS_PER_SEC);
        }
    };

    std::ostream &operator<<(std::ostream &out, stopper s)
    {
        out<<s();
        return out;
    }
}

#endif
