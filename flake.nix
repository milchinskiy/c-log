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
        meta = nixpkgs.lib.importTOML ./project.toml;
        pkgs = nixpkgs.legacyPackages.${system};
        nativeBuildInputs = with pkgs; [
          llvmPackages_latest.clang-tools
          llvmPackages_latest.clang
          cmake
          cmake-format
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
            cmake-language-server
          ];

          CFLAGS = "-O2 -g";
          CXXFLAGS = "-O2 -g";

          shellHook = ''
            export CC=clang
            export CXX=clang++
            echo "Project: ${meta.project.name} ${meta.project.version}"
            clang --version
          '';
        };

        package = pkgs.stdenv.mkDerivation {
          inherit nativeBuildInputs buildInputs;
          pname = meta.project.name;
          version = meta.project.version;
          src = self;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_C_COMPILER=clang"
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
