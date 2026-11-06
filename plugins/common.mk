# Общие правила для плагинов LADSPA. Подключается из Makefile плагина:
#
#   NAME   = spring_reverb          # имя файла: spring_reverb.so
#   SRCS   = SpringReverb.cpp       # исходники плагина
#   GROUP  = Reverb                 # подпапка при установке (подменю в Fx)
#   include ../common.mk
#
# make            собрать плагин
# make install    скопировать в $(DEST)/$(GROUP)/ (по умолчанию ~/config/non-packaged/lib/ladspa)
# make clean
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall
CXXFLAGS += -fPIC
LDFLAGS  += -shared
DEST     ?= $(HOME)/config/non-packaged/lib/ladspa

all: $(NAME).so

$(NAME).so: $(SRCS) $(wildcard *.h) $(wildcard ../common/*.h)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRCS) -lm

install: all
	mkdir -p "$(DEST)/$(GROUP)"
	cp $(NAME).so "$(DEST)/$(GROUP)/"

clean:
	rm -f $(NAME).so

.PHONY: all install clean
