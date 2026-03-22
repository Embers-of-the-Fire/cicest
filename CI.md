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
  - **MSVC path**
    - Sets up the Visual Studio developer command environment
    - Installs LLVM (`choco install llvm`)
    - Runs `.github/scripts/run-msvc-tests.ps1`
    - If LLVM CMake metadata is unavailable, the script skips this optional run
  - Both Windows legs are non-blocking (`continue-on-error`) so CI remains green if Windows support is temporarily unavailable

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
