#!/bin/bash
# Проверка плагина сайдчейна BeFX на Linux: огибающая ключа, кэш, расчёт приседания, встроенный плагин, микшер.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -I ../../Core-src -o build/sidechain_test sidechain_test.cpp \
	../../Core-src/KeyCache.cpp ../../Core-src/BeFxSidechain.cpp ../../Core-src/Mixer.cpp ../../Core-src/Metronome.cpp \
	../../Core-src/FxChain.cpp ../../Core-src/Vst2Host.cpp ../../Core-src/LadspaHost.cpp -ldl -pthread
./build/sidechain_test
