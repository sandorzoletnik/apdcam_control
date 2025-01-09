from DAQ import *
import Config


n_adc = 1
resolutionBits = [14]*n_adc

processorTasks = [
    ("diskdump",{"process_period":100})  # a tuple, first element is defining the DAQ() member function name add_processor_diskdump, the second is a set of keyword args
]



DAQ().clear_processors()

for task in processorTasks:
    # if the given task is a tuple, its first element must be a string which indicates a member function of the DAQ
    # class like this: "add_processor_"+XXX must be member function of DAQ, where XXX is the first member of the tuple.
    # subsequent members of the tuple must be function arguments to this function.
    if type(task) is tuple:
        if type(task[0]) is not str:
            print("The first element of a tuple must be a string")
            die()
        f = getattr(DAQ(),"add_processor_"+task[0])
        if f is None:
            print("The shared DAQ library 'libapdcam10g.so' does not have a function named '"+task+"'")
            die()
        # If this tuple has 2 elements, and the 2nd element is a dictionary, it is interpreted as keyword arguments
        if len(task)==2 and isinstance(task[1],dict):
            f(**task[1])
            # otherwise interpret all elements as the tuple after the first one as arguments
        else:
            f(*task[1:])
    elif type(task) is str:
        f = getattr(DAQ(),"add_processor_"+task)
        if f is None:
            print("The shared DAQ library 'libapdcam10g.so' does not have a function named '"+task+"'")
        else:
            f()
    else:
        DAQ().add_processor_python(task)
        
print("Processors set up")

# By default, create an "all enabled" mask
channelMasks = [[True]*Config.channels_per_board]*n_adc
DAQ().channel_masks(channelMasks)
print("masks ok")

DAQ().resolution_bits(resolutionBits)
print("resolution ok")
DAQ().init(True)

print("Starting DAQ")

DAQ().start_cmd_thread()
DAQ().start(False)

