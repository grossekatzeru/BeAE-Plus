#!/bin/bash
# Проверка плагина "ds1" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
SRC=$(ls ../../plugins/ds1/*.cpp)
g++ -O2 -fPIC -shared -Wall -Wextra -o build/ds1.so $SRC -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/ds1_test ds1_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/ds1_test
