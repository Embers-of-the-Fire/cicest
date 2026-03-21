# Continuous Integration (CI)

This repository uses GitHub Actions for automated validation on pushes and pull requests.

## Workflows

### 1) `Tests` workflow

File: `.github/workflows/tests.yml`

Runs test suites on a platform matrix:

- `ubuntu-latest` (Linux)
- `macos-latest` (macOS)
- `windows-latest` (Windows via MSYS2)
- `windows-latest` (Windows via MSVC)

Behavior by platform:

- **Linux**
  - Uses Nix (`cachix/install-nix-action`)
  - Runs `.github/scripts/run-tests.sh` (which executes `nix run .#tests`)
- **macOS**
  - Installs dependencies via Homebrew (`cmake`, `ninja`, `llvm`, `libxml2`, `libffi`)
  - Runs `.github/scripts/run-platform-tests.sh macos`
- **Windows**
  - **MinGW path**
    - Sets up MSYS2 + MinGW64 toolchain and dependencies
    - Runs `.github/scripts/run-platform-tests.sh windows`
    - Configures CMake with `-DCICEST_BUILD_E2E_TESTS=OFF`, so the MinGW leg builds the
      project and runs non-e2e tests only
    - This leg is non-blocking (`continue-on-error`) so CI can stay green if MinGW support is temporarily unavailable
  - **MSVC path**
    - Sets up the Visual Studio developer command environment
    - Installs LLVM and Ninja (`choco install llvm ninja`)
    - Runs `.github/scripts/run-msvc-tests.ps1`
    - Prefers a Rust-style `Ninja + clang-cl` build on Windows, with a `Visual Studio 2022 + ClangCL` fallback if Ninja is unavailable
    - Uses the installed LLVM if it exposes `LLVMConfig.cmake`; otherwise downloads the matching `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` developer archive as a fallback
    - Runs the full CTest suite and then a CLI end-to-end smoke test that builds and executes a sample program through `cstc` / `cstc_inspect`
    - If LLVM CMake metadata is unavailable, the script fails instead of skipping so this leg remains required
    - This leg is required, alongside Linux and macOS

## 2) `Lint and Format` workflow

File: `.github/workflows/lint-format.yml`

Runs on `ubuntu-latest` only.

- Installs Nix (`cachix/install-nix-action`)
- Executes `nix run .#lint --print-build-logs`
- The script:
  - Requires `clangd >= 19`
  - Prefers `clang++` and validates C++23 `std::expected` support
  - Configures and builds with CMake + Ninja (`-Werror` enabled)
  - Runs `clang-format --dry-run --Werror` against tracked C/C++ sources

## Trigger Rules

Both workflows trigger on:

- Pushes to `main` or `master`
- All pull requests

## Local Reproduction

### Linux tests (Nix path)

```bash
NIX_CONFIG='experimental-features = nix-command flakes' bash .github/scripts/run-tests.sh
```

### Lint + format (Linux, Nix app)

```bash
nix run .#lint
```

### Platform test script (manual use)

```bash
bash .github/scripts/run-platform-tests.sh macos
bash .github/scripts/run-platform-tests.sh windows
pwsh -File .github/scripts/run-msvc-tests.ps1
```

The Windows MinGW script invocation above skips the end-to-end suite by passing
`-DCICEST_BUILD_E2E_TESTS=OFF`.
