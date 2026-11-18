# Haiku:  make   ->  ToneSource
CXX ?= g++
CXXFLAGS ?= -O2 -Wall

ToneSource: main.cpp ToneNode.cpp ToneNode.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp ToneNode.cpp -lbe -lmedia

clean:
	rm -f ToneSource
