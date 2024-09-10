#ifndef __APDCAM10G_FAKE_CAMERA_H__
#define __APDCAM10G_FAKE_CAMERA_H__

#include "daq_settings.h"
#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <functional>

namespace apdcam10g
{
    class fake_camera : public daq_settings
    {
    private:
        std::string server_ip_;
        std::function<bool(unsigned int)> packet_filter_ = [](unsigned int) { return true; };
    public:
        fake_camera &server_ip(const std::string &ip) { server_ip_ = ip; return *this; }
        void send(int n, bool wait=true);
        void packet_filter(std::function<bool(unsigned int)> filter) { packet_filter_ = filter; }
    };

}


#endif
