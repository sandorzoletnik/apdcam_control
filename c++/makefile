CXX = g++ #/opt/gcc-14.2.0/bin/g++-14.2.0
LD = g++ #/opt/gcc-14.2.0/bin/g++-14.2.0

CXXFLAGS := -Wno-interference-size -std=c++23 -pthread -DFOR_PYTHON 
LDFLAGS := -static-libgcc -static-libstdc++ 

# Check if we are in docker ubuntu-20.04 where the g++ compiler has been installed into this location
ifneq (,$(wildcard /opt/gcc-14.2.0/bin/g++-14.2.0))
  CXX = /opt/gcc-14.2.0/bin/g++-14.2.0
  LD = /opt/gcc-14.2.0/bin/g++-14.2.0
endif

HEDAERS = $(wildcard *.h)
SRCS = $(wildcard *.C)
CINCS = $(wildcard *.Cinc)
DEPS = $(SRCS:.C=.d)
OBJS = $(SRCS:.C=.o)

OS = $(shell ./getos)

all : binaries/$(OS)/libapdcam10g.so binaries/$(OS)/apdcam-daq.exe binaries/$(OS)/apdcam-fake-camera.exe

.PHONY: Ubuntu-20.04
.PHONY:  clean

# for some reason thi sdoes not work...
Ubuntu-% : binaries/Ubuntu-%

# it needs to be explicitly written
Ubuntu-20.04 : binaries/Ubuntu-20.04

binaries/Ubuntu-%: $(SRCS) $(HEADERS) $(CINCS)
	$(eval CONT=$(shell basename $@))
	rm -rf binaries/$@ # remove it, it will be re-created in the docker container and copied back here
	docker start $(CONT)
	docker exec $(CONT) sh -c "rm -rf /home/developer/apdcam/c++" # remove entire c++ directory from docker
	cd ..; docker cp c++ $(CONT):/home/developer/apdcam/; cd c++  # copy entire c++ directory to docker
	docker exec $(CONT) sh -c "cd /home/developer/apdcam/c++; make clean; make"  # make clean on the VM 
	docker cp $(CONT):/home/developer/apdcam/c++/$@ binaries/ # copy back

binaries/$(OS)/libapdcam10g.so : $(OBJS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) -shared -o $@ $(OBJS)  $(LDFLAGS)

binaries/$(OS)/apdcam-daq.exe:  tools/apdcam-daq.C  binaries/$(OS)/libapdcam10g.so $(HEADERS) $(CINCS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(LDFLAGS) -Lbinaries/$(OS) -lapdcam10g
binaries/$(OS)/apdcam-fake-camera.exe:  tools/apdcam-fake-camera.C binaries/$(OS)/libapdcam10g.so $(HEADERS) $(CINCS)
	if [ ! -d binaries/$(OS) ] ; then mkdir binaries/$(OS); fi
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(LDFLAGS) -Lbinaries/$(OS) -lapdcam10g

binaries/$(OS):
	mkdir $@

clean :
	rm -f *.o binaries/$(OS)/* *.d *~ *.dat

# uncomment this
include $(DEPS)

# -fPIC is required. It is specified in CXXFLAGS
%.o : %.C $(HEADERS)
	$(CXX) -fPIC $(CXXFLAGS) -O2 -c -o $@ $<

%.d : %.C
	$(CXX) $(CXXFLAGS) -M -MF $@ $^



