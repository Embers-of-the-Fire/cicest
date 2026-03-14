$ErrorActionPreference = "Stop"

$llvmCandidates = @(
    $env:LLVM_PATH,
    "C:\Program Files\LLVM"
) | Where-Object { $_ -and (Test-Path $_) }

if ($llvmCandidates.Count -eq 0) {
    throw "LLVM was not found. Ensure LLVM is installed and LLVM_PATH is set."
}

$llvmRoot = $llvmCandidates[0]
$llvmCmakeDir = Join-Path $llvmRoot "lib\cmake\llvm"

if (-not (Test-Path $llvmCmakeDir)) {
    throw "LLVM CMake package was not found at '$llvmCmakeDir'."
}

$env:PATH = "$llvmRoot\bin;$env:PATH"

cmake -S . -B build -G "NMake Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
  -DCICEST_BUILD_TESTS=ON `
  -DLLVM_DIR="$llvmCmakeDir"

cmake --build build
ctest --test-dir build --output-on-failure

