/*

  ring_buffer is a fixed-size buffer offering a cyclic push-pop operation. It is only thread-safe in a
  single-producer/single-consumer scheme. 

  Pop operation (i.e. removing objects from the buffer) do not call
  the destructor of the removed objects. This is ok for built-in types
  but may make surprises for class objects.

  Instead of cyclically reset the push/pop indices to zero whenever they reach the end of the buffer
  (this should be checked by an if statement every time these indices are updated, i.e. an element is
  pushed or popped), we keep track of the actual data stored in the buffer using two counters which
  keep running continuously/infinitely (they will of course overflow at some point). This solves two problems
  at the same time:

  1 - If the pop and push indices were limited to [0..buffersize[, pop_index==push_index could
      indicate either a full or empty buffer. An extra flag would be needed to keep track,
      causing further complications. With the chosen scheme, push_counter==pop_counter
      indicates empty buffer, whereas push_counter==pop_counter+buffersize indicates full buffer.
  2 - Data from different channels may come through different sockets from the camera. The camera
      raw data format does not include timestamps or other means of synchronization. It is only
      the count of data of the individual channels that can be used. The above scheme (i.e. a
      continuously running counter for both push and pop positions) automatically provides
      this feature

  ring_buffer is a template class with two template types:
  1 - The first type is of course the data type that this buffer can store
  2 - An optional class from which ring_buffer will derive. It will inherit all public members
      of this base class type. This is an elegant way to easily augment the functionality
      of ring_buffer with further services. In the current context, this feature will be used
      for the buffers storing the subsequent values of the individual channels. The ring_buffer
      storing these values will derive from channel_info so that channel number, board number
      and further info can directly be queried from the corresponding ring_buffer
      
 */
  
#ifndef __APDCAM10G_RING_BUFFER_H__
#define __APDCAM10G_RING_BUFFER_H__

#include <iostream>
#include <atomic>
#include <new>
#include <sys/mman.h>
#include <cmath>

#include "error.h"
#include "utils.h"

using namespace std;

namespace apdcam10g
{
    // An empty class serving as the default base class type. It adds no overhead.
    class EMPTY_RING_BUFFER_BASE {};

    template<typename T, typename BASE=EMPTY_RING_BUFFER_BASE>
    class ring_buffer : public BASE
    {
    private:

        // Both pop_counter_ and push_counter_ are read by both the producer and consumer threads,
        // so ensure these two atomic variables are in the same cache-line to avoid the need to
        // synchronize two cache-lines
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> pop_counter_;
        std::atomic<size_t> push_counter_;

        // mask_=buffersize-1. Buffersize must be power of 2 so mask will be used to
        // enforce cyclic indexing (modulo buffersize of the continuously running counters)
        size_t mask_;

        // Extra size at the end of the buffer so that if a continuous range of elements is
        // required, those elements which eventually happen to be at the front, can be copied
        // to the extra space at the end
         size_t extra_size_;

        // dynamically allocated buffer to store the data
        T*     buffer_ = 0;

        // A true/false flag to communicate from the producer thread to the consumer thread that
        // the data flow is terminated, no more data than that available currently in the buffer
        // will be produced
        std::atomic_flag        terminated_;

        // Variables for statistics
        double sum_size_=0;    // sum of the values of size sampled at every call to push(..) or publish(...)
        double sum_size2_=0;   // sum of the squared size values
        double sum_n_=0;       // Number of samplings of the size
        size_t max_size_=0;

        ring_buffer(ring_buffer const&);
        void operator = (ring_buffer const&);

    public:
        ring_buffer(size_t buffer_size=0, size_t extra_size=0)
        {
            resize(buffer_size,extra_size);
        }

        T *raw_buffer() const
        {
            return buffer_;
        }
        
        // Reset the statistics data, which calculates the maximum buffer (fill) size during operation, the average
        // fill size, and its sigma
        virtual void reset_statistics()
        {
            sum_size_ = sum_size2_ = sum_n_ = 0;
            max_size_=0;
        }

