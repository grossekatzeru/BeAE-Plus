#!/bin/bash
# Проверка микшера и цепочек эффектов на Linux.
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -shared -fPIC -O2 -o build/test_plugin.so ../ladspa/test_plugin.c -lm
g++ -std=c++17 -Wall -I ../../Core-src -o build/mixer_test mixer_test.cpp ../../Core-src/Mixer.cpp ../../Core-src/Envelope.cpp ../../Core-src/KeyCache.cpp ../../Core-src/BeFxSidechain.cpp ../../Core-src/Metronome.cpp ../../Core-src/FxChain.cpp ../../Core-src/Vst2Host.cpp ../../Core-src/LadspaHost.cpp -ldl -pthread
./build/mixer_test build
