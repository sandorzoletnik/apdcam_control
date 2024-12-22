#ifndef __APDCAM10G_THREAD_SAFE_H__
#define __APDCAM10G_THREAD_SAFE_H__

// This code implements Bjarne Stroustrup's solution published in
// "Wrapping C++ Member Function Calls"
// The class 'thread_safe' implements thread-safe access to non-trivially-copyable
// classes that can not be used in std::atomic.
// Note that the class nests the desired type, but will behave as a pointer:
// thread_safe<vector<int>> v;
// v->resize(10);
// 

#include <mutex>

template <typename T>
class thread_safe
{
private:
    // In contrast to Bjarne's method, we do not store a pointer to a variable defined elsewhere,
    // but nest a variable
    T variable_;

    // The recursive mutex which protects member function calls
    std::recursive_mutex mutex_;

public:

    // Little improvement over Bjarne's code: the class 'proxy' is nested inside the wrapper class 'thread_safe'
    // to avoid global namespace pollution
    class proxy
    {
    private:
        thread_safe<T> *ptr_;
        mutable bool own_;

        proxy(thread_safe<T> *p) : ptr_(p), own_(true) {}
        proxy(const proxy &rhs) : ptr_(rhs.ptr_), own_(true) { rhs.own_=false; }
        proxy &operator=(const proxy&); // prevent assignment
    public:
        friend class thread_safe<T>;
        T *operator->() { return &(ptr_->variable_); }
        T &operator*() { return ptr_->variable_; }

        decltype((ptr_->variable_)[0]) &operator[](int i) { return (ptr_->variable_)[i]; }
        
        ~proxy() 
            { 
                if(own_)
                {
                    ptr_->mutex_.unlock();
                }
            }
    };

    proxy operator->() 
        { 
            mutex_.lock();
            return proxy(this); 
        }
    proxy operator*()
        { 
            mutex_.lock();
            return proxy(this); 
        }
};

#endif
