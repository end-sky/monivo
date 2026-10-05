{ pkgs ? import <nixpkgs> { }, configFile ? ./config.lua }:
pkgs.callPackage ./package.nix { inherit configFile; }
