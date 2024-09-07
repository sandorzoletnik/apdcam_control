#ifndef __APDCAM10G_UTILS_H__
#define __APDCAM10G_UTILS_H__

#include <vector>
#include <string>
#include <concepts>

namespace apdcam10g
{
    // Split the string at the specified separators (by default: space, tab and newline), and return
    // the substring in a vector. Consecutive separator characters are treated as one single separator
    std::vector<std::string> split(const std::string &s, const std::string &separator = " \t\n");

    // Take a vector of T* (first argument). Loop over its members and delete the objects pointed
    // to by the given element.
    // Resize the vector to 'size'. Then loop over the new elements, create a new object (using operator new)
    // with the constructor arguments args..., and assign its address to the given element
    template <typename T,typename... ARGS>
    void regenerate(std::vector<T*> &v, size_t size, ARGS... args)
    {
        for(auto p : v) delete p;
        v.resize(size);
        for(auto &p : v) p = new T(args...);
    }

    template<typename T, std::invocable<unsigned int> F>
    void regenerate_by_func(std::vector<T*> &v, size_t size, F func)
    {
        for(auto p : v) delete p;
        v.resize(size);
        for(unsigned int i=0; i<size; ++i) v[i] = func(i);
    }

}


#endif
