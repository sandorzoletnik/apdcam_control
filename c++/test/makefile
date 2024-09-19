include ../makefile.params

ALL=apdcam-data-recorder apdcam-fake-camera
all : $(ALL)

HEADERS = $(wildcard ../*.h)

LDFLAGS = -L.. -lapdcam10g # -ljsoncpp

apdcam-data-recorder : apdcam-data-recorder.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)
apdcam-fake-camera : apdcam-fake-camera.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)
udp-client : udp-client.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)
udp-server : udp-server.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS)  -I.. -o $@ $< $(LDFLAGS)

.PHONY : clean
clean:
	rm -f $(ALL) *.o *~
