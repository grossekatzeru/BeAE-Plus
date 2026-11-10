#!/bin/bash
# Проверка плагина "tube_screamer" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
SRC=$(ls ../../plugins/tube_screamer/*.cpp)
g++ -O2 -fPIC -shared -Wall -Wextra -o build/tube_screamer.so $SRC -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/tube_screamer_test tube_screamer_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/tube_screamer_test
