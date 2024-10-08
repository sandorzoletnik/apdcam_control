import ctypes
import os
import threading
import Config
from RingBuffer import *

'''
A wrapper module to load the C++ shared library (the low-leval data acquisition backend), and set up the
argument types and return types of the C functions defined therein. It mimics a singleton type, as it will
be apparent from the following example

Do not forget that the C++ DAQ system can be accessed by the DAQ() function call (don't forget the parentheses!)
which returns the single instance of the C++ backend. 

Usage:

from DAQ import *
DAQ().get_net_parameters()  # no need to load a priori the DLL, DAQ.instance() takes care of that
DAQ().add_processor_diskdump()
DAQ().init(True)
DAQ().start(False)
DAQ().wait_finish()

'''

# Convert a python list to a native C array of type 'ctype', if 'l' is a simple list,
# or to a C nested array (array of pointers) if 'l' is a nested list, i.e. if each of its elements
# are themselves lists.
# ctype is the native C value type, for example ctypes.c_int, ctypes.c_uint, ctypes.c_bool, etc
def convertToCArray(l,ctype):
    # for non-lists, or empty lists, we return None
    if type(l) is not list or len(l)==0:
        return None;
    
    # a 1-dimensional list
    if type(l[0]) is not list:
        n = len(l)
        # Create the C native array type of a given length
        result = (ctype*n)()
        for i in range(n):
            result[i] = l[i]
        return result
        
    else:
        n1 = len(l)
        n2 = len(l[0])  # we assume all list elements (thmeselves being lists) have the same size so just take the first one
        # Create a native C array with size n1 of pointers to type 'ctype'
        result = (ctypes.POINTER(ctype)*n1)()
        for i in range(n1):
            # For each element create a new native C array with size n2
            result[i] = (ctype*n2)()
            for j in range(n2):
                result[i][j] = l[i][j]
        return result



