#!/bin/bash
# Проверка ядра внешних кодировщиков на Linux (ExtEncoder.cpp): настройки, командные строки, временный WAV,
# поиск и запуск программ (поддельные lame/oggenc/flac/opusenc в временной папке).
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++11 -O2 -Wall -Wextra -I ../../Core-src -o build/encoder_test encoder_test.cpp ../../Core-src/ExtEncoder.cpp
./build/encoder_test
