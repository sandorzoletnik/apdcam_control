import sys
import time
import os
import ctypes

import importlib
from .QtVersion import QtVersion
QtWidgets = importlib.import_module(QtVersion+".QtWidgets")
QtGui = importlib.import_module(QtVersion+".QtGui")
QtCore = importlib.import_module(QtVersion+".QtCore")
Qt = QtCore.Qt

# from PyQt6.QtWidgets import QApplication, QWidget,  QFormLayout, QVBoxLayout, QHBoxLayout, QGridLayout, QTabWidget, QLineEdit, QDateEdit, QPushButton, QTextEdit, QGroupBox, QLabel, QSpinBox, QCheckBox
# from PyQt6.QtCore import Qt
from .ApdcamUtils import *
from .RingBuffer import *
from .GuiMode import *
from functools import partial


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


class Measure(QtWidgets.QWidget):
    def __init__(self,parent):
        super(Measure,self).__init__(parent)
        self.gui = parent
        layout = QtWidgets.QVBoxLayout()
        self.setLayout(layout)

        self.daq = None

        h = QtWidgets.QHBoxLayout()
        layout.addLayout(h)
        h.addWidget(QtWidgets.QLabel("Sample number: "))
        self.sampleNumber = QtWidgets.QSpinBox()
        self.sampleNumber.settingsName = "Sample number"
        self.sampleNumber.setMinimum(1)
        self.sampleNumber.setMaximum(1000000)
        self.sampleNumber.setValue(10)
        self.sampleNumber.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
        self.sampleNumber.lineEdit().returnPressed.connect(lambda: self.gui.camera.setSampleNumber(self.sampleNumber.value()))
        self.sampleNumber.setToolTip("Set the number of samples to acquire")
        h.addWidget(self.sampleNumber)

        h.addStretch(1)

        h.addWidget(QtWidgets.QLabel("Timeout [s]: "))
        self.timeout = QtWidgets.QSpinBox()
        self.timeout.settingsName = "Timeout"
        self.timeout.setMinimum(0)
        self.timeout.setValue(100)
        self.timeout.setToolTip("Specify the timeout of the data acquisition. It will terminate after this time is passed")
        h.addWidget(self.timeout)
        h.addStretch(10)

        h = QtWidgets.QHBoxLayout()
        layout.addLayout(h)
        h.addWidget(QtWidgets.QLabel("Data directory: "))
        self.dataDirectory = QtWidgets.QLineEdit()
        self.dataDirectory.settingsName = "Data directory"
        self.dataDirectory.setToolTip("Directory for storing the recorded data from the camera")
        self.dataDirectory.setText("/home/apdcam/tmp")
#        self.dataDirectory.setText("/home/barna/tmp/apdcam")
        h.addWidget(self.dataDirectory)
        self.dataDirectoryDialogButton = QtWidgets.QPushButton("PICK")
        h.addWidget(self.dataDirectoryDialogButton)
        self.dataDirectoryDialogButton.clicked.connect(lambda: self.dataDirectory.setText(str(QtWidgets.QFileDialog.getExistingDirectory(self, "Select Directory"))))

        self.measureButton = QtWidgets.QPushButton("Start measurement")
        self.measureButton.clicked.connect(self.measure)
        self.measureButton.setToolTip("Start the measurement")
        layout.addWidget(self.measureButton)

        self.messages = QtWidgets.QTextEdit(self)
        layout.addWidget(self.messages)

        layout.addStretch(1)

    def show_message(self,msg):
        self.messages.append(msg)

    def load_daq(self,sopath):
        if self.daq is not None:
            return True

        self.daq = ctypes.CDLL(sopath)
        if self.daq is None:
            return False

        self.daq.write_settings.restype = None
        self.daq.write_settings.argtypes = [ctypes.c_char_p]
        
        self.daq.start.restype = None
        self.daq.start.argtypes = [ctypes.c_bool]

        self.daq.stop.restype = None
        self.daq.stop.argtypes = [ctypes.c_bool]

        self.daq.dual_sata.restype = None
        self.daq.dual_sata.argtypes = [ctypes.c_bool]

        self.daq.debug.restype = None
        self.daq.debug.argtypes = [ctypes.c_bool]

        self.daq.init.restype = None
        self.daq.init.argtypes = [ctypes.c_bool]

        self.daq.get_buffer.restype = None
        self.daq.get_buffer.argtypes = [ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16))]

        return True

    def measure(self):

        dir = os.path.dirname(__file__)
        dllpath = os.path.join(dir,"../c++/libapdcam10g.so")
        if not self.load_daq(dllpath):
            print("FAILED TO LOAD " + dllpath)
            return

        self.daq.debug(False)
        self.daq.get_net_parameters()
        self.daq.add_processor_diskdump();
        #self.daq.dual_sata(self.getDualSata())
        
        masks = [ [True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False] ]

        self.daq.channel_masks(convertToCArray(masks,ctypes.c_bool),len(masks))
        res = [14]
        self.daq.resolution_bits(convertToCArray(res,ctypes.c_uint),len(res))
        self.daq.init(True);

        buffers = [None]*128
        for i in range(128):
            b = ctypes.POINTER(ctypes.c_uint16)()
            n = ctypes.c_uint()
            self.daq.get_buffer(i,ctypes.byref(n),ctypes.byref(b))
            if b:
                buffers[i] = RingBuffer(ctypes.c_uint16,n.value,b)

        self.daq.write_settings(b"apdcam-daq.cnf");
#        self.daq.dump()
        self.daq.start(False)

        print("Python has finished starting the DAQ")
        time.sleep(10)

        print("Python waiting for threads to finish")
        self.daq.wait_finish()
        print("Python DAQ has finished")

        return

        if not self.gui.status.connected:
            self.gui.show_error("Camera is not connected")
            return
        self.gui.stopGuiUpdate()
        self.gui.show_warning("After the measurement is completed, please re-start the GUI update manually by clicking on the corresponding button in the 'Main' tab")
        time.sleep(1)
        self.gui.saveSettings(ask=False)
        self.gui.camera.measure(numberOfSamples=self.sampleNumber.value(),datapath=self.dataDirectory.text(),timeout=self.timeout.value()*1000)
