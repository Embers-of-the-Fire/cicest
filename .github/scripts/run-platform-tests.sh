#!/usr/bin/env bash
set -euo pipefail

platform="${1:-}"
build_e2e_tests=ON

case "${platform}" in
  linux)
    ;;
  macos)
    export PATH="$(brew --prefix llvm)/bin:${PATH}"
    export PKG_CONFIG_PATH="$(brew --prefix libxml2)/lib/pkgconfig:$(brew --prefix libffi)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    export CMAKE_PREFIX_PATH="$(brew --prefix llvm);$(brew --prefix libxml2);$(brew --prefix libffi)"
    ;;
  windows)
    export PATH="/mingw64/bin:${PATH}"
    export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    export CMAKE_PREFIX_PATH="/mingw64"
    build_e2e_tests=OFF
    echo "Skipping end-to-end tests on Windows MinGW; running build and non-e2e tests only."
    ;;
  *)
    echo "Unsupported platform '${platform}'. Expected linux, macos, or windows."
    exit 1
    ;;
esac

if command -v llvm-config >/dev/null 2>&1; then
  export LLVM_DIR="$(llvm-config --cmakedir)"
fi

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCICEST_BUILD_TESTS=ON \
  -DCICEST_BUILD_E2E_TESTS="${build_e2e_tests}"

cmake --build build
ctest --test-dir build --output-on-failure
