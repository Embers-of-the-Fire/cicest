{
  description = "Cicest Lang dev environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
      llvm = pkgs.llvmPackages_22;
      llvm-dependencies = with pkgs; [
        llvm.llvm
        llvm.llvm.dev
        llvm.lld
        llvm.bintools
        llvm.libcxx
        llvm.compiler-rt
        llvm.libunwind
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
        llvm.clang
        llvm.clang-tools
        patchelf
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
        prerelease-bundle = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "prerelease-bundle";
              runtimeInputs = make-dependencies ++ llvm-dependencies ++ parser-dependencies;
              text = ''
                bash .github/scripts/build-prerelease-bundle.sh "$@"
              '';
            }
          }/bin/prerelease-bundle";
        };
        # Prints the exact toolchain versions used by the lint/format pipeline;
        # useful when diagnosing toolchain crashes.
        toolchain-versions = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "toolchain-versions";
              runtimeInputs = make-dependencies;
              text = ''
                clang --version | head -n1
                clangd --version | head -n1
                clang-tidy --version | head -n1
                clang-format --version | head -n1
              '';
            }
          }/bin/toolchain-versions";
        };
        # Formats all tracked C/C++ sources in place with the flake's
        # clang-format. Pass --dry-run to check instead of rewriting.
        format = {
          type = "app";
          program = "${
            pkgs.writeShellApplication {
              name = "format";
              runtimeInputs = make-dependencies ++ [ pkgs.git ];
              text = ''
                if [[ "''${1:-}" == "--dry-run" ]]; then
                  git ls-files '*.h' '*.hh' '*.hpp' '*.c' '*.cc' '*.cpp' '*.cxx' |
                    xargs clang-format --dry-run --Werror
                else
                  git ls-files '*.h' '*.hh' '*.hpp' '*.c' '*.cc' '*.cpp' '*.cxx' |
                    xargs clang-format -i
                fi
              '';
            }
          }/bin/format";
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
