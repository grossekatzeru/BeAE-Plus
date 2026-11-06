#!/bin/bash
# Проверка плагина "Ламповое эхо" на Linux: собирается отдельный .so и загружается через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/spring_reverb.so ../../plugins/spring_reverb/SpringReverb.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/spring_test spring_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/spring_test
