# On Haiku:  make
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -I/boot/system/develop/headers/private/audio

usb_record_test: usb_record_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ usb_record_test.cpp -lroot

clean:
	rm -f usb_record_test
