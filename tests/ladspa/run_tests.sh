#!/bin/bash
# Проверка ядра LADSPA-хоста на Linux (на Haiku не нужна).
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -shared -fPIC -O2 -o build/test_plugin.so test_plugin.c -lm
g++ -std=c++17 -Wall -Wextra -I ../../Core-src -o build/host_test host_test.cpp ../../Core-src/LadspaHost.cpp -ldl
(cd build && ./host_test .)
