#!/bin/bash
# Проверка плагина "Фленджер" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/flanger.so ../../plugins/flanger/Flanger.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/flanger_test flanger_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/flanger_test
