#!/usr/bin/python3

import sys
import os
dir = os.path.dirname(__file__)
sys.path.append(dir + '/../../')
import apdcam_control
#from DAQ import *

# def trace(frame, event, arg):
#     print("%s, %s:%d" % (event, frame.f_code.co_filename, frame.f_lineno))
#     return trace

# def test():
#     print("Line 8")
#     print("Line 9")

# sys.settrace(trace)
print("Loading DAQ")
apdcam_control.DAQ()
print("loaded")

#sys.path.append(dir + '/../../apdcam_control')
from ApdcamGui import *
#app = apdcam_control.gui.ApdcamGuiApp()
app = ApdcamGuiApp()
sys.exit(app.exec())

