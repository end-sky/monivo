-- Build-time generator (Lua >= 5.3).
--   lua tools/gen.lua config.lua assets/index.html assets/canvas.js build/adblock.json [extra-list ...] > config_gen.h
-- * config.lua + homepage  -> C header (stdout)
-- * ABP/EasyList-style filter lists -> WebKit content-rule-list JSON (arg[3])

local cfg_path, html_path, canvas_path, json_path = arg[1], arg[2], arg[3], arg[4]
local extra_lists = {}
for i = 5, #arg do extra_lists[#extra_lists + 1] = arg[i] end

local function fail(msg)
  io.stderr:write("config error: ", tostring(msg), "\n")
  os.exit(1)
end

local chunk, err = loadfile(cfg_path)
if not chunk then fail(err) end
local ok, cfg = pcall(chunk)
if not ok then fail(cfg) end
if type(cfg) ~= "table" then fail(cfg_path .. " must `return { ... }`") end

-- ---------- helpers ----------

local function esc(s) -- C string literal body
  return (s:gsub('[%c"\\]', function(c)
    if c == "\n" then return '\\n"\n"' end
    return string.format("\\%03o", c:byte())
  end))
end
local function lit(s) return '"' .. esc(s) .. '"' end

local function jstr(s) -- JSON string
  return '"' .. s:gsub('[%c"\\]', function(c)
    if c == '"' then return '\\"' elseif c == "\\" then return "\\\\" end
    return string.format("\\u%04x", c:byte())
  end) .. '"'
end
local function jarr(t)
  local o = {}
  for i, v in ipairs(t) do o[i] = jstr(v) end
  return "[" .. table.concat(o, ",") .. "]"
end

local function int(v, default, name)
  v = v == nil and default or v
  if type(v) ~= "number" or v < 1 then fail("'" .. name .. "' must be a positive number") end
  return math.floor(v)
end
local function bool(v, default, name)
  if v == nil then return default end
  if type(v) ~= "boolean" then fail("'" .. name .. "' must be true or false") end
  return v
end

-- ---------- ABP filter list -> WebKit content rules ----------

local BLOCK = '{"type":"block"}'
local ALLOW = '{"type":"ignore-previous-rules"}'

local TYPEMAP = {
  script = "script", image = "image", stylesheet = "style-sheet", css = "style-sheet",
  font = "font", media = "media", xmlhttprequest = "raw", xhr = "raw",
}
local DEFAULT_TYPES = { "image", "style-sheet", "script", "font", "raw", "svg-document", "media" }

-- Only constructs WebKit's url-filter regex subset accepts: literals, \x escapes,
-- ".*", "[...]" classes, "(...)?" groups, ^ and $.
local function to_regex(s)
  return (s:gsub(".", function(c)
    if c == "*" then return ".*"
    elseif c == "^" then return "[/:?&=;,]"        -- ABP separator, approximated
    elseif c:find("[%w_%-/:%%~!,;=&@#' ]") then return c
    else return "\\" .. c end
  end))
end

