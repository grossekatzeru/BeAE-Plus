#!/bin/bash
# Проверка плагина "Пластинчатый реверб" на Linux: собирается отдельный .so и загружается через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/plate_reverb.so ../../plugins/plate_reverb/PlateReverb.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/plate_test plate_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/plate_test
