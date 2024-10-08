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


    class signal2exception
    {
    private:
        static std::map<std::jthread::id,std::string> thread_names_;

        // The signal-handler routine which can be set for POSIX signals. It simply throws an apdcam_error exception
        // with the appropriate message
        static void run(int signum)
            {
                APDCAM_ERROR("Signal " + std::to_string(signum) + " is caught");
                //signal (signum, SIG_DFL);
                //raise (signum);
            }


    public:

        // The below vararg 'set' functions can be used to set the signal handler for the given signals.
        // The first argument is a name that will be associated with the calling thread for the user's
        // convenience, subsequent integer args (any number of them) are signal numbers
        // Usage: signal2exception::set("my_thread_name",SIGSEGV,SIGINT);

        static void set(const std::string &thread_name, int signum)
            {
                thread_names_[std::this_thread::get_id()] = thread_name;
                signal(signum,signal2exception::run);
            }

        template <typename... SIGNUMS>
        static void set(const std::string &thread_name, int signum1, SIGNUMS... signums)
            {
                signal(signum1,signal2exception::run);
                set(thread_name,signums...);
            }
    };

    std::map<std::jthread::id,std::string> signal2exception::thread_names_;



class flag_locker
{
private:
    std::atomic_flag *flag_;
public:
    flag_locker(std::atomic_flag &flag) : flag_(&flag) {flag.test_and_set();}
    ~flag_locker() {flag_->clear();}
};


int main()
try
{
    unlink("the_fifo");
    mkfifo("the_fifo",0666);

    while(true)
    {
        ifstream file("the_fifo");
        string line;
        cerr<<"Start"<<endl;

        while((cerr<<"waiting for input..."), getline(file,line))
        {
            cerr<<line<<endl;
            if(line=="quit") return 0;
        }
    }

    return 0;


    std::atomic_flag running;

    auto t = std::jthread([&running]()
        {
            signal2exception::set("The thread",SIGSEGV,SIGTERM,SIGKILL);
            try
            {
                flag_locker flk(running);
                sleep(100);
            }
            catch(apdcam10g::error &e)
            {
                cerr<<e.full_message()<<endl;
            }
        });

    sleep(1);
    cerr<<"Running: "<<running.test()<<endl;
    pthread_kill(t.native_handle(),SIGTERM);
    cerr<<"EJNYE"<<endl;
    sleep(1);
    cerr<<"Running: "<<running.test()<<endl;

    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e<<endl;
}
