#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/tempo_test tempo_test.cpp ../../Core-src/TempoDetect.cpp -lm
./build/tempo_test
