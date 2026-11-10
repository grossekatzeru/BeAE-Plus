#!/bin/bash
# Проверка плагина "Гейт / экспандер" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/gate.so ../../plugins/gate/Gate.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/gate_test gate_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/gate_test
