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

        buffer_ = buffer

    # define the parenthesis operator, which does exactly the same as that of the c++ counterpart
    # it accesses the element at the provided 'counter': buffer[counter modulo buffersize]
    def __call__(self,counter):
        return buffer_[counter&mask_]

    
