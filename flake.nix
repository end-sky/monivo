{
  description = "Monivo - minimal privacy-first browser";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAll (pkgs: rec {
        monivo = pkgs.callPackage ./package.nix { };
        default = monivo;
      });
      overlays.default = final: _prev: { monivo = final.callPackage ./package.nix { }; };
    };
}
