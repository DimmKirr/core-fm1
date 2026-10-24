{
  description = "core-fm1 firmware build environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        isAarch64 = pkgs.stdenv.hostPlatform.isAarch64;
        x86Glibc = pkgs.pkgsCross.gnu64.glibc;
        x86GccLib = pkgs.pkgsCross.gnu64.stdenv.cc.cc.lib;
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.qemu
            pkgs.libusb1
          ] ++ pkgs.lib.optionals isAarch64 [
            x86Glibc
            x86GccLib
          ];

          shellHook = pkgs.lib.optionalString isAarch64 ''
            export QEMU_LD_PREFIX="${x86Glibc}"
            export QEMU_SET_ENV="LD_LIBRARY_PATH=${x86Glibc}/lib:${x86GccLib}/lib"
          '';
        };
      });
}
