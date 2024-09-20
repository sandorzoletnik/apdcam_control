#ifndef __APDCAM10G_SHOT_DATA_LAYOUT_H__
#define __APDCAM10G_SHOT_DATA_LAYOUT_H__

#include "channel_info.h"
#include "terminal.h"
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

namespace apdcam10g
{
    class shot_data_layout
    {
    private:
        std::string prompt_;

        class bit
        {
        public:
            int channel_index = -1;
        };

        class byte
        {
        public:
            bit bits[8];
            channel_info *channel = 0;
        };

        std::vector<byte> bytes_;
        std::vector<channel_info*> channels_;

        void init_(unsigned int n_bytes, unsigned int resolution_bits, const std::vector<channel_info*> &channels) 
            {
                bytes_.resize(n_bytes);
                for(auto &b : bytes_) b = byte();
                channels_ = channels;
                for(unsigned int i_channel=0; i_channel<channels_.size(); ++i_channel)
                {
                    channel_info *c = channels_[i_channel];
                    int b = c->byte_offset + c->nbytes - 1;
                    for(unsigned int i_bit=0, i_target_bit=c->shift; i_bit<resolution_bits; ++i_bit)
                    {
                        bytes_[b].bits[i_target_bit].channel_index = i_channel;
                        if(++i_target_bit > 7)
                        {
                            --b;
                            i_target_bit = 0;
                        }
                    }
                    if(c->nbytes==3) bytes_[c->byte_offset+1].channel = c;
                    else if(c->nbytes==2)
                    {
                        int n2 = 8-c->shift;
                        int n1 = resolution_bits-n2;
                        if(n2>n1) bytes_[c->byte_offset+1].channel = c;
                        else      bytes_[c->byte_offset  ].channel = c;
                    }
                    else if(c->nbytes==1) bytes_[c->byte_offset].channel = c;
                    else APDCAM_ERROR("This should never happen");
                }
            }
        

    public:
        void prompt(const std::string &p) { prompt_ = p; }

        shot_data_layout(unsigned int n_bytes, unsigned int resolution_bits, const std::vector<ring_buffer<data_type,channel_info>*> &channels)
            {
                std::vector<channel_info*> channels2(channels.size());
                for(unsigned int i=0; i<channels.size(); ++i) channels2[i] = channels[i];
                init_(n_bytes, resolution_bits, channels2);
            }
        shot_data_layout(unsigned int n_bytes, unsigned int resolution_bits, const std::vector<channel_info*> &channels)
            {
                init_(n_bytes, resolution_bits, channels);
            }


        // begin, end -- range of the bytes to start
        void show(apdcam10g::byte *buffer=0, ostream &out = cerr, int begin=0, int end=-1) const
            {
                if(end<0) end = bytes_.size();
                if(end > bytes_.size()) end = bytes_.size();
                for(unsigned int i_byte = begin; i_byte<end; ++i_byte)
                {
                    out<<prompt_<<" ["<<std::setw(2)<<i_byte<<"]   ";
                    for(int i_bit=7; i_bit>=0; --i_bit)
                    {
                        const int chind = bytes_[i_byte].bits[i_bit].channel_index;
                        if(chind<0) out<<terminal::reset;
                        else
                        {
                            if(chind%2==0) out<<terminal::green_bg<<terminal::black_fg;
                            else           out<<terminal::red_bg<<terminal::black_fg;
                        }
                        if(buffer)
                        {
                            out<<(int)((buffer[i_byte]>>i_bit)&apdcam10g::byte(1));
                        }
                        else out<<"*";
                        out<<terminal::reset;
                    }
                    if(bytes_[i_byte].channel)
                    {
                        channel_info *c = bytes_[i_byte].channel;
                        string label = std::to_string(c->board_number) + "/" + std::to_string(c->chip_number) + "/" + std::to_string(c->channel_number);
                        out<<"    "<<label;
                        if(buffer) out<<"  --> "<<c->get_from_shot(buffer);
                    }
                    out<<endl;
                }
                out<<endl;
            }
    };




    

}

#endif
