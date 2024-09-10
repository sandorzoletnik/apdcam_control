#include "fake_camera.h"
#include "error.h"
#include <iostream>
#include <string.h>

using namespace std;

void help()
{
    cout<<"Usage: fake-camera [options]"<<endl<<endl;
    cout<<" -i SERVER_IP        Specify the IP address of the server which receives the data (defaults to 127.0.0.1 [localhost])"<<endl;
    cout<<" -n NUMBER_OF_SHOTS  Specify the number of shots to be sent (defaults to 100)"<<endl;
    cout<<" -s FILE             Read settings from the specified file (must have been exported by apdcam-data-recorder)"<<endl;
    cout<<" --drop-packets <n>  Drop every nth packet to simulate packet loss"<<endl;
    exit(0);
}

int main(int argc, char *argv[])
try
{
    string ld_library_path = getenv("LD_LIBRARY_PATH");
    ld_library_path += ":..";
    setenv("LD_LIBRARY_PATH",ld_library_path.c_str(),1);

    apdcam10g::fake_camera cam;
    cam.server_ip("127.0.0.1");
    int nshots = 100;
    bool settings_ok = false;

    for(int opt=1; opt<argc; ++opt)
    {
        if(!strcmp(argv[opt],"-h") || !strcmp(argv[opt],"--help")) help();
        else if(!strcmp(argv[opt],"-i"))
        {
            if(++opt>=argc) APDCAM_ERROR("IP address required after -i");
            cam.server_ip(argv[opt]);
        }
        else if(!strcmp(argv[opt],"-n"))
        {
            if(++opt>=argc) APDCAM_ERROR("Number of shots expected after -n");
            nshots = atoi(argv[opt]);
        }
        else if(!strcmp(argv[opt],"-s"))
        {
            if(++opt>=argc) APDCAM_ERROR("Settings filename exptected after -s");
            settings_ok = cam.read(argv[opt]);
        }
        else if(!strcmp(argv[opt],"--drop-packets"))
        {
            if(++opt>=argc) APDCAM_ERROR("Number expected after --drop-packets");
            const unsigned int drop_packets = atoi(argv[opt]);
            cam.packet_filter([drop_packets](unsigned int packet_no) { if((packet_no+1)%drop_packets==0) return false; return true; });
        }
        else
        {
            APDCAM_ERROR("Unknown argument: " + std::string(argv[opt]));
        }
    }
    if(!settings_ok) APDCAM_ERROR("Settings have not been specified, or failed to read");

    // Determine the MTU, set package size, etc
    cam.get_net_parameters();
    
    cam.send(nshots);
    return 0;
}
catch(const apdcam10g::error &e)
{
    cerr<<e.full_message()<<endl;
}
