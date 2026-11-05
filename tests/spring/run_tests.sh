#!/bin/bash
# Проверка DSP "Ламповое эхо" на Linux (на Haiku не нужна).
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/spring_test spring_test.cpp ../../Core-src/SpringReverb.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/spring_test
