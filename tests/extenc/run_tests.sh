#!/bin/bash
# Проверка ядра внешних кодировщиков (ExtEncoder) на Linux: командные строки, настройки, WAV, запуск процессов.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/extenc_test extenc_test.cpp ../../Core-src/ExtEncoder.cpp
./build/extenc_test
