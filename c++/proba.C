
#include <thread>
#include <string>
#include <string.h>
#include <iostream>
#include <bit>
#include <stdint.h>
#include <algorithm>
#include <tuple>
#include <deque>
#include <mutex>
#include <map>
#include <vector>
#include <string.h>
#include "stopper.h"

std::mutex the_mutex;

#define LOCK std::scoped_lock lock(the_mutex)

#include "error.h"
//#include "ring_buffer.h"
//#include "safe_semaphore.h"
//#include "rw_mutex.h"

using namespace apdcam10g;
using namespace std;

#include <iostream>
#include <ranges>


template <typename F>
void call_many_times(F f, int n)
{
    for(int i=0; i<n; ++i) cerr<<f()<<endl;
}

int main()
try
{

    call_many_times([](){static int i=0; return i++;},10);

/*
    const int BUFFERSIZE = 32;
    ring_buffer<int> q(BUFFERSIZE,BUFFERSIZE);

    const int READ_CHUNK = 8;
    const int WRITE_CHUNK = 7;

    // Number of producer threads
    const int NP = 1; 

    // Consume thread
    std::jthread consumer([&q]{
        try
        {
            size_t from=0, to=from+READ_CHUNK;
            while(true)
            {
                auto [p,n] = q(from,to);
                if(p)
                {
                    q.dump();
                    for(int i=0; i<n; ++i)
                    {
                        cerr<<p[i]<<endl;
                    }
                }
                else
                {
                    cerr<<"Queue is terminated"<<endl;
                    break;
                }
                cerr<<endl;
                q.pop_to(to);
                from = from+n;
                to = from+READ_CHUNK;

            }
        }
        catch(apdcam10g::error &e) { cerr<<"Consumer error: "<<e<<endl; }
    });


    std::vector<std::jthread> producers(NP);

    for(int p=0; p<NP; ++p)
    {
        producers[p] = std::jthread([&q,p]{
            try
            {
                for(int i=0; i<10; ++i)
                {
                    for(int j=0; j<WRITE_CHUNK; ++j)
                    {
                        int *p = q.future_element(j);
                        *p = i*WRITE_CHUNK+j;
                    }
                    q.publish(WRITE_CHUNK);
                }
                q.terminate();
            }
            catch(apdcam10g::error &e) { cerr<<"Error in producer: "<<e<<endl; }
        });
    }

    for(int i=0; i<NP; ++i) producers[i].join();
    consumer.join();

*/
    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
