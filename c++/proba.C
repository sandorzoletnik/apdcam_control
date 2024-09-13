
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
#include <iostream>
#include <string.h>
#include "stopper.h"
#include "error.h"
#include "terminal.h"
#include "bytes.h"

using namespace std;
using namespace apdcam10g;
using namespace terminal;


int main()
try
{
    unsigned int i = 257;
    std::byte b = (std::byte)i;
    cerr<<(int)b<<endl;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
