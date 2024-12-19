#include "daq.h"
#include "arg.h"
#include "processor_diskdump.h"
#include "processor_test_pattern_match.h"
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
    cout<<"  --help-commands                  Print a help about the possible commands (given after the -c switch)"<<endl;
    cout<<"  -i|--interface <interface>       Set the network interface. Defaults to 'lo'"<<endl;
    cout<<"  -c <command ...>                 Send a command to a running APDCAM DAQ process. The rest of the command"<<endl;
    cout<<"                                   line arguments is interpreted as the command and is simply written"<<endl;
    cout<<"                                   into the named pipe ~/.apdcam10g/cmd"<<endl;
    cout<<"                                   For a list of available commands run apdcam-daq --help-commands"<<endl;
    cout<<"  -k|--kill                        kill the running apdcam DAQ process (if there is any), the PID of which is in ~/.apdcam10g/pid"<<endl;
    cout<<"  -d directory                     Specify the output directory for diskdump (i.e. where the per-channel data is written)"<<endl;
    cout<<"  -s|--sample-buffer <interface>   Set the sample buffer size. Must be power of 2. Defaults to "<<daq::instance().channel_buffer_size()<<endl;
    cout<<"  -n|--network-buffer <interface>  Set the network ring buffer size in terms of UDP packets. Must be power of 2. Defaults to "<<daq::instance().network_buffer_size()<<endl;
    cout<<"  -D|--debug                       Set debug mode"<<endl;
    cout<<"  -t|--test-pattern <number>       Set a test pattern matcher. Possible values are those which are selectable on the APDCAM10G device: "<<endl;
    cout<<"                                   6 - Short pseudo-number sequence"<<endl;
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
    signal(SIGINT,flush_output);

    int test_pattern = -1;

    for(args a(argc,argv); a; ++a)
    {
        cerr<<"arg: "<<a()<<endl;
        if(a()=="-h" || a()=="--help") { help(); exit(0); }
        else if(a()=="--help-commands") { daq::cmd_help(); exit(0); }
        else if(a()=="-c")
        {
            auto fifo_name = configdir() / "cmd";
            if(!std::filesystem::is_fifo(fifo_name)) APDCAM_ERROR("No apdcam data acquisition process seems to be running. The FIFO '" + fifo_name + "' does not exist");
            ofstream fifo(fifo_name);
            for(++a; a; ++a) fifo<<" "<<a();
            fifo<<endl;
            exit(0);
        }
        else if(a()=="-k" || a()=="--kill")
        {
            auto pid_file_name = configdir() / "pid";
            ifstream pid_file(pid_file_name);
            if(!pid_file.good()) APDCAM_ERROR(std::string("Did not find PID file: ") + pid_file_name.string());
            pid_t pid;
            pid_file>>pid;
            kill(pid,SIGKILL);
            exit(0);
        }
        else if(a()=="-d")                           processor_diskdump::default_output_dir(a.get<std::string>(1,"Directory name expected after -d"));
        else if(a()=="-i" || a()=="--interface")     daq::instance().interface(a.get<std::string>(1,"Interface name"));
        else if(a()=="-s" || a()=="--sample-buffer") daq::instance().channel_buffer_size(a.get<int>(1,"Buffer size"));
        else if(a()=="-n" || a()=="--network-buffer") daq::instance().network_buffer_size(a.get<int>(1,"Buffer size"));
        else if(a()=="-D" || a()=="--debug")          daq::instance().debug(true);
        else if(a()=="-t" || a()=="--test-pattern") test_pattern = a.get<int>(1,"Test pattern number");
        else APDCAM_ERROR(std::string("Bad argument: ") + a());
    }

    pseudo_random_short prs;

    daq::instance().add_processor(new processor_test_pattern_match(&prs));

    if(test_pattern>0)
    {
        switch(test_pattern)
        {
            case 6:
                daq::instance().add_processor(new processor_diskdump); break;
            default:
                APDCAM_ERROR("Bad test pattern specified");
        }
    }
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
    daq::instance().start_cmd_thread();
    sleep(1);

//    daq::instance().print_channel_map();
    daq::instance().start(true);

    daq::instance().stop_cmd_thread();

    return 0;
}
catch(apdcam10g::error &e)
{
    cerr<<e.full_message()<<endl;
}
