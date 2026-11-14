#!/bin/bash
# Проверка ядра огибающих автоматизации на Linux.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/envelope_test envelope_test.cpp ../../Core-src/Envelope.cpp
./build/envelope_test
