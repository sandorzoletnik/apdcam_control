include ../makefile.params

ALL=apdcam-daq.exe apdcam-fake-camera.exe
all : $(ALL)

HEADERS = $(wildcard ../*.h)

LDFLAGS += -L.. -lapdcam10g 

apdcam-daq.exe : apdcam-daq.C ../libapdcam10g.so $(HEADERS)
	$(CXX) $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)
apdcam-fake-camera.exe : apdcam-fake-camera.C ../libapdcam10g.so $(HEADERS)
	$(CXX) $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)

# earlier test programs for UDP communication
udp-client : udp-client.C ../libapdcam10g.so $(HEADERS)
	$(CXX) $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)
udp-server : udp-server.C ../libapdcam10g.so $(HEADERS)
	$(CXX) $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)

.PHONY : clean
clean:
	rm -f $(ALL) *.o *~