        void resize(size_t buffer_size, size_t extra_size=0)
        {
            mask_ = buffer_size-1;
            extra_size_ = extra_size;

            if(buffer_ != 0)
            {
                ::munlock(buffer_,mask_+1+extra_size_);
                delete [] buffer_;
            }
            buffer_ = 0;
            
            // Ensure buffer_size is power of 2
            if(buffer_size>=2 && (buffer_size&(buffer_size-1))!=0) APDCAM_ERROR("Ring buffer size must be a power of 2");

            if(buffer_size>0)
            {
                // Allocate buffer with extra space for flattening
                buffer_ = new T[buffer_size+extra_size];
                
                // Prevent the buffer memory from beeing swapped to disk
                if(::mlock(buffer_,buffer_size+extra_size) != 0) APDCAM_ERROR_ERRNO("Failed to lock memory from swapping");
            }

            clear();
        }

        ~ring_buffer()
        {
            if(buffer_ != 0)
            {
                ::munlock(buffer_,mask_+1+extra_size_);
                delete [] buffer_;
            }
        }

        // Copy the values from an instance of its base class using the base class's assignment operator
        void copy_values(const BASE &rhs)
        {
            BASE::operator=(rhs);
        }

        // Query the 'terminated' flag
        bool terminated() const
        {
            return terminated_.test(std::memory_order_acquire);
        }

        // Set the 'terminated' flag to signal consumers that no more data is coming down via this buffer
        void terminate()
        {
            terminated_.test_and_set(std::memory_order_release);
        }

        // Query the pop and push counters
        size_t pop_counter() const 
        {
            return pop_counter_.load(std::memory_order_acquire);
        }
        size_t push_counter() const 
        {
            return push_counter_.load(std::memory_order_acquire);
        }

        // Push a new element into the buffer. This function does not wait until free slots are avialable: it returns
        // nullptr immediately if the buffer is full. So a wait loop needs to be implemented around the call of this function.
        // In case of success, it returns a pointer to the newly stored object within the buffer.
        T *push(T const& value)
        {
            // Take a snapshot of the current status. Read pop_index as second to very slightly increase the chance
            // that the consumer thread liberates space
            const size_t push_counter = push_counter_.load(std::memory_order_relaxed);
            // Pop operations (which update pop_counter) do not have any side effect (they do not even call the destructors
            // of the objects of type T !) so we can load pop_counter relaxed
            const size_t pop_counter = pop_counter_.load(std::memory_order_relaxed);

            // Buffer is full
            if(push_counter >= pop_counter+mask_+1) return 0;

            // Accumulate the statistical variables
            {
                const size_t s = size();
                ++sum_n_;
                sum_size_ += s;
                sum_size2_ += s*s;
                if(s>max_size_) max_size_ = s;
            }

            // We are the only producers, and we have room. No other thread is producing data into the buffer, i.e.
            // no other thread has decreased the available space since we last checked it. So just write to the new place
            buffer_[push_counter&mask_] = value;

            // Get a pointer to the newly stored object
            T* ptr = buffer_+(push_counter&mask_);

            // We are the only producers, no other thread has changed push_counter_ in the meantime, so use the snapshot value 'push_counter', incremented
            push_counter_.store(push_counter+1,std::memory_order_release);

            return ptr;
        }

        // If the buffer is non-empty, remove the front element, and return true.
        // Othersize return false
        bool pop()
        {
            // Take a snapshot of the current status. Read push_counter as second to very slightly increase the chance that the producer thread added new data
            // and the queue is not empty
            const size_t pop_counter = pop_counter_.load(std::memory_order_relaxed);
            const size_t push_counter = push_counter_.load(std::memory_order_acquire);

            // Buffer is empty
            if(push_counter == pop_counter) return false;

            // We are the only consumers, no other thread has changed pop_counter_, so use the cached snapshot value 'pop_counter', incremented
            // We do not produce any side effect that should be propagated to the producer thread (copying the value of the just-popped
            // element into 'value' is local to this thread) so store relaxed
            pop_counter_.store(pop_counter+1,std::memory_order_relaxed);

            return true;
        }

        // If the buffer is non-empty, copy the front element into 'value', remove it from the buffer, and return true
        // Otherwise return false;
        // Does not call the destructor of the object that is removed from the buffer.
        bool pop(T &value)
        {
            // Take a snapshot of the current status. Read push_counter as second to very slightly increase the chance that the producer thread added new data
            // and the queue is not empty
            const size_t pop_counter = pop_counter_.load(std::memory_order_relaxed);
            const size_t push_counter = push_counter_.load(std::memory_order_acquire);

            // Buffer is empty
            if(push_counter == pop_counter) return false;

            // We are the only consumers, no other thread has removed data from the front
            value = buffer_[pop_counter&mask_];

            // We are the only consumers, no other thread has changed pop_counter_, so use the cached snapshot value 'pop_counter', incremented
            // We do not produce any side effect that should be propagated to the producer thread (copying the value of the just-popped
            // element into 'value' is local to this thread) so store relaxed
            pop_counter_.store(pop_counter+1,std::memory_order_relaxed);

            return true;
        }

