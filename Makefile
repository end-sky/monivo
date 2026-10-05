CC         ?= cc
PKG_CONFIG ?= pkg-config
LUA        ?= lua
CONFIG     ?= config.lua
PREFIX     ?= /usr/local
PKGS        = gtk+-3.0 webkit2gtk-4.1

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter -I. $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDLIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

all: monivo

config_gen.h: $(CONFIG) tools/gen.lua assets/index.html
	$(LUA) tools/gen.lua $(CONFIG) assets/index.html > $@.tmp
	mv $@.tmp $@

monivo: src/monivo.c config_gen.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/monivo.c $(LDLIBS)

install: monivo
	install -Dm755 monivo $(DESTDIR)$(PREFIX)/bin/monivo
	install -Dm644 monivo.desktop $(DESTDIR)$(PREFIX)/share/applications/monivo.desktop

clean:
	rm -f monivo config_gen.h config_gen.h.tmp

.PHONY: all install clean