# Load the shared library if it hasnt been loaded yet, and set up the argument and return types
# of the functions defined therein
def DAQ():
    if DAQ.instance_ is None:
        dir = os.path.dirname(__file__)
        dllpath = os.path.join(dir,"c++/libapdcam10g.so")
        DAQ.instance_ = ctypes.CDLL(dllpath)

        if DAQ.instance_ is None:
            return None

        DAQ.instance_.get_net_parameters.restype = None
        DAQ.instance_.get_net_parameters.argtypes = []

        DAQ.instance_.write_settings.restype = None
        DAQ.instance_.write_settings.argtypes = [ctypes.c_char_p]
        
        DAQ.instance_.start.restype = None
        DAQ.instance_.start.argtypes = [ctypes.c_bool]

        DAQ.instance_.stop.restype = None
        DAQ.instance_.stop.argtypes = [ctypes.c_bool]

        DAQ.instance_.dual_sata.restype = None
        DAQ.instance_.dual_sata.argtypes = [ctypes.c_bool]

        DAQ.instance_.channel_masks.restype = None
        DAQ.instance_.channel_masks.argtypes = [ctypes.POINTER(ctypes.POINTER(ctypes.c_bool)), ctypes.c_int]

        DAQ.instance_.resolution_bits.restype = None
        DAQ.instance_.resolution_bits.argtypes = [ctypes.POINTER(ctypes.c_uint), ctypes.c_int]

        DAQ.instance_.add_processor_diskdump.restype = None
        DAQ.instance_.add_processor_diskdump.argtypes = []

        DAQ.instance_.add_processor_python.restype = None
        DAQ.instance_.add_processor_python.argtypes = []

        DAQ.instance_.debug.restype = None
        DAQ.instance_.debug.argtypes = [ctypes.c_bool]

        DAQ.instance_.init.restype = None
        DAQ.instance_.init.argtypes = [ctypes.c_bool]

        DAQ.instance_.get_buffer.restype = None
        DAQ.instance_.get_buffer.argtypes = [ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16))]

        # Tell the DAQ to wait for all threads, and finish
        DAQ.instance_.wait_finish.restype = None
        DAQ.instance_.wait_finish.argtypes = []

        # Overwrite the C++ library's 'channel_masks' routine by a wrapper which accepts python list
        orig_channel_masks = DAQ.instance_.channel_masks
        def channel_masks(m):
            orig_channel_masks(convertToCArray(m,ctypes.c_bool),len(m))
        DAQ.instance_.channel_masks = channel_masks

        # Overwrite the C++ library's 'resolution_bits' routine by a wrapper which accepts a python list
        orig_resolution_bits = DAQ.instance_.resolution_bits
        def resolution_bits(r):
            orig_resolution_bits(convertToCArray(r,ctypes.c_uint),len(r))
        DAQ.instance_.resolution_bits = resolution_bits

        # Overwrite the C++ library 'add_processor_python'
        orig_add_processor_python = DAQ.instance_.add_processor_python
        def add_processor_python(p):
            # Call the underlying C++ function to create a C++ 'processor_python' object and add it to the C++ daq.
            # That class has no connection to the actual python class that is doing the real job. The C++ python_processor
            # is only a bookkeeper that sets the flag that allows the next python object to run, and then obtains
            # its return value
            orig_add_processor_python()
            # In addition, add this python processor to the list of processors
            DAQ.python_processors_.append(p)
        DAQ.instance_.add_processor_python = add_processor_python

        # SAve the original 'clear_processors' function of the DLL
        orig_clear_processors = DAQ.instance_.clear_processors
        # define a new one which just calls the old one, and also clears the list of python processors
        def clear_processors():
            orig_clear_processors()
            DAQ.python_processors_ = []
        # set the new one
        DAQ.instance_.clear_processors = clear_processors

        # Now modify the 'start' function of the DLL, add extra functionality (running the python processors in a
        # new thread if we have defined such processors)
        # The new wrapper 'start' member function of DAQ will run all activities in separate threads - including
        # the loop of the (eventual) python analysis tasks (which is started from python) If wait==False, 
        # it returns immediately after starting these threads
        orig_start = DAQ.instance_.start
        def start(wait):
            # Call this function to make sure we set the C++/python communication flag to
            # the right state
            DAQ.instance_.python_analysis_done(0)
            if len(DAQ.python_processors_) > 0:

                buffers = [None]*Config.max_channels
                for i in range(len(buffers)):
                    b = ctypes.POINTER(ctypes.c_uint16)()
                    n = ctypes.c_uint()
                    DAQ.instance_.get_buffer(i,ctypes.byref(n),ctypes.byref(b))
                    if b:
                        buffers[i] = RingBuffer(ctypes.c_uint16,n.value,b)


                # Define a function which loops until we stop it, and in each loop it
                # calls all processors
                def processor_loop():
                    # loop until the C++ DAQ raises the flag 'python_analysis_stop'
                    while not DAQ.instance_.python_analysis_stop():
                        for p in DAQ.python_processors_:
                            from_counter = ctypes.c_size_t()
                            to_counter   = ctypes.c_size_t()
                            needs = ctypes.c_size_t()
                            DAQ.instance_.python_analysis_wait_for_data(ctypes.byref(from_counter),ctypes.byref(to_counter))
                            if DAQ.instance_.python_analysis_stop():
                                break;
                            needs.value = p.run(buffers,from_counter.value,to_counter.value)
                            DAQ.instance_.python_analysis_done(needs)

                DAQ.python_processor_thread_ = threading.Thread(target=processor_loop)
                DAQ.python_processor_thread_.start()
            orig_start(wait)
            
        DAQ.instance_.start = start

        # Overwrite the 'statistics' function of the C++ library with a wrapper that returns the results in a python list
        orig_statistics = DAQ.instance_.statistics
        def statistics():
            n_packets = ctypes.c_uint()
            n_shots   = ctypes.c_uint()
            orig_statistics(ctypes.byref(n_packets),ctypes.byref(n_shots))
            return [n_packets.value,n_shots.value]
        DAQ.instance_.statistics = statistics

        # Replace the 'status' function of the C++ library by a wrapper that returns the results (number of active
        # network, extractor and processor threads) in a python list
        orig_status = DAQ.instance_.status
        def status():
            n_network_threads = ctypes.c_uint()
            n_extractor_threads = ctypes.c_uint()
            n_processor_thread = ctypes.c_uint()
            orig_status(ctypes.byref(n_network_threads),ctypes.byref(n_extractor_threads),ctypes.byref(n_processor_thread))
            return [n_network_threads.value,n_extractor_threads.value,n_processor_thread.value]
        DAQ.instance_.status = status

    return DAQ.instance_;

DAQ.instance_ = None
DAQ.python_processors_ = []
        