        // Pop the elements from the front of the buffer up to (including) counter
        void pop_to(size_t counter)
        {
            pop_counter_.store(counter, std::memory_order_relaxed);
        }
        
        // To be called by the consumer only.
        // Return the pointer to the nth element beyond the back of the buffer. With n=0 returns the first future element.
        // If the buffer is full, returns zero
        // This function is useful to write (prepare) data without "publishing" it, i.e. without making it available yet
        // for the consumer thread
        T *future_element(size_t n)
        {
            // Take a snapshot of the current status. Read pop_index as second to very slightly increase the chance
            // that the consumer thread liberates space
            // push_counter is our own variable, no other thread is changing it, so load relaxed
            const size_t push_counter = push_counter_.load(std::memory_order_relaxed);

            // Spin-lock wait for a free slot for the future element
            // Pop only liberates slots, but does not have side effects, so load relaxed
            while( pop_counter_.load(std::memory_order_relaxed)+mask_+1 <= push_counter_+n );

            return buffer_+((push_counter_+n)&mask_);
        }

        // "Publish" n new elements. That is, if you have prepared data in the 'future' domain, you can now make it available
        // to the consumer thread. Note that 'n' here is the number of elements to publish, whereas in 'future_element(n)' it
        // was an index in the future region. So that if you prepared the last data at future_element(n), you should call
        // publish(n+1);
        // This function does not check for consistency! Only use it in the following scenario: future_element(n) was successful
        // (i.e. returned a non-zero pointer), and no push or publish operations were made by this (producer) thread, then 
        // you can call publish(n+1);
        // The successful call to future_element ensures that the region to be published is indeed free, and this programming strategy
        // is also matching practice: you obtain a slot in the future region via future_element, you do your own data manipulation on it
        // (for example swap some future elements, etc, or store values in them), then publish them. 
        void publish(size_t n)
        {
            {
                const size_t s = size();
                ++sum_n_;
                sum_size_ += s;
                sum_size2_ += s*s;
                if(s>max_size_) max_size_ = s;
            }

            // release --> purge all previous writes to the data buffer
            push_counter_.fetch_add(n, std::memory_order_release);
        }

