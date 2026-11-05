#!/bin/bash
# Проверка плагина "Шумодав" на Linux: собирается отдельный .so и загружается через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/noise_reducer.so ../../plugins/NoiseReducer.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/noise_test noise_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/noise_test
