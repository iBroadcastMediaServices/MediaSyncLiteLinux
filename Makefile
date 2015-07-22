SUBDIRS=src

default: all

all:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE)); done

.PHONY: clean

clean:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE) clean); done

install:
	install -s -D mediasynclite /usr/local/bin
	install -m 644 -D share/ui/ui.glade /usr/local/share/mediasynclite/ui.glade
	install -m 644 -D share/ui/__head_.png /usr/local/share/mediasynclite/__head_.png
	install -m 644 -D share/ui/icon.png /usr/local/share/mediasynclite/icon.png
	install -m 644 -D share/ui/splash.jpg /usr/local/share/mediasynclite/splash.jpg
