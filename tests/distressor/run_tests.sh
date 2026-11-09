#!/bin/bash
# Проверка плагина "Компрессор «Дистрессор»" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/distressor.so ../../plugins/distressor/Distressor.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/distressor_test distressor_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/distressor_test
