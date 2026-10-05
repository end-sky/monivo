{
  lib,
  stdenv,
  pkg-config,
  lua5_4,
  wrapGAppsHook3,
  gtk3,
  libsoup_3,
  webkitgtk_4_1,
  glib-networking,       # HTTPS: without it WebKit has "TLS/SSL support not available"
  gsettings-desktop-schemas,
  gst_all_1,             # audio/video codecs
  # Point this at your own config.lua to change search engines without
  # touching the source tree:  monivo.override { configFile = ./my-config.lua; }
  configFile ? ./config.lua,
}:

stdenv.mkDerivation {
  pname = "monivo";
  version = "0.2.0";

  src = lib.cleanSource ./.;

  # lua is only a build-time tool (reads config.lua); it is not linked into the browser.
  nativeBuildInputs = [ pkg-config lua5_4 wrapGAppsHook3 ];

  buildInputs = [
    gtk3
    libsoup_3
    webkitgtk_4_1
    glib-networking
    gsettings-desktop-schemas
  ] ++ (with gst_all_1; [
    gstreamer
    gst-plugins-base
    gst-plugins-good
    gst-plugins-bad
    gst-plugins-ugly
    gst-libav
  ]);

  makeFlags = [
    "PREFIX=${placeholder "out"}"
    "CONFIG=${configFile}"
  ];

  # wrapGAppsHook3 wraps bin/monivo with GIO_EXTRA_MODULES (TLS),
  # GST_PLUGIN_SYSTEM_PATH_1_0 (audio) and XDG_DATA_DIRS (schemas).

  meta = {
    description = "Minimal privacy-first web browser (GTK3 + WebKitGTK)";
    homepage = "https://github.com/sakihanii/monivo-browser";
    platforms = lib.platforms.linux;
    mainProgram = "monivo";
  };
}
