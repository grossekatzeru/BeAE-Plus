#!/bin/bash
# Проверка многополосного компрессора на Linux: собирается отдельный .so и загружается через хост BeAE.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -O2 -fPIC -shared -Wall -Wextra -o build/multiband.so ../../plugins/multiband/Multiband.cpp -lm
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/multiband_test multiband_test.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/multiband_test
