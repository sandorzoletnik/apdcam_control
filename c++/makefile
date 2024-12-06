
include makefile.params

SRCS = $(wildcard *.C)
DEPS = $(SRCS:.C=.d)
OBJS = $(SRCS:.C=.o)

all : libapdcam10g.so tools

.PHONY: tools clean
tools:
	cd tools; $(MAKE)

clean :
	rm -f *.o *.so *.d *~ *.dat
	cd tools; $(MAKE) clean

# uncomment this
include $(DEPS)

# -fPIC is required. It is specified in CXXFLAGS
%.o : %.C
	$(CXX) -fPIC $(CXXFLAGS) -O2 -c -o $@ $<

%.d : %.C
	$(CXX) $(CXXFLAGS) -M -MF $@ $^

libapdcam10g.so : $(OBJS)
	echo $(OBJS)
	$(CXX) -shared -o $@ $^  $(LDFLAGS)


