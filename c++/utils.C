#include "utils.h"

// To get the home dir of the current user
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstdlib>



using namespace std;

namespace apdcam10g
{
    std::string homedir()
    {
        return getenv("HOME");
        //passwd *pw = getpwuid(getuid());
        //return pw->pw_dir;
    }

    vector<string> split(const string &s,const string &separator)
    {
	vector<string> out;
	string w;
	for(unsigned int i=0; i<s.size(); ++i)
	{
	    if(separator.find(s[i]) != string::npos)
	    {
		if(w != "") out.push_back(w);
		w = "";
	    }
	    else
	    {
		w += s[i];
	    }
	}
	if(w != "") out.push_back(w);
	return out;
    }

    std::recursive_mutex &output_mutex()
    {
        static std::recursive_mutex m;
        return m;
    }
    
}
