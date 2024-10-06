import ctypes

class RingBuffer:
    def __init__(self,type,size,buffer):

        if size&(size-1)!=0 or size==0:
            print("RingBuffer size must be non-zero and a power of two")
            return

        # The type of the data that is stored in the ring buffer
        self.type_ = type

        # the mask 
        self.mask_ = size-1

        self.buffer_ = buffer

    # define the [] operator, which accesses the object at counter 'counter' in the buffer.
    # Note that this is the equivalent of the () operator of the C++ ring_buffer. I could not
    # figure out how to return a reference from a function, so that the element accessed by
    # the () operator can also be assigned and changed. For the [] operator, __getitem__ and
    # __setitem__ do the job
    # it accesses the element at the provided 'counter': buffer[counter modulo buffersize]
    def __getitem__(self,counter):
        return self.buffer_[counter&self.mask_]

    def __setitem__(self,counter,newvalue):
        self.buffer_[counter&self.mask_] = newvalue

    
    
