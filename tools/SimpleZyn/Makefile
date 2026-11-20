# Haiku:  make        -> SimpleZyn
# Anywhere: make test -> checks OSC, the bank scanner and the engine launcher (no Haiku needed)
CXX ?= g++
CXXFLAGS ?= -O2 -Wall
CORE = Osc.cpp Engine.cpp Banks.cpp ZynControl.cpp SmfFile.cpp
GUI = InstrumentEditor.cpp MidiPlayer.cpp SongPlayer.cpp

SimpleZyn: main.cpp $(GUI) $(CORE) Osc.h Engine.h Banks.h ZynControl.h InstrumentEditor.h MidiPlayer.h SongPlayer.h SmfFile.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(GUI) $(CORE) -lbe -lmidi2 -ltracker -lnetwork
	-if command -v rc >/dev/null 2>&1; then rc -o SimpleZyn.rsrc SimpleZyn.rdef && xres -o $@ SimpleZyn.rsrc; fi

test: test_core.cpp $(CORE) Osc.h Engine.h Banks.h ZynControl.h SmfFile.h
	$(CXX) -std=c++11 -Wall -pthread -o test_core test_core.cpp $(CORE)
	./test_core

clean:
	rm -f SimpleZyn SimpleZyn.rsrc test_core
