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
        zlib
        zlib.dev
        libxml2
        libxml2.dev
        libffi
        libffi.dev
      ];
      parser-dependencies = with pkgs; [
        argparse
      ];
      make-dependencies = with pkgs; [
        cmake
        gnumake
        ninja
        llvmPackages_latest.clang
        llvmPackages_latest.clang-tools
        pkg-config
      ];
      misc-dependencies = with pkgs; [
        fish

        git
        jq
        curl
        unzip
        file
        gnupg
      ];
      dependencies = llvm-dependencies ++ parser-dependencies ++ make-dependencies ++ misc-dependencies;
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
                cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$@"
                ninja -C build
                ln -sf build/compile_commands.json compile_commands.json
              '';
            }
          }/bin/build";
        };
        build-tests = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "build-tests";
              runtimeInputs = make-dependencies ++ llvm-dependencies ++ parser-dependencies;
              text = ''
                cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCICEST_BUILD_TESTS=ON "$@"
                ninja -C build
                ln -sf build/compile_commands.json compile_commands.json
              '';
            }
          }/bin/build-tests";
        };
        tests = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "tests";
              runtimeInputs = make-dependencies ++ llvm-dependencies ++ parser-dependencies;
              text = ''
                cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCICEST_BUILD_TESTS=ON
                ninja -C build
                ln -sf build/compile_commands.json compile_commands.json
                ctest --test-dir build --output-on-failure
              '';
            }
          }/bin/tests";
        };
        lint = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "lint";
              runtimeInputs = make-dependencies ++ llvm-dependencies ++ parser-dependencies ++ misc-dependencies;
              text = ''
                export CC=''${CC:-clang}
                export CXX=''${CXX:-clang++}
                bash .github/scripts/run-lint-format.sh "$@"
              '';
            }
          }/bin/lint";
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
