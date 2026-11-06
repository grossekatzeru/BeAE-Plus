# Плагины BeAE-Plus: обычные LADSPA-плагины (.so), работают в любом LADSPA-хосте.
# Сборка:    make
# Установка: make install                (в ~/config/non-packaged/lib/ladspa, видят все приложения)
#            make install DEST=<папка>   (например, plugins рядом с BeAE: DEST=/путь/к/BeAE/plugins)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall
CXXFLAGS += -fPIC
LDFLAGS  += -shared
DEST     ?= $(HOME)/config/non-packaged/lib/ladspa

PLUGINS = spring_reverb.so

all: $(PLUGINS)

spring_reverb.so: SpringReverb.cpp SpringReverb.h ladspa_abi.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ SpringReverb.cpp -lm

install: all
	mkdir -p "$(DEST)/Reverb"
	cp spring_reverb.so "$(DEST)/Reverb/"

clean:
	rm -f $(PLUGINS)

.PHONY: all install clean
