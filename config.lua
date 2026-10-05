-- Monivo configuration.
-- This file is evaluated at BUILD time (by tools/gen.lua) and baked into the
-- binary, so after editing it you have to recompile:
--     make              (plain)
--     nix build         (flake)   /   rebuild your NixOS config
--
-- Each engine needs a `name` and a `url`. The url must contain exactly one
-- `%s`, which is replaced by the percent-encoded search query.

return {
  -- Engine selected at startup (must match one of the names below).
  default = "DuckDuckGo",

  engines = {
    { name = "DuckDuckGo", url = "https://duckduckgo.com/?q=%s" },
    { name = "4get",       url = "https://4get.ca/web?s=%s" },
    { name = "Mojeek",     url = "https://www.mojeek.com/search?q=%s" },
    { name = "Brave",      url = "https://search.brave.com/search?q=%s" },
    { name = "Startpage",  url = "https://www.startpage.com/do/search?q=%s" },
    { name = "SearXNG",    url = "https://searx.be/search?q=%s" },
    -- Add your own, e.g. a self-hosted instance:
    -- { name = "My 4get", url = "https://4get.example.org/web?s=%s" },
  },

  dark = false,            -- start in dark mode
  width = 1000,            -- initial window size
  height = 720,
  -- download_dir = "/home/you/Downloads",   -- default: XDG download dir, else ~/Downloads
}
