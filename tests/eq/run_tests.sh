#!/bin/bash
# Проверка эквалайзеров на Linux: собираются отдельные .so и загружаются через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/graphic_eq.so ../../plugins/graphic_eq/GraphicEq.cpp -lm
g++ -O2 -fPIC -shared -Wall -Wextra -o build/parametric_eq.so ../../plugins/parametric_eq/ParametricEq.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/eq_test eq_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/eq_test
