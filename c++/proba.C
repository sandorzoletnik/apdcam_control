#include <atomic>
#include <thread>
#include <string>
#include <string.h>
#include <iostream>
#include <fstream>
#include <bit>
#include <stdint.h>
#include <algorithm>
#include <tuple>
#include <deque>
#include <mutex>
#include <map>
#include <vector>
#include <iostream>
#include <string.h>
#include "stopper.h"
#include "error.h"
#include "terminal.h"
#include "bytes.h"
#include "settings.h"

using namespace std;
using namespace apdcam10g;
using namespace terminal;

int main()
try
{
    std::atomic<int> i;
    int expected = 12;
    i = 12;
    cerr<<i.compare_exchange_weak(expected,19,std::memory_order_seq_cst,std::memory_order_seq_cst)<<endl;
    cerr<<i<<endl;
    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
