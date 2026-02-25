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
    in
    {
      devShells.${system}.default =
        (pkgs.buildFHSEnv {
          name = "cicest-lang-dev";
          targetPkgs =
            pkgs:
            (with pkgs; [
              llvmPackages_latest.llvm
              llvmPackages_latest.llvm.dev
              llvmPackages_latest.lld
              llvmPackages_latest.bintools
              llvmPackages_latest.libcxx
              llvmPackages_latest.compiler-rt
              llvmPackages_latest.libunwind

              cmake
              gnumake
              ninja
              clang

              fish
            ]);

          runScript = "fish";
        }).env;
    };
}
