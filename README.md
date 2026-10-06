# Monivo

## The browser that just gets the job done. Originally by @sakihanii, rewritten in C.

# How is this different than the original?

- Removed the unnecessary shortcuts.
- Rewritten in C.
- Added new features.
- Binary file is now 20kb instead of 800kb.
- Super lightweight, adblocker and NoScript-style extensions built-in.
- UTC and language spoofing, Tor-style.

A tiny privacy-first browser: GTK3 + WebKitGTK 4.1 (libsoup 3), one C file (~500 lines).

- Ephemeral session: no history, cookies or cache on disk; third-party cookies blocked
- Fingerprint hardening: time zone forced to UTC, language to `en-US`/`en`, canvas readback blocked (Tor-style)
- Built-in **adblock** (EasyList-syntax lists, converted at build time) and **NoScript-style modes**
- Search-engine picker next to the address bar; add your own in `config.lua`
- Audio/video (GStreamer) and file downloads (to your Downloads folder, never overwritten)
- Shortcuts: `Ctrl+L` address bar, `Ctrl+H` home, `Alt+←/→` back/forward, `Ctrl+R`/`F5` reload,
  `Ctrl +/-/0` zoom, `Ctrl+Shift+J` NoScript mode, `Ctrl+Q` quit

## Build

Needs `gtk3`, `webkit2gtk-4.1`, `pkg-config`, a C compiler, and `lua` >= 5.3 (build-time only).

    make run                       # build and start from this directory
    make install PREFIX=~/.local   # installs bin/, share/applications/ (launcher entry), share/monivo/

### Nix / NixOS

    nix build && ./result/bin/monivo      # flake
    nix-build && ./result/bin/monivo      # classic

Install it so launchers see it (dmenu lists binaries from `$PATH`; rofi/GNOME/etc. use the `.desktop` file):

    environment.systemPackages = [
      (pkgs.callPackage /path/to/monivo/package.nix {
        configFile = ./monivo.lua;                 # your config.lua
        # filterLists = [ (pkgs.fetchurl { url = "https://easylist.to/easylist/easylist.txt"; hash = "sha256-..."; }) ];
      })
    ];

Optional default browser: `xdg-settings set default-web-browser monivo.desktop`.

## config.lua (read at build time: edit, then rebuild)

- `engines` / `default`: search engines; `%s` in the url is the query.
- `adblock = true|false`, `adblock_lists = { ... }`: lists in ABP/EasyList syntax. `filters/default.txt`
  is a small starter list; `tools/update-lists.sh` downloads EasyList, EasyPrivacy and uBlock filters.
- `noscript = true|false`, `noscript_mode = 1|2` (start mode; `Ctrl+Shift+J` or the **NS** button flips it live).
  - Mode 1: scripts, frames, wasm, media, fetch/XHR allowed; fonts, WebGL, unprompted popups blocked.
  - Mode 2: static only: HTML, CSS and images. Everything above blocked.
  - `noscript_modes = { [1] = { font = true } }` overrides single capabilities
    (`script frame media fetch font image popup webgl`).

- `timezone = "UTC"` (or any zone name, or `false`): sets `TZ` for the browser, so `Date`/`Intl` report it.
- `languages = { "en-US", "en" }` (or `false`): `Accept-Language`, `navigator.language(s)`, `Intl` default locale.
  Plain `{ "en" }` works but is rarer than `en-US`, so it stands out more.
- `canvas = "block" | "noise" | false`: `block` returns blank data from `toDataURL`/`toBlob`/`getImageData`/WebGL
  `readPixels` (like Tor, minus its permission prompt); `noise` flips low bits differently per site and per session.

Compiled adblock rules are cached in `~/.cache/monivo/filters` (no browsing data).

## Limits (compared to the real extensions)

WebKitGTK has no WebExtension support, so uBlock Origin and NoScript themselves can't run here. This is a
native equivalent built on WebKit's content-blocker engine: network blocking and element hiding from
EasyList-style lists work; uBO-only features (scriptlets, `$redirect`, `$removeparam`, `$csp`, procedural
cosmetic filters, dynamic filtering matrix, per-site allow lists) are skipped. NoScript modes are global
(no per-site trust list), and WebSocket/beacon traffic can't be filtered separately.

Canvas protection is a script injected into every frame, not a browser-engine patch: determined pages can
sidestep it (e.g. canvas work inside Web Workers). Fonts, screen size, WebGL strings, audio and the
User-Agent are not spoofed (WebGL is off in NoScript mode 1).

## Troubleshooting

- Blank/white window on NVIDIA or some Wayland setups: `WEBKIT_DISABLE_DMABUF_RENDERER=1 monivo`
- Adblock "rules failed" in the toolbar: the full error is printed to the terminal; remove the last list you added.
- Sandbox error in containers: `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 monivo` (last resort)
