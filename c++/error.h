#ifndef __APDDCAM10G_ERROR_H__
#define __APDDCAM10G_ERROR_H__

#include <string>
#include <iostream>
#include <string.h>
#include <errno.h>
#include "backtrace.h"

namespace apdcam10g
{
    /*

      This class 'error' is functioning as an exception. This class is thrown by fatal errors, which cause program termination,
      with a message about the cause.

     */
    
    class error
    {
    private:
        std::string message_;
        std::string file_;
        int line_;
        mutable bool handled_ = false;
    public:
        error(const std::string &message, const std::string &file, int line) : message_(message), file_(file), line_(line) {}
        
        const error &operator=(const error &rhs)
            {
                message_ = rhs.message_;
                file_ = rhs.file_;
                line_ = rhs.line_;
                handled_ = rhs.handled_;
                return *this;
            }
        
        const std::string &message() const {handled_ = true; return message_;}
        const std::string &file() const { handled_ = true; return file_; }
        int line() const { handled_ = true; return line_; }
        
        std::string full_message() const { return "ERROR: "+message()+" [in "+file()+", line "+std::to_string(line())+"]"; }
            
        ~error() 
        {
            // Seemed to be a good idea to print the message if it has not been accessed before,
            // but if the exception is not caught, its destructor is not called (because there is
            // no stack unwinding?)
            if(!handled_) std::cerr<<"Unhandled error: "<<message_<<std::endl;
        }
        
        void print(std::ostream &out = std::cerr)
        {
            out<<full_message()<<std::endl;
        }

    };

    inline std::ostream &operator<<(std::ostream &out, const error &e) 
    {
        out<<e.full_message();
        return out;
    }
}

/*

  This macro prints the actual position in the source file, and prints the backtrace
  for debuggin purposes, and throws an apdcam10g::error class with the provided
  message 'msg'
  This macro is called for serious errors.

 */

#define APDCAM_ERROR(msg)\
    {\
    std::cerr<<std::endl; \
    std::cerr<<"File: "<<__FILE__<<", Line: "<<__LINE__<<std::endl; \
    std::cerr<<">>> "<<msg<<std::endl;                              \
    std::cerr<<std::endl;\
    apdcam10g::print_backtrace();     \
    throw apdcam10g::error(msg,__FILE__,__LINE__); \
    }

/*

  This macro prints the actual position in the source file, and prints the backtrace
  for debugging purposes
  In addition to printing the message 'msg' it also appends the error message associated
  with the C ssytem library's "errno", which carries information about the error
  encountered in the last C system call

 */

#define APDCAM_ERROR_ERRNO(msg)\
    {\
    std::cerr<<std::endl; \
    std::cerr<<"File: "<<__FILE__<<", Line: "<<__LINE__<<std::endl; \
    std::cerr<<">>> "<<msg<<std::endl;                              \
    std::cerr<<std::endl;\
    apdcam10g::print_backtrace();     \
    throw apdcam10g::error(std::string(msg) + ": " + std::string(strerror(errno)),__FILE__,__LINE__); \
    }


#endif
