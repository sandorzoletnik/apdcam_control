#ifndef __APDCAM10G_LOCKABLE_H__
#define __APDCAM10G_LOCKABLE_H__

#include <shared_mutex>

/*

lockable is a utility class to easily create a mutex associated with a specific variable.
lockable simply inherits from the given type and from a mutex as well.

Example:

lockable<vector<int>> v;
{
   std::shared_lock lck(v);  // create a scoped lock 
   v.resize(10);
   v[0] = 1;
}  // scope of locking ends here

*/

namespace apdcam10g
{
    template <typename T, typename MUTEX=std::shared_mutex>
        class lockable : public T
    {
    private:
        mutable MUTEX mutex_;

    public:
        void lock() const { mutex_.lock(); }
        void unlock() const { mutex_.unlock(); }
        void lock_shared() const { mutex_.lock_shared(); }
        void unlock_shared() const { mutex_.unlock_shared(); }

        template <typename U> requires std::is_constructible_v<T,U>
        lockable(const U &rhs) : T(rhs) {}
        
        lockable() : T() {}

        template <typename U> requires std::is_assignable_v<T,U>
        const T &operator=(const U &rhs) 
            {
                T::operator=(rhs);
                return rhs;
            }
    };
}

#endif
