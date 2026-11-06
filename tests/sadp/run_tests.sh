#!/bin/bash
# Проверка плагина САДП на Linux: собирается отдельный .so и загружается через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/sadp.so ../../plugins/sadp/Sadp.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/sadp_test sadp_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/sadp_test
