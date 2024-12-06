#ifndef __APDCAM10G_TEESTREAM_H__
#define __APDCAM10G_TEESTREAM_H__

#include <iostream>
#include <fstream>
#include <filesystem>

using namespace std;

namespace apdcam10g
{
    template <typename CharT=char, typename Traits=std::char_traits<CharT>>
    class teebuf : public std::basic_streambuf<CharT,Traits>
    {
    private:
        std::basic_streambuf<CharT, Traits> *rdbuf1_=0, *rdbuf2_=0;
        bool owns_rdbuf1_=false, owns_rdbuf2_=false;
    public:
        teebuf(std::basic_streambuf<CharT,Traits> *rdbuf1,std::basic_streambuf<CharT,Traits> *rdbuf2, bool owns_rdbuf1=false, bool owns_rdbuf2=false)
            : rdbuf1_(rdbuf1), rdbuf2_(rdbuf2), owns_rdbuf1_(owns_rdbuf1), owns_rdbuf2_(owns_rdbuf2)
            {}
        
        ~teebuf()
            {
                rdbuf1_->pubsync();
                rdbuf2_->pubsync();
                if(owns_rdbuf1_) delete rdbuf1_;
                if(owns_rdbuf2_) delete rdbuf2_;
            }
    protected:
        
        int overflow(int ch=Traits::eof()) override
            {
                int result = rdbuf1_->sputc(ch);
                if(result!=Traits::eof()) result = rdbuf2_->sputc(ch);
                return result;
            }
        
        virtual int sync() override
            {
                int result = rdbuf1_->pubsync();
                if(result == 0) result = rdbuf2_->pubsync();
                return result;
            }
    };

    // Duplicate all data going to orig_stream into the named file
    ostream &tee(ostream &orig_stream, const std::filesystem::path &filename, std::ios_base::openmode mode=std::ios_base::out);
}

#endif
