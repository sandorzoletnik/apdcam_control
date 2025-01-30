import ctypes

'''
This class is a python wrapper for the C++ class ring_buffer
It provides the [] indexing operator to access a given element.
NOTE: this is the counterpart of the () operator of the c++ class, which
accesses elements by the counter (which is starting at zero for the first
object pushed into the buffer, and increments sequentially), and NOT the []
operator of the c++ class which starts indexing at the current first entry
in the buffer. This may be confusing. The c++ class was implemented first,
with () accessing the elements by the counter, and [] by index, then
I discovered that in python the () operator can not be overloaded (at least easily)
to return a variable by reference, i.e. that the returned object is assignable:
b = RingBuffer()
# fill b
b(3) = 123  # this does not work, but the [] operator can do it
'''

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

    
    
