#!/usr/bin/env bash
set -euo pipefail

supports_expected() {
  local compiler="$1"
  cat <<'EOF' | "${compiler}" -std=c++23 -x c++ -fsyntax-only - >/dev/null 2>&1
#include <expected>
int main() {
  std::expected<int, int> value(1);
  return *value;
}
EOF
}

check_clangd_version() {
  if ! command -v clangd >/dev/null 2>&1; then
    echo "clangd not found in PATH."
    return 1
  fi

  local version_line major
  version_line="$(clangd --version | head -n1)"
  major="$(echo "${version_line}" | grep -Eo '[0-9]+' | head -n1 || true)"

  if [[ -z "${major}" ]]; then
    echo "Could not parse clangd version from: ${version_line}"
    return 1
  fi

  if (( major < 19 )); then
    echo "clangd >= 19 is required, found: ${version_line}"
    return 1
  fi

  echo "Detected ${version_line}"
}

check_clangd_version

if [[ -n "${CXX:-}" ]]; then
  selected_cxx="${CXX}"
elif command -v clang++ >/dev/null 2>&1 && supports_expected clang++; then
  selected_cxx="clang++"
elif command -v g++ >/dev/null 2>&1 && supports_expected g++; then
  selected_cxx="g++"
else
  echo "No available C++ compiler with std::expected support was found (need C++23 <expected>)."
  exit 1
fi

if [[ -n "${CC:-}" ]]; then
  selected_cc="${CC}"
elif [[ "${selected_cxx}" == "g++" ]]; then
  selected_cc="gcc"
elif [[ "${selected_cxx}" == "clang++" ]]; then
  selected_cc="clang"
else
  selected_cc="${selected_cxx}"
fi

export CC="${selected_cc}"
export CXX="${selected_cxx}"

echo "Using compiler: CC=${CC}, CXX=${CXX}"

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
