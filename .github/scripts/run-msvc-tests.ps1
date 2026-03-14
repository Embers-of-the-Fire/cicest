$ErrorActionPreference = "Stop"

$llvmRootCandidates = @()

foreach ($candidate in @($env:LLVM_PATH, $env:LLVM_ROOT)) {
    if (-not $candidate) {
        continue
    }

    $trimmed = $candidate.Trim().Trim('"')
    if ($trimmed -and (Test-Path $trimmed)) {
        $llvmRootCandidates += $trimmed
    }
}

$programFilesCandidates = @()
foreach ($basePath in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $basePath) {
        continue
    }

    $candidate = Join-Path $basePath "LLVM"
    if (Test-Path $candidate) {
        $programFilesCandidates += $candidate
    }
}

$llvmRootCandidates += $programFilesCandidates

$clangCommand = Get-Command clang -ErrorAction SilentlyContinue
if ($clangCommand) {
    $clangRoot = Split-Path (Split-Path $clangCommand.Source -Parent) -Parent
    if ($clangRoot -and (Test-Path $clangRoot)) {
        $llvmRootCandidates += $clangRoot
    }
}

$llvmRootCandidates = $llvmRootCandidates | Select-Object -Unique

$llvmCmakeDirCandidates = @()

if ($env:LLVM_DIR) {
    $llvmDirCandidate = $env:LLVM_DIR.Trim().Trim('"')
    if ($llvmDirCandidate -and (Test-Path (Join-Path $llvmDirCandidate "LLVMConfig.cmake"))) {
        $llvmCmakeDirCandidates += $llvmDirCandidate
    }
}

foreach ($llvmRoot in $llvmRootCandidates) {
    $candidate = Join-Path $llvmRoot "lib\cmake\llvm"
    if (Test-Path (Join-Path $candidate "LLVMConfig.cmake")) {
        $llvmCmakeDirCandidates += $candidate
    }
}

if ($llvmCmakeDirCandidates.Count -eq 0) {
    $searchRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($searchRoot in $searchRoots) {
        $foundConfig = Get-ChildItem -Path $searchRoot -Filter LLVMConfig.cmake -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "cmake\\llvm\\LLVMConfig\.cmake$" } |
            Select-Object -First 1
        if ($foundConfig) {
            $llvmCmakeDirCandidates += $foundConfig.Directory.FullName
            break
        }
    }
}

$llvmCmakeDirCandidates = $llvmCmakeDirCandidates | Select-Object -Unique

if ($llvmCmakeDirCandidates.Count -eq 0) {
    Write-Warning "LLVM CMake package was not found. Skipping optional MSVC test run."
    exit 0
}

$llvmCmakeDir = $llvmCmakeDirCandidates[0]
$llvmRoot = Split-Path (Split-Path (Split-Path $llvmCmakeDir -Parent) -Parent) -Parent

$env:PATH = "$llvmRoot\bin;$env:PATH"

cmake -S . -B build -G "NMake Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
  -DCICEST_BUILD_TESTS=ON `
  -DLLVM_DIR="$llvmCmakeDir"

cmake --build build
ctest --test-dir build --output-on-failure
