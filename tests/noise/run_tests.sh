#!/bin/bash
# Проверка ядра шумодава (NoiseCore) на Linux; на Haiku не нужна.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/noise_test noise_test.cpp ../../Core-src/NoiseCore.cpp
./build/noise_test
