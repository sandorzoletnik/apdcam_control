#include "lockable.h"

#include <iostream>
#include <vector>

using namespace apdcam10g;
using namespace std;

int main()
{
    lockable<vector<int>> v;
    {
        std::shared_lock lck(v);
        v.resize(10);
        v[0] = 0;
        v[1] = 1;
        for(auto t : v) cerr<<t<<endl;
    }

    return 0;
}
