# Monivo

A tiny privacy-first browser: GTK3 + WebKitGTK 4.1 (libsoup 3), one C file (~350 lines).

- Ephemeral session: no history, cookies or cache on disk; third-party cookies blocked
- Search-engine picker next to the address bar; add your own in `config.lua`
- Audio/video (GStreamer) and file downloads (saved to your Downloads folder, never overwritten)
- Shortcuts: `Ctrl+L` address bar, `Ctrl+H` home, `Alt+←/→` back/forward, `Ctrl+R`/`F5` reload, `Ctrl +/-/0` zoom, `Ctrl+Q` quit

## Build

Needs `gtk3`, `webkit2gtk-4.1`, `pkg-config`, a C compiler, and `lua` (build-time only, to read `config.lua`).

    make && ./monivo              # or: make install PREFIX=~/.local

### Nix / NixOS

    nix build && ./result/bin/monivo      # flake
    nix-build && ./result/bin/monivo      # classic

In your NixOS config, with your own engine list kept next to it:

    environment.systemPackages = [
      (pkgs.callPackage /path/to/monivo/package.nix { configFile = ./monivo.lua; })
    ];

The package pulls in `glib-networking` (HTTPS) and the GStreamer plugin sets (audio/video)
and wraps the binary so they are found at runtime.

## Search engines

Edit `config.lua`, then **rebuild** (`make`, `nix build`, or `nixos-rebuild`). The file is read at
build time and baked into the binary. Each engine is `{ name = "...", url = "https://.../?q=%s" }`;
`%s` is replaced by the query. A bad config fails the build with a message.

## Troubleshooting

- Blank/white window on NVIDIA or some Wayland setups: `WEBKIT_DISABLE_DMABUF_RENDERER=1 monivo`
- Pages fail with a sandbox error inside containers: `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 monivo` (last resort)
- Site-specific video codecs (e.g. DRM content) are not supported.
