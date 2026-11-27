#!/bin/bash
# Проверка таблицы задержек по картам на Linux.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/cardlatency_test cardlatency_test.cpp
./build/cardlatency_test
