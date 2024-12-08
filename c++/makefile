
include makefile.params

SRCS = $(wildcard *.C)
DEPS = $(SRCS:.C=.d)
OBJS = $(SRCS:.C=.o)

all : libapdcam10g.so tools

.PHONY: ubuntu-20.04

ubuntu-20.04:
	docker cp . ubuntu_20_04:/home/developer/apdcam/
	docker exec ubuntu_20_04 sh -c "cd /home/developer/apdcam/c++; make clean; make"
	docker cp ubuntu_20_04:/home/developer/apdcam/c++/libapdcam10g.so ubuntu-20.04/
	docker cp ubuntu_20_04:/home/developer/apdcam/c++/tools/apdcam-daq.exe ubuntu-20.04/
	docker cp ubuntu_20_04:/home/developer/apdcam/c++/tools/apdcam-fake-camera.exe ubuntu-20.04/

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


