#!/bin/bash
# Проверка ядра измерения задержки записи на Linux.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/latency_test latency_test.cpp ../../Core-src/LatencyCore.cpp
./build/latency_test
