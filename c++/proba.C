#include <thread>
#include <string>
#include <string.h>
#include <iostream>
#include <bit>
#include <stdint.h>
#include <tuple>
#include <deque>
//#include "error.h"
//#include "ring_buffer.h"
//#include "safe_semaphore.h"
#include <string.h>
using namespace std;
//using namespace apdcam10g;

class empty
{};

class A : public empty
{
public:
    int i;
};

int main()
{
    A a;
    cerr<<sizeof(A)<<endl;

    return 0;
}
