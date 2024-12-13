#include "backtrace.h"
#include <iostream>
#include <cstdlib>
#include <cxxabi.h>
using namespace std;

namespace apdcam10g
{
    vector<string> get_backtrace(unsigned int depth)
    {
        void **buffer = new void*[depth];
        int size = backtrace(buffer,depth);
        char **s = backtrace_symbols(buffer,size);
        vector<string> result;
        for(int i=0; i<size; ++i) result.push_back(string(s[i]));
        free(s);
        return result;
    }

    void print_backtrace(unsigned int depth)
    {
        auto ss = get_backtrace(depth);
        int status;
        for(auto s : ss)
        {
            const unsigned first = s.find('(');
            const unsigned last = s.find('+',first);
            const string lib = s.substr(0,first);
            const string mangled_name = s.substr (first+1,last-first-1);        
            char *realname = abi::__cxa_demangle(mangled_name.c_str(),NULL,NULL,&status);
            if(status == 0)
            {
                cerr<<lib<<"  -->  "<<realname<<endl;
                std::free(realname);
            }
            else
            {
                cerr<<s<<"  (demangling failed with code: "<<status<<")"<<endl;
            }
        }
    }
}
