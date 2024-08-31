// https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

// I think the queue fails if more than max_value(size_t) data is sent through it, i.e. enqueue_pos_/dequeue_pos_ overflows

#include <assert.h>
#include <iostream>

template<typename T>
class mpmc_bounded_queue
{
public:

    struct cell_t
    {
        std::atomic<size_t>   sequence_;
        T                     data_;
    };

    static size_t const     cacheline_size = 64;
    typedef char            cacheline_pad_t [cacheline_size];
    cacheline_pad_t         pad0_; // I assume this here is needed to separate the members of this class from any other thing in memory
    cell_t* const           buffer_;
    size_t const            buffer_mask_;
    cacheline_pad_t         pad1_;
    std::atomic<size_t>     enqueue_pos_;
    cacheline_pad_t         pad2_;
    std::atomic<size_t>     dequeue_pos_;
    cacheline_pad_t         pad3_;  // I assume this is needed to separate members from anything else in memory
    mpmc_bounded_queue(mpmc_bounded_queue const&);
    void operator = (mpmc_bounded_queue const&);

public:
    mpmc_bounded_queue(size_t buffer_size) : buffer_(new cell_t [buffer_size]) , buffer_mask_(buffer_size - 1)
    {
        assert((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0));
        for (size_t i = 0; i != buffer_size; ++i)
        {
            //buffer_[i].sequence_.store(i, std::memory_order_relaxed);
            buffer_[i].sequence_.store(i, std::memory_order_seq_cst);
        }
        //enqueue_pos_.store(0, std::memory_order_relaxed);
        enqueue_pos_.store(0, std::memory_order_seq_cst);
        //dequeue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_seq_cst);
    }

    ~mpmc_bounded_queue()
    {
        delete [] buffer_;
    }

    void dump(bool producer, bool consumer)
    {
        if(producer) cerr<<enqueue_pos_.load(std::memory_order_seq_dst)<<endl;
        if(consumer) cerr<<dequeue_pos_.load(std::memory_order_seq_dst)<<endl;
        for(int i=0; i<=buffer_mask_; ++i) std::cerr<<buffer_[i].data_<<"  --  "<<buffer_[i].sequence_<<endl;
    }

    bool enqueue(T const& data)
    {
        cell_t* cell;
        //size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        size_t pos = enqueue_pos_.load(std::memory_order_seq_cst);
        for (;;)
        {
            cell = &buffer_[pos & buffer_mask_];
            //size_t seq = cell->sequence_.load(std::memory_order_acquire);
            size_t seq = cell->sequence_.load(std::memory_order_seq_cst);
            intptr_t dif = (intptr_t)seq - (intptr_t)pos;
            if (dif == 0) // (1)
            {
                // Atomically check if enqueue_pos_ has not changed in the meantime. If it hasn't, increment it, and break the loop.
                // If it has, then update 'pos' to the new value of enqueue_pos_, and keep looping.
                //if (enqueue_pos_.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed)) break;
                if (enqueue_pos_.compare_exchange_weak (pos, pos + 1, std::memory_order_seq_cst)) break;
            }
            // This element has not yet been liberated by dequeue. We are biting into the tail, so return false
            else if (dif < 0) return false;
            // This happens when enqueue_pos_ has overflowed, and is less then sequence_? But doesn't this mean
            // that we should return false since this element has not yet been liberated by dequeue? The value
            // written to sequence_ by dequeue is pos+buffer_mask_+1 which equals pos+buffersize, which would
            // overflow in the same way, and the equality condition would be satisfied
            //else pos = enqueue_pos_.load(std::memory_order_relaxed);
            else pos = enqueue_pos_.load(std::memory_order_seq_cst);
        }
        cell->data_ = data;
//        cell->sequence_.store(pos + 1, std::memory_order_release);
        cell->sequence_.store(pos + 1, std::memory_order_seq_cst);
        return true;
    }

    bool dequeue(T& data)
    {
        cell_t* cell;
        //size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        size_t pos = dequeue_pos_.load(std::memory_order_seq_cst);
        for (;;)
        {
            cell = &buffer_[pos & buffer_mask_];
            //size_t seq = cell->sequence_.load(std::memory_order_acquire);
            size_t seq = cell->sequence_.load(std::memory_order_seq_cst);
            intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);

            if (dif == 0)
            {
                // Atomically check if dequeue_pos_ has not changed in the meantime. If it hasn't (i.e. no other thread dequeued the element), 
                // increment it, and break the loop.
                // If it has, then update 'pos' to the new value of enqueue_pos_, and keep looping.
                //if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_seq_cst)) break;
            }
            else if (dif < 0) return false;
            //else pos = dequeue_pos_.load(std::memory_order_relaxed);
            else pos = dequeue_pos_.load(std::memory_order_seq_cst);
        }

        data = cell->data_;
        // Update sequence_ to the value expected at the next enqueue operation
        //cell->sequence_.store (pos + buffer_mask_ + 1, std::memory_order_release);
        cell->sequence_.store (pos + buffer_mask_ + 1, std::memory_order_seq_cst);
        return true;
    }


}; 


