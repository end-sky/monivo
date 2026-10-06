{ pkgs ? import <nixpkgs> { }, configFile ? ./config.lua, filterLists ? [ ] }:
pkgs.callPackage ./package.nix { inherit configFile filterLists; }
