SUBDIRS=src
RESOURCES_PATH=share/ui/*

# PREFIX is environment variable, but if it is not set, then set default value
ifeq ($(PREFIX),)
    PREFIX := /usr
endif

default: all

all:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE)); done

.PHONY: clean

clean:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE) clean); done

install:
	install -s -D mediasynclite $(DESTDIR)$(PREFIX)/bin/mediasynclite
	@for file in $(RESOURCES_PATH); do (echo "Install: " $$file "to:" $(DESTDIR)$(PREFIX)/share/mediasynclite/`basename $$file`; if [ -f $$file ]; then install -m 644 -D $$file $(DESTDIR)$(PREFIX)/share/mediasynclite/`basename $$file`; fi); done
