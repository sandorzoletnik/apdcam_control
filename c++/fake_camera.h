#ifndef __APDCAM10G_FAKE_CAMERA_H__
#define __APDCAM10G_FAKE_CAMERA_H__

#include "daq_settings.h"

#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <functional>


namespace apdcam10g
{
    

    class fake_camera : public daq_settings<channel_info_with_generator>
    {
    private:
        std::string server_ip_;
        std::function<bool(unsigned int)> packet_filter_ = [](unsigned int) { return true; };
        std::function<unsigned int(unsigned int)> skip_shots_ = [](unsigned int) { return 0; };
        test_pattern_sequence *test_pattern_sequence_ = 0;

        // Loop over all channels and set a value generator for them: either a null pointer (if we
        // are not using a test pattern), or a generator using the test pattern sequence.
        // This function must be called if the test pattern is changed, or if the enabled channels
        // are recalculated
        void set_generators_();

    public:
        fake_camera &server_ip(const std::string &ip) { server_ip_ = ip; return *this; }
        void send(int n, bool wait=true);
        void packet_filter(std::function<bool(unsigned int)> filter) { packet_filter_ = filter; }
        void skip_shots(std::function<unsigned int(unsigned int)>  f) { skip_shots_ = f; }

        void test_pattern(uint8_t tp);

        // Override this function to include setting up the channel value generators
        void calculate_channel_info() override;

        fake_camera();
        ~fake_camera();
    };

}


#endif
