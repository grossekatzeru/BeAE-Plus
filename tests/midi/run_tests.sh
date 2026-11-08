#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/midi_test midi_test.cpp ../../Core-src/MidiFile.cpp ../../Core-src/SoundFont.cpp ../../Core-src/MidiSynth.cpp -lm
./build/midi_test
