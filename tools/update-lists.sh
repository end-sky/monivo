#!/bin/sh
# Download popular filter lists into filters/ (EasyList/EasyPrivacy: GPLv3+ / CC BY-SA 3.0).
# Then enable them in config.lua's adblock_lists and rebuild.
set -e
cd "$(dirname "$0")/../filters"
fetch() { curl -fsSL "$1" -o "$2" && echo "updated filters/$2"; }
fetch https://easylist.to/easylist/easylist.txt easylist.txt
fetch https://easylist.to/easylist/easyprivacy.txt easyprivacy.txt
fetch https://ublockorigin.github.io/uAssets/filters/filters.txt ublock.txt
