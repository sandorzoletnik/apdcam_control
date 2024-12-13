#ifndef __APDCAM10G_BACKTRACE_H__
#define __APDCAM10G_BACKTRACE_H__

#include <execinfo.h>
#include <vector>
#include <string>

namespace apdcam10g
{
    std::vector<std::string> get_backtrace(unsigned int depth=20);
    void print_backtrace(unsigned int depth=20);
}

#endif
