import ctypes
import os
import threading

from RingBuffer import *

'''
A wrapper module to load the C++ shared library (the low-leval data acquisition backend), and set up the
argument types and return types of the C functions defined therein. It mimics a singleton type, as it will
be apparent from the following example

Usage:

import daq
daq.instance().get_net_parameters()  # no need to load a priori the DLL, daq.instance() takes care of that
daq.instance().add_processor_diskdump()
daq.instance().init(True)
daq.instance().start(False)
daq.instance().wait_finish()

'''


# Load the shared library if it hasnt been loaded yet, and set up the argument and return types
# of the functions defined therein
def instance():
    if instance.instance_ is None:
        dir = os.path.dirname(__file__)
        dllpath = os.path.join(dir,"../c++/libapdcam10g.so")
        instance.instance_ = ctypes.CDLL(dllpath)

        if instance.instance_ is None:
            return None

        instance.instance_.write_settings.restype = None
        instance.instance_.write_settings.argtypes = [ctypes.c_char_p]
        
        instance.instance_.start.restype = None
        instance.instance_.start.argtypes = [ctypes.c_bool]

        instance.instance_.stop.restype = None
        instance.instance_.stop.argtypes = [ctypes.c_bool]

        instance.instance_.dual_sata.restype = None
        instance.instance_.dual_sata.argtypes = [ctypes.c_bool]

        instance.instance_.debug.restype = None
        instance.instance_.debug.argtypes = [ctypes.c_bool]

        instance.instance_.init.restype = None
        instance.instance_.init.argtypes = [ctypes.c_bool]

        instance.instance_.get_buffer.restype = None
        instance.instance_.get_buffer.argtypes = [ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16))]

        # Overwrite the C++ library 'add_processor_python'
        orig_add_processor_python = instance.instance_.add_processor_python
        def add_processor_python(p):
            # Call the underlying C++ function to create a C++ 'processor_python' object and add it to the C++ daq.
            # That class has no connection to the actual python class that is doing the real job. The C++ python_processor
            # is only a bookkeeper that sets the flag that allows the next python object to run, and then obtains
            # its return value
            orig_add_processor_python()
            # In addition, add this python processor to the list of processors
            instance.python_processors_.append(p)
        instance.instance_.add_processor_python = add_processor_python

        # SAve the original 'clear_processors' function of the DLL
        orig_clear_processors = instance.instance_.clear_processors
        # define a new one which just calls the old one, and also clears the list of python processors
        def clear_processors():
            orig_clear_processors()
            instance.python_processors_ = []
        # set the new one
        instance.instance_.clear_processors = clear_processors

        # Now modify the 'start' function of the DLL, add extra functionality (running the python processors in a
        # new thread if we have defined such processors)
        orig_start = instance.instance_.start
        def start(wait):
            # Call this function to make sure we set the C++/python communication flag to
            # the right state
            instance.instance_.python_analysis_done(0)
            if len(instance.python_processors_) > 0:

                print("Change hard-coded value of 128")
                buffers = [None]*128
                for i in range(len(buffers)):
                    b = ctypes.POINTER(ctypes.c_uint16)()
                    n = ctypes.c_uint()
                    instance.instance_.get_buffer(i,ctypes.byref(n),ctypes.byref(b))
                    if b:
                        buffers[i] = RingBuffer(ctypes.c_uint16,n.value,b)


                # Define a function which loops until we stop it, and in each loop it
                # calls all processors
                def processor_loop():
                    # loop until the C++ DAQ raises the flag 'python_analysis_stop'
                    while not instance.instance_.python_analysis_stop():
                        for p in instance.python_processors_:
                            from_counter = ctypes.c_size_t()
                            to_counter   = ctypes.c_size_t()
                            needs = ctypes.c_size_t()
                            instance.instance_.python_analysis_wait_for_data(ctypes.byref(from_counter),ctypes.byref(to_counter))
                            if instance.instance_.python_analysis_stop():
                                break;
                            needs.value = p.run(buffers,from_counter.value,to_counter.value)
                            instance.instance_.python_analysis_done(needs)

                instance.python_processor_thread_ = threading.Thread(target=processor_loop)
                instance.python_processor_thread_.start()
            orig_start(wait)
            
        instance.instance_.start = start

    return instance.instance_;

instance.instance_ = None
instance.python_processors_ = []
        
