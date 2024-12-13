
include makefile.params

HEDAERS = $(wildcard *.h)
SRCS = $(wildcard *.C)
DEPS = $(SRCS:.C=.d)
OBJS = $(SRCS:.C=.o)

OS = $(shell ./getos)

all : binaries/$(OS)/libapdcam10g.so binaries/$(OS)/apdcam-daq.exe binaries/$(OS)/apdcam-fake-camera.exe

.PHONY: Ubuntu-20.04
.PHONY:  clean

Ubuntu-20.04:
	rm -rf binaries/$@ # remove it, it will be re-created on the docker VM
	docker exec ubuntu_20_04 sh -c "rm -rf /home/developer/apdcam/c++" # remove entire c++ directory from docker
	cd ..; docker cp c++ ubuntu_20_04:/home/developer/apdcam/; cd c++  # copy entire c++ directory to docker
	docker exec ubuntu_20_04 sh -c "cd /home/developer/apdcam/c++; make clean; make"  # make clean on the VM 
	docker cp ubuntu_20_04:/home/developer/apdcam/c++/binaries/Ubuntu-20.04 binaries/ # copy back

binaries/$(OS)/libapdcam10g.so : $(OBJS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) -shared -o $@ $(OBJS)  $(LDFLAGS)

binaries/$(OS)/apdcam-daq.exe:  tools/apdcam-daq.C  binaries/$(OS)/libapdcam10g.so $(HEADERS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(LDFLAGS) -Lbinaries/$(OS) -lapdcam10g
binaries/$(OS)/apdcam-fake-camera.exe:  tools/apdcam-fake-camera.C binaries/$(OS)/libapdcam10g.so $(HEADERS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(LDFLAGS) -Lbinaries/$(OS) -lapdcam10g

binaries/$(OS):
	mkdir $@

clean :
	rm -f *.o binaries/$(OS)/* *.d *~ *.dat

# uncomment this
include $(DEPS)

# -fPIC is required. It is specified in CXXFLAGS
%.o : %.C
	$(CXX) -fPIC $(CXXFLAGS) -O2 -c -o $@ $<

%.d : %.C
	$(CXX) $(CXXFLAGS) -M -MF $@ $^



