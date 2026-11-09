#!/bin/bash
# Проверка плагина "Вокальная цепочка 80-х (слэп + пластина)" на Linux: отдельный .so + хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/vocal_chain.so ../../plugins/vocal_chain/VocalChain.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/vocal_test vocal_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/vocal_test
