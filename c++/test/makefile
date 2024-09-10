include ../makefile.params

ALL=apdcam-data-recorder apdcam-fake-camera
all : $(ALL)

HEADERS = $(wildcard ../*.h)

apdcam-data-recorder : apdcam-data-recorder.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS) -std=c++20 -I.. -o $@ $< -L.. -lapdcam10g -ljsoncpp -pthread
apdcam-fake-camera : apdcam-fake-camera.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS) -std=c++20 -I.. -o $@ $< -L.. -lapdcam10g -ljsoncpp -pthread
udp-client : udp-client.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS) -std=c++20 -I.. -o $@ $< -L.. -lapdcam10g -ljsoncpp -pthread
udp-server : udp-server.C ../libapdcam10g.so $(HEADERS)
	g++ $(CXXFLAGS) -std=c++20 -I.. -o $@ $< -L.. -lapdcam10g -ljsoncpp -pthread

.PHONY : clean
clean:
	rm -f $(ALL) *.o *~