local function domains(spec, sep)
  local pos, neg = {}, {}
  for d in spec:gmatch("[^" .. sep .. "]+") do
    local n = d:sub(1, 1) == "~"
    d = (n and d:sub(2) or d):lower()
    if not d:match("^[a-z0-9][a-z0-9%.%-]*$") then return nil end
    local list = n and neg or pos
    list[#list + 1] = "*" .. d
  end
  if #pos > 0 and #neg > 0 then return nil end   -- can't mix if-/unless-domain
  return pos, neg
end

local function make_rule(rx, o, types, action)
  local t = { '"url-filter":' .. jstr(rx) }
  if o.case then t[#t + 1] = '"url-filter-is-case-sensitive":true' end
  if types then t[#t + 1] = '"resource-type":' .. jarr(types) end
  if o.child then t[#t + 1] = '"load-context":["child-frame"]' end
  if o.party then t[#t + 1] = '"load-type":["' .. o.party .. '"]' end
  if o.pos and #o.pos > 0 then t[#t + 1] = '"if-domain":' .. jarr(o.pos) end
  if o.neg and #o.neg > 0 then t[#t + 1] = '"unless-domain":' .. jarr(o.neg) end
  return '{"trigger":{' .. table.concat(t, ",") .. '},"action":' .. action .. "}"
end

local function net_rule(line)
  local allow = line:sub(1, 2) == "@@"
  if allow then line = line:sub(3) end
  local pat, opts = line:match("^(.*)%$([%w~_%-=,|%.%*:]+)$")
  if not pat then pat, opts = line, "" end
  if pat == "" or pat:find("[\128-\255]") then return end
  if #pat > 2 and pat:sub(1, 1) == "/" and pat:sub(-1) == "/" then return end -- regex rule

  local o, types, frame = {}, {}, false
  for opt in opts:gmatch("[^,]+") do
    local neg = opt:sub(1, 1) == "~"
    local name, val = (neg and opt:sub(2) or opt):match("^([^=]+)=?(.*)$")
    if name == "third-party" or name == "3p" then o.party = neg and "first-party" or "third-party"
    elseif name == "first-party" or name == "1p" then o.party = neg and "third-party" or "first-party"
    elseif name == "domain" then
      o.pos, o.neg = domains(val, "|")
      if not o.pos then return end
    elseif name == "match-case" then o.case = true
    elseif name == "important" then -- no-op
    elseif (name == "subdocument" or name == "frame") and not neg then frame = true
    elseif TYPEMAP[name] and not neg then types[#types + 1] = TYPEMAP[name]
    else return end                       -- unsupported option: drop the rule
  end

  local prefix, body, host = "", pat, false
  if pat:sub(1, 2) == "||" then prefix, body, host = "^[^:]+://+([^/]+\\.)?", pat:sub(3), true
  elseif pat:sub(1, 1) == "|" then prefix, body = "^", pat:sub(2) end
  local tail = ""
  if body:sub(-1) == "|" then tail, body = "$", body:sub(1, -2) end
  if #body < (host and 3 or 4) then return end
  local rx = prefix .. to_regex(body) .. tail

  if frame then
    if #types > 0 then return end
    types, o.child = { "document" }, true
  elseif #types == 0 then
    for i, v in ipairs(DEFAULT_TYPES) do types[i] = v end
    -- plain host rules also cover ad iframes (document loads)
    if host and body:match("^[%w%.%-%*]+%^?$") then types[#types + 1] = "document" end
  end
  return allow and "allow" or "block", make_rule(rx, o, types, allow and ALLOW or BLOCK)
end

local PSEUDO_OK = { ["not"] = 1, ["first-child"] = 1, ["last-child"] = 1, ["nth-child"] = 1,
  ["nth-of-type"] = 1, ["first-of-type"] = 1, ["last-of-type"] = 1, ["only-child"] = 1, ["empty"] = 1 }

local function selector_ok(sel)
  if #sel > 1000 or sel:sub(1, 1) == "+" then return false end   -- "+js(" = scriptlet
  if not sel:match("^[%w%s_%-#%.>%+~,%*%[%]=%^%$|\"'%(%):/%?%%&@]+$") then return false end
  if sel:find("::", 1, true) then return false end
  for p in sel:gmatch(":([%a%-]+)") do
    if not PSEUDO_OK[p] then return false end
  end
  return true
end

local function css_rule(line)
  if line:find("#[@%?%$%%]+#") then return end    -- exceptions / procedural / scriptlets
  local dom, sel = line:match("^([^#]*)##(.+)$")
  if not sel or not selector_ok(sel) then return end
  local o = {}
  if dom ~= "" then
    o.pos, o.neg = domains(dom, ",")
    if not o.pos then return end
  end
  return sel, o
end

local function convert_lists(paths)
  local blocks, csss, allows, seen = {}, {}, {}, {}
  local generic = {}                   -- generic cosmetic selectors, batched
  local total, skipped = 0, 0
  for _, path in ipairs(paths) do
    local f = io.open(path, "rb")
    if not f then fail("adblock list not found: " .. path) end
    for line in f:lines() do
      line = line:gsub("\r$", ""):gsub("^%s+", ""):gsub("%s+$", "")
      local c = line:sub(1, 1)
      if line ~= "" and c ~= "!" and c ~= "[" then
        total = total + 1
        local kind, json, handled = nil, nil, false
        if line:find("##", 1, true) or line:find("#[@%?%$%%]+#") then
          local sel, o = css_rule(line)
          if sel and not o.pos and not o.neg then
            if not seen["g" .. sel] then
              seen["g" .. sel] = true
              generic[#generic + 1] = sel
            end
            handled = true
          elseif sel then
            kind, json = "css", make_rule(".*", o, nil,
              '{"type":"css-display-none","selector":' .. jstr(sel) .. "}")
          end
        else
          kind, json = net_rule(line)
        end
        if json then
          handled = true
          if not seen[json] then
            seen[json] = true
            local dst = kind == "block" and blocks or kind == "css" and csss or allows
            dst[#dst + 1] = json
          end
        end
        if not handled then skipped = skipped + 1 end
      end
    end
    f:close()
  end
  for i = 1, #generic, 100 do
    local sels = table.concat(generic, ", ", i, math.min(i + 99, #generic))
    csss[#csss + 1] = make_rule(".*", {}, nil,
      '{"type":"css-display-none","selector":' .. jstr(sels) .. "}")
  end
  local all = {}
  for _, t in ipairs({ blocks, csss, allows }) do
    for _, r in ipairs(t) do all[#all + 1] = r end
  end
  return all, total, skipped
end

-- ---------- config ----------

local engines = cfg.engines
if type(engines) ~= "table" or #engines == 0 then fail("'engines' must be a non-empty list") end

local adblock = bool(cfg.adblock, true, "adblock")
local noscript = bool(cfg.noscript, true, "noscript")
local ns_mode = cfg.noscript_mode == nil and 1 or cfg.noscript_mode
if ns_mode ~= 1 and ns_mode ~= 2 then fail("'noscript_mode' must be 1 or 2") end

-- capabilities: true = allowed.  wasm always follows script (no separate switch in WebKit).
local NS = {
  { script = true,  frame = true,  media = true,  fetch = true,  font = false, image = true, popup = false, webgl = false },
  { script = false, frame = false, media = false, fetch = false, font = false, image = true, popup = false, webgl = false },
}
for m, over in pairs(cfg.noscript_modes or {}) do
  if not NS[m] or type(over) ~= "table" then fail("noscript_modes: only [1] and [2] exist") end
  for k, v in pairs(over) do
    if NS[m][k] == nil then fail(("noscript_modes[%d]: unknown capability '%s'"):format(m, k)) end
    NS[m][k] = bool(v, NS[m][k], "noscript_modes[" .. m .. "]." .. k)
  end
end

local function ns_rules(c)
  local t, r = {}, {}
  if not c.script then t[#t + 1] = "script" end
  if not c.media then t[#t + 1] = "media" end
  if not c.fetch then t[#t + 1] = "raw" end
  if not c.font then t[#t + 1] = "font" end
  if not c.image then t[#t + 1] = "image"; t[#t + 1] = "svg-document" end
  if #t > 0 then
    r[#r + 1] = '{"trigger":{"url-filter":".*","resource-type":' .. jarr(t) .. '},"action":' .. BLOCK .. "}"
  end
  if not c.frame then
    r[#r + 1] = '{"trigger":{"url-filter":".*","resource-type":["document"],"load-context":["child-frame"]},"action":'
      .. BLOCK .. "}"
  end
  return #r > 0 and "[" .. table.concat(r, ",") .. "]" or nil
end

-- ---------- fingerprinting ----------

local timezone = cfg.timezone
if timezone == nil then timezone = "UTC" end
if timezone ~= false and (type(timezone) ~= "string" or not timezone:match("^[%w_/%+%-:]+$")) then
  fail("'timezone' must be a zone name like \"UTC\" or \"Europe/Berlin\", or false")
end

local languages = cfg.languages
if languages == nil then languages = { "en-US", "en" } end
if languages == false then languages = {} end
if type(languages) ~= "table" then fail("'languages' must be a list like { \"en-US\", \"en\" }, or false") end
for _, l in ipairs(languages) do
  if type(l) ~= "string" or not l:match("^%a[%w%-]*$") then fail("bad language tag: " .. tostring(l)) end
end

local canvas = cfg.canvas
if canvas == nil then canvas = "block" end
local CANVAS_MODES = { [false] = 0, block = 1, noise = 2 }
if CANVAS_MODES[canvas] == nil then fail("'canvas' must be \"block\", \"noise\" or false") end

-- ---------- adblock ----------

local ab_id = "ab-0"
local json = "[]"
if adblock then
  local lists = {}
  for _, p in ipairs(cfg.adblock_lists or { "filters/default.txt" }) do lists[#lists + 1] = p end
  for _, p in ipairs(extra_lists) do lists[#lists + 1] = p end
  local rules, total, skipped = convert_lists(lists)
  if #rules == 0 then
    io.stderr:write("warning: no usable adblock rules found, adblock disabled\n")
    adblock = false
  elseif #rules > 150000 then
    fail(("%d rules is over WebKit's limit of 150000; use fewer/smaller lists"):format(#rules))
  else
    json = "[" .. table.concat(rules, ",\n") .. "]"
    local h = 5381
    for i = 1, #json do h = (h * 33 + json:byte(i)) & 0xffffffff end
    ab_id = string.format("ab-%08x", h)
    io.stderr:write(("adblock: %d rules from %d filters in %d list(s) (%d unsupported, skipped)\n")
      :format(#rules, total, #lists, skipped))
  end
end
local jf = assert(io.open(json_path, "wb"))
jf:write(json)
jf:close()

-- ---------- C header ----------

local out = {}
local function w(...) out[#out + 1] = string.format(...) end

w("/* generated by tools/gen.lua - do not edit */\n")
w("typedef struct { const char *name, *prefix, *suffix; } Engine;\n")
w("static const Engine engines[] = {\n")
local default_idx
for i, e in ipairs(engines) do
  if type(e) ~= "table" or type(e.name) ~= "string" or type(e.url) ~= "string" then
    fail(("engine #%d needs string fields 'name' and 'url'"):format(i))
  end
  local pre, post = e.url:match("^(.-)%%s(.*)$")
  if not pre or post:find("%%s") then
    fail(("engine '%s': url must contain exactly one %%s"):format(e.name))
  end
  if e.name == cfg.default then default_idx = i - 1 end
  w("\t{ %s, %s, %s },\n", lit(e.name), lit(pre), lit(post))
end
w("};\n")
if cfg.default ~= nil and not default_idx then
  fail(("default engine '%s' is not in the engines list"):format(tostring(cfg.default)))
end

w("#define N_ENGINES %d\n", #engines)
w("#define DEFAULT_ENGINE %d\n", default_idx or 0)
w("#define WIN_W %d\n", int(cfg.width, 1000, "width"))
w("#define WIN_H %d\n", int(cfg.height, 720, "height"))
w("#define START_DARK %d\n", bool(cfg.dark, false, "dark") and 1 or 0)
w("#define DOWNLOAD_DIR %s\n", type(cfg.download_dir) == "string" and lit(cfg.download_dir) or "NULL")

w("#define TIMEZONE %s\n", timezone and lit(timezone) or "NULL")
w("#define N_LANGS %d\nstatic const char *const LANGS[] = {", #languages)
for _, l in ipairs(languages) do w(" %s,", lit(l)) end
w(" NULL };\n")
w("#define CANVAS_MODE %d\n", CANVAS_MODES[canvas])

w("#define ADBLOCK %d\n#define ADBLOCK_ID \"%s\"\n", adblock and 1 or 0, ab_id)
w("#define NOSCRIPT %d\n#define NOSCRIPT_MODE %d\n", noscript and 1 or 0, ns_mode)
w("static const char *const NS_RULES[2] = {\n")
for m = 1, 2 do
  local r = ns_rules(NS[m])
  w("\t%s,\n", r and lit(r) or "NULL")
end
w("};\n")
w("static const struct { int js, webgl, media, popup; } NS_SET[2] = {\n")
for m = 1, 2 do
  local c = NS[m]
  w("\t{ %d, %d, %d, %d },\n", c.script and 1 or 0, c.webgl and 1 or 0, c.media and 1 or 0, c.popup and 1 or 0)
end
w("};\n")

local f = assert(io.open(canvas_path, "rb"))
local canvas_js = f:read("a")
f:close()
w("static const char CANVAS_JS[] =\n\"%s\";\n", esc(canvas_js))

f = assert(io.open(html_path, "rb"))
local html = f:read("a")
f:close()
w("static const char HOME_HTML[] =\n\"%s\";\n", esc(html))

io.write(table.concat(out))
