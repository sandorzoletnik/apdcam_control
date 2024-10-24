import sys
import time
import os
import ctypes
from DAQ import *
from .ApdcamUtils import *

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
from ..Processor import *



class Measure(QtWidgets.QWidget):
    def __init__(self,parent):
        super(Measure,self).__init__(parent)
        self.gui = parent
        layout = QtWidgets.QVBoxLayout()
        self.setLayout(layout)

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


        DAQ().start_cmd_thread()

        daqGroup = QVGroupBox("DAQ settings and monitoring")
        layout.addWidget(daqGroup)
        h = QtWidgets.QHBoxLayout()
        daqGroup.addLayout(h)
        h.addWidget(QtWidgets.QLabel("Network buffer size: "))
        self.daqNetworkBufferSize = QtWidgets.QSpinBox()
        self.daqNetworkBufferSize.setMinimum(2)
        self.daqNetworkBufferSize.setMaximum(1<<20)
        self.daqNetworkBufferSize.setValue(DAQ().get_network_buffer_size())
        self.daqNetworkBufferSize.lineEdit().returnPressed.connect(lambda: DAQ().network_buffer_size(self.daqNetworkBufferSize.value()))
        h.addWidget(self.daqNetworkBufferSize)
        h.addStretch(1)

        h.addWidget(QtWidgets.QLabel("Sample buffer size: "))
        self.daqSampleBufferSize = QtWidgets.QSpinBox()
        self.daqSampleBufferSize.setMinimum(2)
        self.daqSampleBufferSize.setMaximum(1<<20)
        self.daqSampleBufferSize.setValue(DAQ().get_channel_buffer_size())
        self.daqSampleBufferSize.lineEdit().returnPressed.connect(lambda: DAQ().sample_buffer_size(self.daqSampleBufferSize.value()))
        h.addWidget(self.daqSampleBufferSize)
        h.addStretch(5)

        h = QtWidgets.QHBoxLayout()
        daqGroup.addLayout(h)
        h.addWidget(QtWidgets.QLabel("MTU: "))
        self.MTU_label = QtWidgets.QLabel()
        h.addWidget(self.MTU_label)
        h.addStretch(1)
        h.addWidget(QtWidgets.QLabel("OCTET: "))
        self.OCTET_label = QtWidgets.QLabel()
        h.addWidget(self.OCTET_label)
        h.addStretch(6)

        h = QtWidgets.QHBoxLayout()
        daqGroup.addLayout(h)
        h.addWidget(QtWidgets.QLabel("Processor threads: "))
        self.processorThreads = QtWidgets.QLabel("0")
        h.addWidget(self.processorThreads)
        h.addStretch(10)


        h = QtWidgets.QHBoxLayout()
        daqGroup.addLayout(h)
        g = QtWidgets.QGridLayout()
        #g.setContentsMargins(20,0,20,0)
        g.setSpacing(15)
        h.addLayout(g)
        h.addStretch(1)
        g.addWidget(QtWidgets.QLabel("STREAM"),0,0)
        g.addWidget(QtWidgets.QLabel("Net. thread"),0,1)
        g.addWidget(QtWidgets.QLabel("Ext. thread"),0,2)
        g.addWidget(QtWidgets.QLabel("Received packets"),0,3)
        g.addWidget(QtWidgets.QLabel("Lost packets"),0,4)
        g.addWidget(QtWidgets.QLabel("Netbuf content"),0,5)
        g.addWidget(QtWidgets.QLabel("Max. netbuf content"),0,6)
        g.addWidget(QtWidgets.QLabel("Samplebuf content"),0,7)
        g.addWidget(QtWidgets.QLabel("Max. Samplebuf content"),0,8)
        g.addWidget(QtWidgets.QLabel("Shots extracted"),0,9)
        self.streamDisplayColumns = 10

        self.streamDisplay = [[QtWidgets.QLabel() for i in range(self.streamDisplayColumns)] for j in range(Config.max_boards)]
        for i_adc in range(Config.max_boards):
            for j in range(self.streamDisplayColumns):
                if(j==0):
                    self.streamDisplay[i_adc][j].setText(str(i_adc))
                else:
                    self.streamDisplay[i_adc][j].setText("---")
                g.addWidget(self.streamDisplay[i_adc][j],i_adc+1,j)



        h = QtWidgets.QHBoxLayout()
        layout.addLayout(h)

        self.measureButton = QtWidgets.QPushButton("Start measurement")
        self.measureButton.clicked.connect(self.startMeasurement)
        self.measureButton.setToolTip("Start the measurement")
        h.addWidget(self.measureButton)

        self.stopMeasureButton = QtWidgets.QPushButton("Stop measurement");
        self.stopMeasureButton.clicked.connect(self.stopMeasurement)
        self.stopMeasureButton.setToolTip("Gracefully stop the data acquisition")
        h.addWidget(self.stopMeasureButton)

        self.abortMeasureButton = QtWidgets.QPushButton("Abort measurement");
        self.abortMeasureButton.clicked.connect(self.abortMeasurement)
        self.abortMeasureButton.setToolTip("Abort the data acquisition")
        h.addWidget(self.abortMeasureButton)

        self.messages = QtWidgets.QTextEdit(self)
        layout.addWidget(self.messages)

        layout.addStretch(1)

    def updateDaqState(self):
        self.processorThreads.setText(str(DAQ().processor_threads()))

        n_adc = DAQ().n_adc()
        for i_adc in range(Config.max_boards):
            if i_adc<n_adc:
                for j in range(self.streamDisplayColumns):
                    self.streamDisplay[i_adc][j].show()
                self.streamDisplay[i_adc][1].setText("run" if DAQ().network_thread_active(i_adc) else "")
                self.streamDisplay[i_adc][2].setText("run" if DAQ().extractor_thread_active(i_adc) else "")
                self.streamDisplay[i_adc][3].setText(str(DAQ().received_packets(i_adc)))
                self.streamDisplay[i_adc][4].setText(str(DAQ().lost_packets(i_adc)))
                self.streamDisplay[i_adc][5].setText(str(DAQ().network_buffer_content(i_adc)))
                self.streamDisplay[i_adc][6].setText(str(DAQ().max_network_buffer_content(i_adc)))
                self.streamDisplay[i_adc][7].setText(str(DAQ().channel_buffer_content_of_board(i_adc)))
                self.streamDisplay[i_adc][8].setText(str(DAQ().max_channel_buffer_content_of_board(i_adc)))
                self.streamDisplay[i_adc][9].setText(str(DAQ().extracted_shots_of_board(i_adc)))
            else:
                for j in range(self.streamDisplayColumns):
                    if j>0:
                        self.streamDisplay[i_adc][j].setText("")
                    self.streamDisplay[i_adc][j].hide()
                

    def show_message(self,msg):
        self.messages.append(msg)


    def abortMeasurement(self):
        DAQ().kill_all()

    def stopMeasurement(self):
        DAQ().stop(False)
        
    def startMeasurement(self):

        DAQ().get_net_parameters()
        self.MTU_label.setText(str(DAQ().get_mtu()))
        self.OCTET_label.setText(str(DAQ().get_octet()))

        masks = [ [True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False,
                   True,True,True,True,False,False,False,False] ]

        processors = [ProcessorTest(),"diskdump"]

        self.gui.cameraPolling(False)

        print("Calling self.gui.camera.measure")
        self.gui.camera.measure(channelMasks=masks,resolutionBits=14,processorTasks=processors)
        print("Returned from self.gui.camera.measure")

        # return

        # if not self.gui.status.connected:
        #     self.gui.show_error("Camera is not connected")
        #     return
        # self.gui.stopGuiUpdate()
        # self.gui.show_warning("After the measurement is completed, please re-start the GUI update manually by clicking on the corresponding button in the 'Main' tab")
        # time.sleep(1)
        # self.gui.saveSettings(ask=False)
        # self.gui.camera.measure(numberOfSamples=self.sampleNumber.value(),datapath=self.dataDirectory.text(),timeout=self.timeout.value()*1000)
