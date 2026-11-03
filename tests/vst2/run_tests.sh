#!/bin/bash
# Проверка VST2-хоста на Linux с тестовым плагином.
set -e
cd "$(dirname "$0")"
mkdir -p build/plugins
gcc -shared -fPIC -O2 -o build/plugins/test_vst.so test_vst.c
g++ -std=c++17 -Wall -I ../../Core-src -o build/vst_test vst_test.cpp ../../Core-src/Vst2Host.cpp ../../Core-src/FxChain.cpp ../../Core-src/Mixer.cpp ../../Core-src/LadspaHost.cpp -ldl
./build/vst_test build/plugins
