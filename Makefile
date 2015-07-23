SUBDIRS=src
INSTALL_PATH=/usr/local/share/mediasynclite
RESOURCES_PATH=share/ui/*

default: all

all:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE)); done

.PHONY: clean

clean:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE) clean); done

install:
	install -s -D mediasynclite /usr/local/bin
	@for file in $(RESOURCES_PATH); do (echo "Install: " $$file "to:" $(INSTALL_PATH)/`basename $$file`; if [ -f $$file ]; then install -m 644 -D $$file $(INSTALL_PATH)/`basename $$file`; fi); done