        // This function must be called only from a single consumer thread
        // It spin-lock waits (1) for data to become available up to (exclusive) counter_to. When this holds,
        // and if the data range is flat (i.e. it does not fold over to the beginning), the first value
        // of the returned tuple is set to the start of the range and the second value to the number elements (equal to
        // counter_to-counter_from), and the second two values are zero. If the range folds back to the
        // front, the folded-back interval is memcpy-d to the extra space at the end of the queue, if it fits
        // (it is the user's responsibility to request a range that surely fits into the extra space), 
        // and the pointer to the first element of the range is returned, together with the number of elements (equal
        // to counter_to-counter_from)
        // If the 'terminated' flag is set by the producer before or after spin-lock waiting (1), the wait loop is broken
        // and the available number of elemens is returned in the second value
        std::tuple<T*,size_t> operator()(size_t counter_from, size_t counter_to)
        {
            // We assume the user is not stupid... we do not check, for performance reasons
            //if(counter_to < counter_from) APDCAM_ERROR("counter_to is less than counter_from");

            // Pop operation does not have side effects relevant to us (and we are anyway the consumer thread
            // which is calling pop), so load relaxed
            const size_t pop_counter = pop_counter_.load(std::memory_order_relaxed);

            // Normally, the user would query sequentially increasing ranges. That is, query 0-100, 100-200, 200-300, etc
            // and pop these ranges onces processed.
            // If 0-100 is popped (100 exclusive...), and the queue is empty, but user is requesting 100-200, we do not check
            // the pop-counter, only the push-counter. Once this is available, and the user has not cut the tree on which he
            // was sitting (i.e. did not pop a range, say, 0-200, and then requests 100-200), then this ensures consistency.
            // If the queue is still empty when the second range 100-200 is queried, we just wait for the push-counter to
            // reach 200.
            // However, the user must not query a range (a,b) such that a<pop_counter. It is his responsibility to not
            // query a range starting from a value which he already popped. 
            // So we can get granted that counter_from>=pop_counter

            // Storing snapshot of the terminated flag
            bool term = false;

            // spin-lock wait for the required data to become available
            while( push_counter_.load(std::memory_order_acquire) < counter_to )
            {
                // but break the link if the queue is terminated. The 'terminated' flag indicates that
                // no more data will ever arrive into the queue
                if( (term=terminated()) == true )
                {
                    // Re-query the push_counter, more data may have arrived in the meantime
                    const size_t push_counter = push_counter_.load(std::memory_order_acquire);
                    // and limit counter_to to push_counter if needed
                    if(push_counter < counter_to) counter_to = push_counter;
                }
            }

            // Here: do not get granted that data is available up to the requested counter_to !

            // If the requested range cyclically goes to the front... copy that data to the extra space
            if((counter_to&mask_) < (counter_from&mask_))
            {
                // The number of requested elements
                const size_t n = counter_to-counter_from;

                // Calculate the number of elements that fit continuously up to the end
                const size_t n_back = (mask_+1)-(counter_from&mask_);

                // Number of elements which are at the front
                const size_t n_front = n-n_back;

                // If the requested number of elements do not fit into the extra space, punish the user
                // (he should make sure he never requests too much which would not fit into the extra space)
                if(n_front > extra_size_) APDCAM_ERROR("Requested range does not fit into extra space of the ring_buffer");

                // Bitwise copy the front elements into the extra space
                memcpy(buffer_+mask_+1,buffer_,n_front*sizeof(T));
            }

            return 
            {
                // if no data is available and we return because of being terminated, make sure the user does not get a non-zero pointer
                (counter_to>counter_from?buffer_+(counter_from&mask_):0), 
                counter_to-counter_from
            };
        }

        // Does not make any checks!
        T &operator()(size_t counter)
        {
            return buffer_[counter&mask_];
        }

        T &operator[](size_t index)
        {
            return buffer_[(pop_counter_+index)&mask_];
        }

        size_t capacity() const
        {
            return mask_+1;
        }

        // Returns the actual number of elements in the buffer. operator[] can be called with values from 0 to size()-1
        size_t size() const
        {
            return push_counter_.load(std::memory_order_acquire)-pop_counter_.load(std::memory_order_acquire);
        }

        // Return whether the buffer is empty - i.e. there are actually no data stored in it.
        bool empty() const
        {
            return size()==0;
        }

        ring_buffer &clear()
        {
            // Initialize counters and terminated flag
            push_counter_.store(0, std::memory_order_relaxed);
            pop_counter_.store(0, std::memory_order_relaxed);
            terminated_.clear(std::memory_order_relaxed);
            return *this;
        }

        void dump()
        {
            auto push_counter = push_counter_.load(std::memory_order_seq_cst);
            auto pop_counter  = pop_counter_.load(std::memory_order_seq_cst);
            std::cerr<<"---------------------------------"<<std::endl;
            std::cerr<<"Push position: "<<push_counter<<" ("<<(push_counter&mask_)<<")"<<std::endl;
            std::cerr<<"Pop position: "<<pop_counter<<" ("<<(pop_counter&mask_)<<")"<<std::endl;
            for(size_t p=0; p<=mask_; ++p)
            {
                // Buffer is full
                if(push_counter == pop_counter+mask_+1) 
                {
                    cerr<<"** ";
                }
                else if( (push_counter&mask_)>(pop_counter&mask_) )
                {
                    if((pop_counter&mask_)<=p && p<(push_counter&mask_)) cerr<<"** ";
                    else cerr<<"   ";
                }
                else
                {
                    if( (pop_counter&mask_)<=p || p<(push_counter&mask_)) cerr<<"** ";
                    else cerr<<"   ";
                }
                cerr<<buffer_[p]<<endl;
            }
        }

        // Mean value of the buffer size, sampled at every push(...) or publish(...) call
        double mean_size() const { return sum_size_/sum_n_; }
        // Standard deviation of the buffer size
        double sd_size() const { return std::sqrt(sum_size2_/sum_n_-sq(sum_size_/sum_n_)); }
        // Return the maximum size so far
        size_t max_size() const { return max_size_; }
    }; 
}

#endif
