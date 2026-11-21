# Haiku:  make   ->  BeAETap
CXX ?= g++
CXXFLAGS ?= -O2 -Wall

BeAETap: main.cpp TapNode.cpp TapNode.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp TapNode.cpp -lbe -lmedia

clean:
	rm -f BeAETap
