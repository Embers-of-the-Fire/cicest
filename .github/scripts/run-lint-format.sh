#!/usr/bin/env bash
set -euo pipefail

export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"

if command -v llvm-config >/dev/null 2>&1; then
  export LLVM_DIR="$(llvm-config --cmakedir)"
fi

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_FLAGS="-Werror"

cmake --build build

mapfile -t source_files < <(git ls-files '*.h' '*.hh' '*.hpp' '*.c' '*.cc' '*.cpp' '*.cxx')
if ((${#source_files[@]} == 0)); then
  echo "No C/C++ source files found for format check."
  exit 0
fi

clang-format --dry-run --Werror "${source_files[@]}"

