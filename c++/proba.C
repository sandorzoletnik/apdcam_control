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

void print(int i)
{
    cerr<<i<<" ";
}

template <typename...SIGNUMS>
void print(int i, SIGNUMS... signums)
{
    cerr<<i<<" ";
    print(signums...);
}


int main()
try
{
    print(1,2,3,4,5);
    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
