#ifndef __APDCAM10G_ARG_H__
#define __APDCAM10G_ARG_H__

#include "error.h"
#include <string>

namespace apdcam10g
{
    // Utility functions to get command line argument values
    template <typename T>
    T s2any(char *s) { return T(); }

    template<> int s2any(char *s) { return atoi(s); }
    template<> double s2any(char *s) { return atof(s); }
    template<> const char * s2any(char *s) { return s; }
    template<> std::string s2any(char *s) { return s; }
    
    class args
    {
    private:
        unsigned int argc_;
        char **argv_;
        unsigned int index_;
        unsigned int consumed_;
        bool verbose_ = false;

    public:
        args(unsigned int argc, char **argv, bool verbose=true) : argc_(argc), argv_(argv), index_(1), consumed_(1), verbose_(verbose) {}

        // Return
        template <typename T=std::string>
        T get(unsigned int offset, const std::string &name="")
            {
                if(index_+offset >= argc_)
                {
                    string msg = "At least " + std::to_string(offset) + " arguments ";
                    if(name != "") msg += "(" + name + ") ";
                    msg += std::string("expected after ") + argv_[index_];
                    APDCAM_ERROR(msg);
                }
                if(index_+offset > consumed_) consumed_ = index_+offset;
                return s2any<T>(argv_[index_+offset]);
            }
    
        template <typename T=std::string>
        T get(unsigned int offset, const std::string &name, T def)
            {
                if(index_+offset >= argc_)
                {
                    if(verbose_)
                    {
                        cerr<<"Using default value '"<<def<<"' for missing argument "<<offset<<" ("<<name<<")  after "<<argv_[index_]<<endl;
                    }
                    return def;
                }
                if(index_+offset > consumed_) consumed_ = index_+offset;
                return s2any<T>(argv_[index_+offset]);
            }
        void operator++()
            {
                index_ = std::max(index_+1,consumed_+1);
                consumed_ = index_;
            }
        operator bool() const
            {
                return index_<argc_;
            }

        // Return the current command line argument as a string
        std::string operator()() const
            {
                return argv_[index_];
            }
    };

}

#endif
