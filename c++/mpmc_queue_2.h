// https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

// I think the queue fails if more than max_value(size_t) data is sent through it, i.e. enqueue_pos_/dequeue_pos_ overflows

#include <assert.h>
#include <iostream>

template<typename T>
class mpmc_bounded_queue
{
public:

/*
    struct cell_t
    {
        std::atomic<size_t>   sequence_;
        T                     data_;
    };
*/

    static size_t const     cacheline_size = 64;
    typedef char            cacheline_pad_t [cacheline_size];
    cacheline_pad_t         pad0_; // I assume this here is needed to separate the members of this class from any other thing in memory
    T* const                buffer_;
    std::atomic_flag *const free_;
    size_t const            buffer_mask_;
    cacheline_pad_t         pad1_;
    std::atomic<size_t>     enqueue_pos_;
    cacheline_pad_t         pad2_;
    std::atomic<size_t>     dequeue_pos_;
    cacheline_pad_t         pad3_;  // I assume this is needed to separate members from anything else in memory
    mpmc_bounded_queue(mpmc_bounded_queue const&);
    void operator = (mpmc_bounded_queue const&);

public:
    mpmc_bounded_queue(size_t buffer_size) : buffer_(new T [buffer_size]), free_(new std::atomic_flag [buffer_size]), buffer_mask_(buffer_size - 1)
    {
        assert((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0));
        for (size_t i = 0; i != buffer_size; ++i)
        {
            //sequence_[i].store(i, std::memory_order_relaxed);
            free_[i].test_and_set(std::memory_order_seq_cst);
        }
        //enqueue_pos_.store(0, std::memory_order_relaxed);
        enqueue_pos_.store(0, std::memory_order_seq_cst);
        //dequeue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_seq_cst);
//        cerr<<"queue pos lock free: "<<std::atomic<size_t>::is_always_lock_free<<endl;
//        cerr<<endl;
    }

    ~mpmc_bounded_queue()
    {
        delete [] buffer_;
        delete [] free_;
    }

    void dump_producer()
    {
        cerr<<"Enqueue position: "<<enqueue_pos_.load(std::memory_order_seq_cst)<<endl;
        for(int i=0; i<=buffer_mask_; ++i) std::cerr<<buffer_[i]<<"  --  "<<free_[i].test()<<endl;
    }
    void dump_consumer()
    {
        const auto dp = dequeue_pos_.load(std::memory_order_seq_cst);
        cerr<<"Dequeue position: "<<dp<<" ("<<(dp&buffer_mask_)<<")"<<endl;
        for(int i=0; i<=buffer_mask_; ++i) std::cerr<<buffer_[i]<<"  --  "<<free_[i].test()<<endl;
    }

    std::mutex the_mutex;


    bool enqueue(T const& value)
    {
        T* data=0;
        std::atomic_flag *free=0;
        
        //size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        size_t pos = enqueue_pos_.load(std::memory_order_seq_cst);
        for (;;)
        {
            free = std::addressof(free_[pos & buffer_mask_]);
            if(!free->test()) return false;
            data = std::addressof(buffer_[pos & buffer_mask_]);

            // Atomically check if enqueue_pos_ has not changed in the meantime. If it hasn't, increment it, and break the loop.
            // If it has, then update 'pos' to the new value of enqueue_pos_, and keep looping.
            //if (enqueue_pos_.compare_exchange_strong (pos, pos + 1, std::memory_order_relaxed)) break;
            if (enqueue_pos_.compare_exchange_strong (pos, pos + 1, std::memory_order_seq_cst)) break;
        }
        
        *data = value;
        free->clear(std::memory_order_seq_cst);
        return true;
    }

    bool dequeue(T& value)
    {
        T* data=0;
        std::atomic_flag *free=0;

        size_t pos = dequeue_pos_.load(std::memory_order_seq_cst);
        for (;;)
        {
            free = std::addressof(free_[pos & buffer_mask_]);
            // This element is 'free' yet, i.e. the producer has not finished writing the data to it. 
            if(free->test()) return false;
            data = std::addressof(buffer_[pos & buffer_mask_]);

            // Atomically check if dequeue_pos_ has not changed in the meantime. If it hasn't (i.e. no other thread dequeued the element), 
            // increment it, and break the loop.
            // If it has, then update 'pos' to the new value of dequeue_pos_, and keep looping.
            //if (dequeue_pos_.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed)) break;
            if (dequeue_pos_.compare_exchange_strong(pos, pos + 1, std::memory_order_seq_cst)) break;
        }

        value = *data;
        // Update sequence_ to the value expected at the next enqueue operation
        //sequence->store (pos + buffer_mask_ + 1, std::memory_order_release);
        free->test_and_set(std::memory_order_seq_cst);
        return true;
    }


}; 


