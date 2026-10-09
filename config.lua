-- Monivo configuration.
-- This file is evaluated at BUILD time (by tools/gen.lua) and baked into the
-- binary, so after editing it you have to recompile:
--     make              (plain)
--     nix build         (flake)   /   rebuild your NixOS config

return {
  ---------------------------------------------------------------- search
  -- Engine selected at startup (must match one of the names below).
  -- Each engine needs a `name` and a `url` with exactly one `%s` (the query).
  default = "DuckDuckGo",

  engines = {
    { name = "DuckDuckGo", url = "https://duckduckgo.com/?q=%s" },
    { name = "4get",       url = "https://4get.ca/web?s=%s" },
    { name = "Mojeek",     url = "https://www.mojeek.com/search?q=%s" },
    { name = "Brave",      url = "https://search.brave.com/search?q=%s" },
    { name = "Startpage",  url = "https://www.startpage.com/do/search?q=%s" },
    { name = "SearXNG",    url = "https://searx.be/search?q=%s" },
    -- { name = "My 4get", url = "https://4get.example.org/web?s=%s" },
  },

  ---------------------------------------------------------------- adblock
  -- Blocks ads/trackers and hides ad elements, using ABP/EasyList-syntax lists.
  adblock = true,
  adblock_lists = {
    "filters/default.txt",        -- small built-in starter list
    -- Run tools/update-lists.sh, then uncomment for real coverage:
    -- "filters/easylist.txt",
    -- "filters/easyprivacy.txt",
    -- "filters/ublock.txt",
  },

  ---------------------------------------------------------------- noscript
  -- Mode 1 (default): scripts, frames, wasm, media and fetch/XHR allowed;
  --                   fonts, WebGL and popups blocked.
  -- Mode 2 (untrusted): no scripts/wasm/frames/media/fetch/fonts/WebGL/popups;
  --                   only static HTML, CSS and images.
  -- Switch live with Ctrl+Shift+J or the "NS" button; this picks the start mode.
  noscript = true,
  noscript_mode = 1,

  -- Optional per-mode overrides (true = allowed). wasm follows `script`.
  -- noscript_modes = {
  --   [1] = { font = true },       -- allow web fonts in mode 1
  --   [2] = { image = false },     -- strictly HTML + CSS in mode 2
  -- },

  ---------------------------------------------------------------- experimental user-agent override
  -- DISABLED by default. When enabled, startup asks for confirmation because spoofing
  -- can break sites, cause incorrect layouts, and may make fingerprinting worse.
  -- This changes only the UA string; it does not make WebKit behave like Chromium.
  experimental_user_agent = {
    enabled = true,
    value = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36",
  },

  ---------------------------------------------------------------- fingerprinting
  -- Time zone reported to websites (Date, Intl). false = use the system zone.
  timezone = "UTC",
  -- Languages sent in Accept-Language and exposed as navigator.language(s).
  -- "en-US" is far more common than plain "en", so it blends in better; use { "en" } if you prefer.
  languages = { "en-US", "en" },
  -- Canvas/WebGL readback protection (what fingerprinters hash):
  --   "block" = blank data, like Tor (default; a few canvas-based features may break)
  --   "noise" = tiny changes, different per site and per session (better site compatibility)
  --   false   = off
  canvas = "block",

  ---------------------------------------------------------------- window
  dark = false,            -- start in dark mode
  width = 1000,            -- initial window size
  height = 720,
  -- download_dir = "/home/you/Downloads",   -- default: XDG download dir, else ~/Downloads
}
