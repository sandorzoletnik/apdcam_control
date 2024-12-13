
from DAQ import *
from Processor import *

DAQ().get_net_parameters()
DAQ().add_processor_python(ProcessorTest())
DAQ().add_processor_diskdump()
DAQ().resolution_bits([14]*4)
DAQ().channel_masks([[True]*32]*4)
DAQ().init(True)
print("Starting DAQ")
print("------- !!!!!!!! try with DAQ().start(True) -------------")
DAQ().start(True)
