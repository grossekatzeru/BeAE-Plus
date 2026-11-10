#!/bin/bash
# Проверка ядра тюнера на Linux: определение высоты, ноты, пресеты.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/tuner_test tuner_test.cpp ../../Core-src/TunerCore.cpp
./build/tuner_test
