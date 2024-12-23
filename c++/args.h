#ifndef __APDCAM10G_ARG_H__
#define __APDCAM10G_ARG_H__

#include "error.h"
#include <string>

/*

  This class provides an easy interface to parse command line arguments with automatic error reporting.
  Usage:

  int main(argc, char **argv)
  {
    int value_int;
    double value_dbl;
    for(args a(argc,argv; a; ++a)
    {
      if(a("-h","--help","--give-me-help"))  // true if the next unparsed argument is any of these values (arbitrary number and type of args!)
      { 
        cerr<<"Help"<<endl; 
        exit(1); 
      }
      else if(a("-i")) value_int=a.get<int>(1); // Get the cmd line argument 1 after the current one as an int. Give general error if missing
      else if(a("-d")) value_dbl=a.get<double>(1,"Some double value"); // Get cmd line arg 1 after the current one as a double. Give it a name in the error report if missing
    }
  }

 */

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
        // Initialize from the (argc,argv) arguments of the main function
        args(unsigned int argc, char **argv, bool verbose=true) : argc_(argc), argv_(argv), index_(1), consumed_(1), verbose_(verbose) {}

        // Return the command line argumnet with an offset w.r.t. the current one, converted to the given type.
        // That is, args.get<int>(1); returns the next command line argument after the current one, as an integer.
        // It prints an error message about missing arguments (the second version of the get function accepts a
        // name for this argument for this error report), and throws an error.
        template <typename T=std::string>
        T get(unsigned int offset, const std::string &name="")
            {
                if(index_+offset >= argc_)
                {
                    std::string msg = "At least " + std::to_string(offset) + " arguments ";
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
                        std::cerr<<"Using default value '"<<def<<"' for missing argument "<<offset<<" ("<<name<<")  after "<<argv_[index_]<<std::endl;
                    }
                    return def;
                }
                if(index_+offset > consumed_) consumed_ = index_+offset;
                return s2any<T>(argv_[index_+offset]);
            }

        // Increment the internal index to one beyond the last consumed/processed argument (accessed either by operator() or get<T>(offset)
        void operator++()
            {
                index_ = std::max(index_+1,consumed_+1);
                consumed_ = index_;
            }

        // Return true if we have not yet processed all arguments, i.e. if we can evaluate for example operator()
        operator bool() const
            {
                return index_<argc_;
            }

        // Return the current command line argument as a string
        std::string operator()() const
            {
                return argv_[index_];
            }

        // variadic template implementation of the () operator, with an arbitrary number and type of arguments.
        // It returns true if the current cmd line argument (the one the internal index points to) agrees with any of the given
        // arguments, converted to the proper type. 
        template <typename VALUE>
        bool operator()(VALUE value)
        {
            return get<VALUE>(0) == value;
        }

        template <typename VALUE, typename... VALUES>
        bool operator()(VALUE value, VALUES... values)
        {
            if(operator()(value)) return true;
            return operator()(values...);
        }
    };

    // Specializations of the () operator to char* and const char* types, to interpret C-style strings as
    // real strings, 
    template <>
    bool args::operator()<const char *>(const char *value)
    {
        return get<std::string>(0) == std::string(value);
    }
    template <>
    bool args::operator()<char *>(char *value)
    {
        return get<std::string>(0) == std::string(value);
    }

}

#endif
