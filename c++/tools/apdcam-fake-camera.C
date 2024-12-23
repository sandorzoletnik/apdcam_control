#include "fake_camera.h"
#include "error.h"
#include <iostream>
#include <string.h>
#include "utils.h"
#include "test_pattern.h"
#include "args.h"

using namespace std;

void help()
{
    cout<<"Usage: fake-camera [options]"<<endl<<endl;
    cout<<" -i SERVER_IP             Specify the IP address of the server which receives the data (defaults to 127.0.0.1 [localhost])"<<endl;
    cout<<" -n NUMBER_OF_SHOTS       Specify the number of shots to be sent (defaults to 100)"<<endl;
    cout<<" -s FILE                  Read settings from the specified file (must have been exported by apdcam-data-recorder)"<<endl;
    cout<<" -t|--test-pattern <int>  Set the test pattern. "<<endl;
    cout<<" --drop-packets <n>       Drop every nth packet to simulate packet loss"<<endl;
    exit(0);
}

using namespace apdcam10g;

int main(int argc, char *argv[])
try
{

    apdcam10g::fake_camera cam;
    cam.server_ip("127.0.0.1");
    int nshots = 100;
    bool settings_ok = false;

    for(args a(argc,argv); a; ++a)
    {
        if(a("-h","--help"))  help();
        else if(a("-i")) cam.server_ip(a.get<std::string>(1,"Server IP"));
        else if(a("-n")) nshots = a.get<int>(1,"Number of shots");
        else if(a("-s")) settings_ok = cam.read_settings(a.get<std::string>(1,"Settings filename"));
        else if(a("--drop-packets"))
        {
            const unsigned int drop_packets = a.get<int>(1);
            cam.packet_filter([drop_packets](unsigned int packet_no) { if((packet_no+1)%drop_packets==0) return false; return true; });
        }
        else if(a("--drop-shots"))
        {
            const unsigned int drop_shots = a.get<int>(1);
            cam.skip_shots([drop_shots](unsigned int shot_no) -> unsigned int { if((shot_no+1)%drop_shots==0) return 1; return 0; });
        }
        else if(a("-t","--test-pattern")) cam.test_pattern(a.get<int>(1,"Test pattern number"));
        else APDCAM_ERROR("Unknown argument: " + a());
    }
    if(!settings_ok) 
    {
        auto settings_filename = apdcam10g::configdir() / "daq.cnf";
        cerr<<"Trying to read settings from default file: "<<settings_filename<<endl;
        cam.read_settings(settings_filename);
    }

    cam.send(nshots);
    return 0;
}
catch(const apdcam10g::error &e)
{
    cerr<<e.full_message()<<endl;
}
