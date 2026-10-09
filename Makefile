CC         ?= cc
PKG_CONFIG ?= pkg-config
# Lua >= 5.3 is needed at build time only
LUA        ?= lua
CONFIG     ?= config.lua
# extra adblock list files, added to those in config.lua
LISTS      ?=
PREFIX     ?= /usr/local
DATADIR    ?= $(PREFIX)/share/monivo
PKGS        = gtk+-3.0 webkit2gtk-4.1

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter -I. \
           -DDATADIR='"$(DATADIR)"' $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDLIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

all: monivo

# one generator run produces both the header and build/adblock.json
config_gen.h: $(CONFIG) tools/gen.lua assets/index.html assets/canvas.js $(wildcard filters/*.txt) $(LISTS)
	@mkdir -p build
	$(LUA) tools/gen.lua $(CONFIG) assets/index.html assets/canvas.js build/adblock.json $(LISTS) > $@.tmp || { rm -f $@.tmp; exit 1; }
	mv $@.tmp $@

monivo: src/main.c src/tabs.c src/keybinds.c src/browser.h config_gen.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/main.c src/tabs.c src/keybinds.c $(LDLIBS)

run: monivo
	MONIVO_DATA=build ./monivo

install: monivo
	install -Dm755 monivo $(DESTDIR)$(PREFIX)/bin/monivo
	install -Dm644 build/adblock.json $(DESTDIR)$(DATADIR)/adblock.json
	install -Dm644 monivo.desktop $(DESTDIR)$(PREFIX)/share/applications/monivo.desktop

clean:
	rm -rf monivo config_gen.h config_gen.h.tmp build

.PHONY: all run install clean
