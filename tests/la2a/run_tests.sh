#!/bin/bash
# Проверка плагина "Оптический компрессор (LA-2A)" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/la2a.so ../../plugins/la2a/La2a.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/la2a_test la2a_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/la2a_test
