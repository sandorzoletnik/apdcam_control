#a.out : proba.C ring_buffer.h
#	g++ -std=c++20 -Wno-interference-size proba.C

include makefile.params

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

%.o : %.C
	g++ $(CXXFLAGS) -fPIC -O2 -c -o $@ $<

%.d : %.C
	g++ $(CXXFLAGS) -M -MF $@ $^

libapdcam10g.so : $(OBJS)
	echo $(OBJS)
	g++ -fPIC -shared -o $@ $^ $(LDFLAGS)


