#!/bin/bash
# Проверка ядра управления джойстиком на Linux.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/controller_test controller_test.cpp ../../Core-src/ControllerCore.cpp
./build/controller_test
