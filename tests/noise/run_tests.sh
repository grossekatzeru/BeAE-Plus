#!/bin/bash
# Проверка DSP "Шумодав" на Linux (на Haiku не нужна).
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/noise_test noise_test.cpp ../../Core-src/NoiseReducer.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/noise_test
