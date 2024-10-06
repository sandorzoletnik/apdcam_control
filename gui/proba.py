#!/usr/bin/python3

import html
import DAQ

f = getattr(DAQ.instance(),"test")
f()
