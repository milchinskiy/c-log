{
  description = "C Log — tiny, no‑alloc, (optionally) thread‑safe C logger";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    supportedSystems = [
      "x86_64-linux"
      "aarch64-linux"
      "x86_64-darwin"
      "aarch64-darwin"
    ];

    eachSystem = nixpkgs.lib.genAttrs supportedSystems (
      system: let
        meta = import ./project.nix;
        pkgs = nixpkgs.legacyPackages.${system};
        nativeBuildInputs = with pkgs; [
          clang-tools
          clang
          cmake
        ];
        buildInputs = with pkgs; [
          pkg-config
          glibc.dev
        ];
      in {
        devShell = pkgs.mkShell {
          inherit nativeBuildInputs buildInputs;
          packages = with pkgs; [
            gdb
          ];

          CFLAGS = "-O2 -g";
          CXXFLAGS = "-O2 -g";

          shellHook = ''
            export CC=clang
            export CXX=clang++
            echo "Project: ${meta.pname} ${meta.version}"
            clang --version
          '';
        };

        package = pkgs.stdenv.mkDerivation {
          inherit nativeBuildInputs buildInputs;
          pname = meta.pname;
          version = meta.version;
          src = self;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_C_COMPILER=clang"
            "-DPROJECT_NAME_OVERRIDE=${meta.pname}"
            "-DPROJECT_VERSION_OVERRIDE=${meta.version}"
          ];

          doCheck = true;
        };
      }
    );
  in {
    devShells =
      nixpkgs.lib.mapAttrs (system: systemAttrs: {
        default = systemAttrs.devShell;
      })
      eachSystem;

    packages =
      nixpkgs.lib.mapAttrs (system: systemAttrs: {
        default = systemAttrs.package;
      })
      eachSystem;
  };
}
