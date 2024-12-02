#include "tee.h"

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
#include <signal.h>
#include "stopper.h"
#include "error.h"
#include "terminal.h"
#include "bytes.h"
#include "settings.h"

#include <sys/types.h>
#include <sys/stat.h>


using namespace std;
using namespace apdcam10g;
using namespace terminal;


void signal_handler(int signum)
{
    std::cerr<<std::stacktrace::current()<<std::endl<<std::endl;
}



int main()
try
{
    signal(SIGSEGV,signal_handler);

    tee(std::cout, "cout-duplum.txt");

    /*
    std::streambuf *b1 = std::cout.rdbuf();
    std::ofstream file("file.txt");
    std::streambuf *b2 = file.rdbuf();
    std::streambuf *teebuf = new teestream(b1,b2);
    std::cout.rdbuf(teebuf);
    */

    std::cout<<"Hello world   "<<123<<endl;

    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
