# Сборка всех плагинов: каждый плагин лежит в своей подпапке и имеет свой Makefile.
#   make              собрать все
#   make install      установить все (DEST=<папка>, по умолчанию ~/config/non-packaged/lib/ladspa)
#   make clean        очистить
# Один плагин: cd <подпапка> && make [install]
SUBDIRS := $(patsubst %/Makefile,%,$(wildcard */Makefile))

all install clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d $@ || exit 1; done

.PHONY: all install clean
