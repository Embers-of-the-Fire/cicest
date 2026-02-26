{
  description = "Cicest Lang dev environment";
  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
      llvm-dependencies = with pkgs; [
        llvmPackages_latest.llvm
        llvmPackages_latest.llvm.dev
        llvmPackages_latest.lld
        llvmPackages_latest.bintools
        llvmPackages_latest.libcxx
        llvmPackages_latest.compiler-rt
        llvmPackages_latest.libunwind
      ];
      parser-dependencies = with pkgs; [
        # taocpp/PEGTL
        pegtl
      ];
      make-dependencies = with pkgs; [
        cmake
        gnumake
        ninja
        clang
      ];
      misc-dependencies = with pkgs; [
        fish

        git
        curl
        unzip
        file
        gnupg
      ];
      dependencies = llvm-dependencies ++ make-dependencies ++ misc-dependencies;
    in
    {
      apps.${system} = {
        build = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "build";
              runtimeInputs = make-dependencies ++ llvm-dependencies ++ parser-dependencies;
              text = ''
                cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug "$@"
                ninja -C build
              '';
            }
          }/bin/build";
        };
      };

      devShells.${system}.default =
        (pkgs.buildFHSEnv {
          name = "cicest-lang-dev";
          targetPkgs = pkgs: dependencies;

          runScript = "fish";
        }).env;
    };
}
