#a.out : proba.C ring_buffer.h
#	g++ -std=c++20 -Wno-interference-size proba.C

SRCS = $(wildcard *.C)
DEPS = $(SRCS:.C=.d)
OBJS = $(SRCS:.C=.o)

all : libapdcam10g.so tools

.PHONY: tools clean
tools:
	cd tools; $(MAKE)

clean :
	rm -f *.o *.so *.d *~
	cd tools; $(MAKE) clean

# uncomment this
include $(DEPS)

include makefile.params

%.o : %.C
	g++ $(CXXFLAGS) -DFOR_PYTHON -fPIC -O2 -std=c++20 -Wno-interference-size -c -o $@ $<

%.d : %.C
	g++ -DFOR_PYTHON -M -MF $@ $^

libapdcam10g.so : $(OBJS)
	echo $(OBJS)
	g++ -fPIC -shared -o $@ $^


