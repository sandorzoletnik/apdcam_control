#!/usr/bin/python3

import html
#import daq


class A:
    def __init__(self):
        self.data = [1,2,3,4,5]

    def __getitem__(self,index):
        return self.data[index]

    def __setitem__(self,index,newvalue):
        self.data[index] = newvalue

    def show(self):
        print(self.data)
    
a = A()
a[2] = 10
a.show()
