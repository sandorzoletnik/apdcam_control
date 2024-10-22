#include "daq.h"
#include "processor_diskdump.h"
#include "shot_data_layout.h"
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <filesystem>

using namespace apdcam10g;
using namespace std;

void help()
{
    cout<<"Usage: apdcam-data-recorder [options]"<<endl<<endl;
    cout<<"  -i <interface>                   Set the network interface. Defaults to 'lo'"<<endl;
    cout<<"  -c <command ...>                 Send a command to a running APDCAM DAQ process. The rest of the command"<<endl;
    cout<<"                                   line arguments is interpreted as the command and is simply written"<<endl;
    cout<<"                                   into the named pipe ~/.apdcam10g/cmd"<<endl;
    cout<<"  -k|--kill                        kill the running apdcam DAQ process (if there is any), the PID of which is in ~/.apdcam10g/pid"<<endl;
    cout<<"  -d directory                     Specify the output directory for diskdump (i.e. where the per-channel data is written)"<<endl;
    cout<<"  -s|--sample-buffer <interface>   Set the sample buffer size. Must be power of 2. Defaults to "<<daq::instance().sample_buffer_size()<<endl;
    cout<<"  -n|--network-buffer <interface>  Set the network ring buffer size in terms of UDP packets. Must be power of 2. Defaults to "<<daq::instance().network_buffer_size()<<endl;
    cout<<"  -d                               Set debug mode"<<endl;
    cout<<endl;
    cout<<"Upon starting it will create a file 'settings.json' that can be read by the fake camera using the -s command line argument"<<endl;
    exit(0);
}



void flush_output(int sig)
{
    cerr<<"Flushing outputs.."<<endl;
    std::this_thread::sleep_for(1s);
    daq::instance().finish();
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
        else if(!strcmp(argv[opt],"-c"))
        {
            auto fifo_name = configdir() / "cmd";
            if(!std::filesystem::is_fifo(fifo_name)) 
            {
                cerr<<"No apdcam data acquisition process seems to be running. The FIFO "<<fifo_name<<" does not exist"<<endl;
                exit(1);
            }
            ofstream fifo(fifo_name);
            for(int i=opt+1; i<argc; ++i)
            {
                if(i>opt+1) fifo<<" ";
                fifo<<argv[i];
            }
            fifo<<endl;
        }
        else if(!strcmp(argv[opt],"-k") || !strcmp(argv[opt],"--kill"))
        {
            auto pid_file_name = configdir() / "pid";
            ifstream pid_file(pid_file_name);
            if(!pid_file.good()) APDCAM_ERROR(std::string("Did not find PID file: ") + pid_file_name.string());
            pid_t pid;
            pid_file>>pid;
            kill(pid,SIGKILL);
            
        }
        else if(!strcmp(argv[opt],"-d"))
        {
            if(opt+1>=argc) APDCAM_ERROR("Directory name expected after -d");
            processor_diskdump::default_output_dir(argv[++opt]);
        }
        else if(!strcmp(argv[opt],"-i"))
        {
            if(opt+1>=argc) APDCAM_ERROR("Missing argument (interface) after -i");
            daq::instance().interface(argv[++opt]);
        }
        else if(!strcmp(argv[opt],"-s") || !strcmp(argv[opt],"--sample-buffer"))
        {
            if(opt+1>=argc) APDCAM_ERROR(std::string("Missing argument (buffer size) after ") + argv[opt]);
            daq::instance().sample_buffer_size(atoi(argv[++opt]));
        }
        else if(!strcmp(argv[opt],"-n") || !strcmp(argv[opt],"--network-buffer"))
        {
            if(opt+1>=argc) APDCAM_ERROR(std::string("Missing argument (buffer size) after ") + argv[opt]);
            daq::instance().network_buffer_size(atoi(argv[++opt]));
        }
        else if(!strcmp(argv[opt],"-d")) daq::instance().debug(true);
        else APDCAM_ERROR(std::string("Bad argument: ") + argv[opt]);
    }

    daq::instance().get_net_parameters();
    daq::instance().add_processor(new processor_diskdump);
    daq::instance().resolution_bits({14});
    daq::instance().channel_masks(
        {
            {
                true,true,true,true,false,false,false,false,
                true,true,true,false,false,false,false,false,
                true,true,true,false,false,false,false,false,
                true,true,true,false,false,false,false,false
            }
        });

    daq::instance().init();

//    daq::instance().write_settings("apdcam-daq.cnf");

//    daq::instance().print_channel_map();

    daq::instance().start(true);

    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e.full_message()<<endl;
}
