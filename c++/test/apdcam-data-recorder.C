#include "daq.h"
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

using namespace apdcam10g;
using namespace std;

daq the_daq;

void help()
{
    cout<<"Usage: apdcam-data-recorder [options]"<<endl<<endl;
    cout<<"  -i <interface>                   Set the network interface. Defaults to 'lo'"<<endl;
    cout<<"  -s|--sample-buffer <interface>   Set the sample buffer size. Must be power of 2. Defaults to "<<the_daq.sample_buffer_size()<<endl;
    cout<<"  -n|--network-buffer <interface>  Set the network ring buffer size in terms of UDP packets. Must be power of 2. Defaults to "<<the_daq.network_buffer_size()<<endl;
    cout<<endl;
    cout<<"Upon starting it will create a file 'settings.json' that can be read by the fake camera using the -s command line argument"<<endl;
    exit(0);
}



void flush_output(int sig)
{
    cerr<<"Flushing outputs.."<<endl;
    std::this_thread::sleep_for(1s);
    the_daq.finish();
    cerr<<"DONE"<<endl;
    std::this_thread::sleep_for(1s);
    signal (sig, SIG_DFL);
    raise (sig);
}

int main(int argc, char *argv[])
try
{

    string ld_library_path = getenv("LD_LIBRARY_PATH");
    ld_library_path += ":..";
    setenv("LD_LIBRARY_PATH",ld_library_path.c_str(),1);

    signal(SIGINT,flush_output);

    for(unsigned int opt=1; opt<argc; ++opt)
    {
        if(!strcmp(argv[opt],"-h") || !strcmp(argv[opt],"--help")) help();
        else if(!strcmp(argv[opt],"-i"))
        {
            if(opt+1>=argc) APDCAM_ERROR("Missing argument (interface) after -i");
            the_daq.interface(argv[++opt]);
        }
        else if(!strcmp(argv[opt],"-s") || !strcmp(argv[opt],"--sample-buffer"))
        {
            if(opt+1>=argc) APDCAM_ERROR(std::string("Missing argument (buffer size) after ") + argv[opt]);
            the_daq.sample_buffer_size(atoi(argv[++opt]));
        }
        else if(!strcmp(argv[opt],"-n") || !strcmp(argv[opt],"--network-buffer"))
        {
            if(opt+1>=argc) APDCAM_ERROR(std::string("Missing argument (buffer size) after ") + argv[opt]);
            the_daq.network_buffer_size(atoi(argv[++opt]));
        }
        else APDCAM_ERROR(std::string("Bad argument: ") + argv[opt]);
    }

    the_daq.get_net_parameters();

    the_daq.init(false,{{true,true,true,false,false,false,false,false,
                         true,true,true,false,false,false,false,false,
                         true,true,true,false,false,false,false,false,
                         true,true,true,false,false,false,false,false}}, {14}, v2);

    the_daq.write("settings.json");

//    the_daq.print_channel_map();

    cerr<<"Starting..."<<endl;
    the_daq.start(true);

    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e.full_message()<<endl;
}
