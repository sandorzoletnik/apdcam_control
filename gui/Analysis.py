

class Analysis:
    # Load the DAQ C++ backend
    def load_daq(self,sopath):
        if self.daq is not None:
            return True

        self.daq = ctypes.CDLL(sopath)
        if self.daq is None:
            return False

        self.daq.get_buffer.restype = None
        self.daq.get_buffer.argtypes = [ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16))]

        return True

    def __init__(self):
        # Load the DAQ C++ shared library
        self.load_daq("/home/fi/apdcam/c++/libapdcam10g.so")

        # allocate RingBuffer slots for the 128 channels, by default they are None.
        # Only the allowed channels will have a non-None entry
        self.buffers = [None]*128

        # query all 128 channels, and set those for which the DAQ returns a non-zero
        # pointer, i.e. which are enabled
        for i in range(128):
            b = ctypes.POINTER(ctypes.c_uint16)()
            n = ctypes.c_uint()
            self.daq.get_buffer(i,ctypes.byref(n),ctypes.byref(b))
            if b:
                self.buffers[i] = RingBuffer(ctypes.c_uint16,n.value,b)

        # The counter of the next data that should be analyzed
        self.next = 0

    # Run the analysis. This function will be called periodically when a pre-defined number
    # of new data is available for all channels. Data in the range [from; to[ is guaranteed
    # to be available in all channel buffers
    def run(self,from,to):
        # Even if data is (still) available in the ring buffers starting from counter 'from',
        # we only want to analyze from 'next' because we analyzed all previous shots so far
        if self.next > from:
            from = self.next

        # loop over all shots
        for i_shot in range(from,to):
            print("SHOT #" + str(i_shot))
            # loop over all channels
            for i_channel in range(128):
                # Take action only for the allowed channels
                if self.buffers[i_channel] is not None:
                    # The first index in [i_channel] selects the channel. Subsequent values of
                    # this channel can be accessed via the () operator using the counter i_shot
                    print("Channel #" + str(i_channel) + " value: " + self.buffers[i_channel](i_shot))
