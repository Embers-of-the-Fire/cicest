$ErrorActionPreference = "Stop"

function Resolve-ExistingPath {
    param(
        [string]$Path
    )

    if (-not $Path) {
        return $null
    }

    $trimmed = $Path.Trim().Trim('"')
    if (-not $trimmed) {
        return $null
    }

    try {
        return (Resolve-Path -LiteralPath $trimmed -ErrorAction Stop).Path
    }
    catch {
        return $null
    }
}

function Add-UniquePath {
    param(
        [ref]$Collection,
        [string]$Path
    )

    $resolvedPath = Resolve-ExistingPath $Path
    if (-not $resolvedPath) {
        return
    }

    if ($Collection.Value -notcontains $resolvedPath) {
        $Collection.Value += $resolvedPath
    }
}

function Add-LlvmCmakeDirCandidate {
    param(
        [ref]$Collection,
        [string]$Candidate
    )

    if (-not $Candidate) {
        return
    }

    $trimmed = $Candidate.Trim().Trim('"')
    if (-not $trimmed) {
        return
    }

    $candidateDirs = @($trimmed)

    $resolvedCandidate = Resolve-ExistingPath $trimmed
    if ($resolvedCandidate) {
        if (Test-Path -LiteralPath $resolvedCandidate -PathType Leaf) {
            if ((Split-Path $resolvedCandidate -Leaf) -eq "LLVMConfig.cmake") {
                $configDir = Resolve-ExistingPath (Split-Path $resolvedCandidate -Parent)
                if ($configDir -and ($Collection.Value -notcontains $configDir)) {
                    $Collection.Value += $configDir
                }
            }
            return
        }

        $candidateDirs += (Join-Path $resolvedCandidate "lib\cmake\llvm")
        $candidateDirs += (Join-Path $resolvedCandidate "cmake\llvm")
    }

    foreach ($candidateDir in $candidateDirs) {
        $configPath = Join-Path $candidateDir "LLVMConfig.cmake"
        if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            $resolvedConfigDir = Resolve-ExistingPath $candidateDir
            if ($resolvedConfigDir -and ($Collection.Value -notcontains $resolvedConfigDir)) {
                $Collection.Value += $resolvedConfigDir
            }
        }
    }
}

function Get-LlvmArchiveVersion {
    param(
        [string[]]$LlvmRoots
    )

    foreach ($llvmRoot in $LlvmRoots) {
        $clangPath = Resolve-ExistingPath (Join-Path $llvmRoot "bin\clang.exe")
        if (-not $clangPath) {
            continue
        }

        $versionOutput = & $clangPath --version 2>$null | Select-Object -First 1
        if ($versionOutput -match "version ([0-9]+\.[0-9]+\.[0-9]+)") {
            return $Matches[1]
        }
    }

    $clangCommand = Get-Command clang -ErrorAction SilentlyContinue
    if ($clangCommand) {
        $versionOutput = & $clangCommand.Source --version 2>$null | Select-Object -First 1
        if ($versionOutput -match "version ([0-9]+\.[0-9]+\.[0-9]+)") {
            return $Matches[1]
        }
    }

    throw "Could not determine the LLVM version needed for the Windows developer archive fallback."
}

function Install-LlvmDeveloperArchiveFallback {
    param(
        [string]$Version
    )

    $archiveName = "clang+llvm-$Version-x86_64-pc-windows-msvc.tar.xz"
    $archiveUrl = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$Version/$archiveName"
    $archivePath = Join-Path $env:RUNNER_TEMP $archiveName
    $extractRoot = Join-Path $env:RUNNER_TEMP "llvm-dev-$Version"

    Write-Warning "Installed LLVM does not expose LLVMConfig.cmake. Downloading $archiveName because this project links against LLVM as a library."

    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }

    Invoke-WebRequest -Uri $archiveUrl -OutFile $archivePath
    New-Item -Path $extractRoot -ItemType Directory -Force | Out-Null
    tar -xf $archivePath -C $extractRoot

    $llvmConfig = Get-ChildItem -Path $extractRoot -Filter LLVMConfig.cmake -Recurse -File -ErrorAction Stop |
        Where-Object { $_.FullName -match "lib\\cmake\\llvm\\LLVMConfig\.cmake$" } |
        Select-Object -First 1
    if (-not $llvmConfig) {
        throw "LLVM developer archive fallback did not contain LLVMConfig.cmake: $archiveUrl"
    }

    return $llvmConfig.Directory.FullName
}

function Resolve-LlvmRootFromCmakeDir {
    param(
        [string]$LlvmCmakeDir
    )

    $resolvedCmakeDir = Resolve-ExistingPath $LlvmCmakeDir
    if (-not $resolvedCmakeDir) {
        throw "LLVM CMake directory '$LlvmCmakeDir' does not exist."
    }

    $currentPath = $resolvedCmakeDir
    for ($i = 0; $i -lt 5; $i++) {
        $currentPath = Split-Path $currentPath -Parent
        if (-not $currentPath) {
            break
        }

        $clangPath = Resolve-ExistingPath (Join-Path $currentPath "bin\clang.exe")
        $clangClPath = Resolve-ExistingPath (Join-Path $currentPath "bin\clang-cl.exe")
        if ($clangPath -or $clangClPath) {
            return $currentPath
        }
    }

    throw "Could not resolve the LLVM installation root from LLVM_DIR '$resolvedCmakeDir'."
}

$llvmRootCandidates = @()
$llvmCmakeDirCandidates = @()

foreach ($candidate in @($env:LLVM_PATH, $env:LLVM_ROOT, $env:LLVM_INSTALL_DIR, $env:LLVMInstallDir, $env:LLVM_HOME)) {
    Add-UniquePath ([ref]$llvmRootCandidates) $candidate
}

if ($env:ProgramFiles) {
    Add-UniquePath ([ref]$llvmRootCandidates) (Join-Path $env:ProgramFiles "LLVM")
}

if (${env:ProgramFiles(x86)}) {
    Add-UniquePath ([ref]$llvmRootCandidates) (Join-Path ${env:ProgramFiles(x86)} "LLVM")
}

if ($env:SystemDrive) {
    Add-UniquePath ([ref]$llvmRootCandidates) (Join-Path $env:SystemDrive "Program Files\LLVM")
    Add-UniquePath ([ref]$llvmRootCandidates) (Join-Path $env:SystemDrive "Program Files (x86)\LLVM")
}

foreach ($commandName in @("llvm-config", "clang", "clang-cl")) {
    $command = Get-Command $commandName -ErrorAction SilentlyContinue
    if (-not $command) {
        continue
    }

    $commandBinDir = Split-Path $command.Source -Parent
    $commandRoot = Split-Path $commandBinDir -Parent
    Add-UniquePath ([ref]$llvmRootCandidates) $commandRoot

    if ($commandName -eq "llvm-config") {
        try {
            Add-LlvmCmakeDirCandidate ([ref]$llvmCmakeDirCandidates) (& $command.Source --cmakedir)
        }
        catch {
        }
    }
}

$llvmRootCandidates = @($llvmRootCandidates | Select-Object -Unique)

if ($env:LLVM_DIR) {
    Add-LlvmCmakeDirCandidate ([ref]$llvmCmakeDirCandidates) $env:LLVM_DIR
    Add-UniquePath ([ref]$llvmRootCandidates) $env:LLVM_DIR
}

foreach ($llvmRoot in $llvmRootCandidates) {
    Add-LlvmCmakeDirCandidate ([ref]$llvmCmakeDirCandidates) $llvmRoot
}

if ($llvmCmakeDirCandidates.Count -eq 0) {
    $searchRoots = $llvmRootCandidates + @($env:ProgramFiles, ${env:ProgramFiles(x86)})
    $searchRoots = @($searchRoots | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)
    foreach ($searchRoot in $searchRoots) {
        $foundConfig = Get-ChildItem -Path $searchRoot -Filter LLVMConfig.cmake -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "cmake\\llvm\\LLVMConfig\.cmake$" } |
            Select-Object -First 1
        if ($foundConfig) {
            Add-LlvmCmakeDirCandidate ([ref]$llvmCmakeDirCandidates) $foundConfig.Directory.FullName
            break
        }
    }
}

$llvmCmakeDirCandidates = @($llvmCmakeDirCandidates | Select-Object -Unique)

if ($llvmCmakeDirCandidates.Count -eq 0) {
    $archiveVersion = Get-LlvmArchiveVersion $llvmRootCandidates
    $archiveCmakeDir = Install-LlvmDeveloperArchiveFallback $archiveVersion
    Add-LlvmCmakeDirCandidate ([ref]$llvmCmakeDirCandidates) $archiveCmakeDir
}

if ($llvmCmakeDirCandidates.Count -eq 0) {
    $searchedRoots = $llvmRootCandidates | Where-Object { $_ } | Sort-Object -Unique
    $searchedRootsText = if ($searchedRoots.Count -gt 0) { $searchedRoots -join "; " } else { "<none>" }
    throw "LLVM CMake package was not found after fallback. Checked LLVM_DIR/LLVM_PATH/LLVM_ROOT, llvm-config, clang, and roots: $searchedRootsText"
}

$llvmCmakeDir = @($llvmCmakeDirCandidates)[0]
$llvmRoot = Resolve-LlvmRootFromCmakeDir $llvmCmakeDir

$env:PATH = "$llvmRoot\bin;$env:PATH"
Write-Host "Using LLVM_DIR=$llvmCmakeDir"

function Resolve-BuildArtifact {
    param(
        [string]$BuildDir,
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        $candidatePath = Join-Path $BuildDir $candidate
        $resolvedCandidate = Resolve-ExistingPath $candidatePath
        if ($resolvedCandidate) {
            return $resolvedCandidate
        }
    }

    $fileName = Split-Path $Candidates[0] -Leaf
    $found = Get-ChildItem -Path $BuildDir -Filter $fileName -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    throw "Could not find built artifact '$fileName' under '$BuildDir'."
}

function Resolve-E2EExecutablePath {
    param(
        [string]$OutputStem
    )

    foreach ($candidate in @($OutputStem, "$OutputStem.exe")) {
        $resolvedCandidate = Resolve-ExistingPath $candidate
        if ($resolvedCandidate) {
            return $resolvedCandidate
        }
    }

    throw "Could not find end-to-end executable output for stem '$OutputStem'."
}

function Assert-RegularFile {
    param(
        [string]$Path,
        [string]$Description
    )

    $resolvedPath = Resolve-ExistingPath $Path
    if (-not $resolvedPath) {
        throw "$Description was not created at '$Path'."
    }

    $item = Get-Item -LiteralPath $resolvedPath -ErrorAction Stop
    if (-not $item.PSIsContainer -and $item.Length -gt 0) {
        return $resolvedPath
    }

    throw "$Description at '$resolvedPath' is empty."
}

function Invoke-EndToEndTests {
    param(
        [string]$BuildDir
    )

    $cstcExe = Resolve-BuildArtifact $BuildDir @(
        "compiler/cstc/Debug/cstc.exe",
        "compiler/cstc/cstc.exe"
    )
    $inspectExe = Resolve-BuildArtifact $BuildDir @(
        "compiler/cstc_inspect/Debug/cstc_inspect.exe",
        "compiler/cstc_inspect/cstc_inspect.exe"
    )

    $tempRoot = Join-Path $env:RUNNER_TEMP "cicest-msvc-e2e"
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
    New-Item -Path $tempRoot -ItemType Directory -Force | Out-Null

    $sourcePath = Join-Path $tempRoot "hello.cst"
    $llvmPath = Join-Path $tempRoot "hello.ll"
    $outputStem = Join-Path $tempRoot "hello"

    Set-Content -LiteralPath $sourcePath -Encoding utf8NoBOM -Value @'
fn main() {
    let four: str = to_str(4);
    let answer: str = str_concat(four, "2");
    println(answer);
    str_free(four);
    str_free(answer);
}
'@

    & $inspectExe $sourcePath --out-type llvm -o $llvmPath
    if ($LASTEXITCODE -ne 0) {
        throw "cstc_inspect failed during MSVC end-to-end testing."
    }

    $llvmOutputPath = Assert-RegularFile $llvmPath "LLVM IR output"
    $llvmOutput = Get-Content -LiteralPath $llvmOutputPath -Raw
    if ($llvmOutput -notmatch "define i32 @main\(\)") {
        throw "LLVM IR output from cstc_inspect did not contain an entry point."
    }

    $compileArgs = @($sourcePath, "-o", $outputStem, "--emit", "all", "--linker", "clang++")
    & $cstcExe @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cstc failed during MSVC end-to-end testing."
    }

    Assert-RegularFile "$outputStem.s" "Assembly output" | Out-Null
    Assert-RegularFile "$outputStem.o" "Object output" | Out-Null
    $programPath = Resolve-E2EExecutablePath $outputStem

    $programOutput = & $programPath
    if ($LASTEXITCODE -ne 0) {
        throw "Generated executable '$programPath' failed during MSVC end-to-end testing."
    }

    $normalizedOutput = ($programOutput | ForEach-Object { $_.TrimEnd("`r") }) -join "`n"
    if ($normalizedOutput -ne "42") {
        throw "Generated executable produced unexpected output: '$normalizedOutput'"
    }
}

if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
    throw "clang-cl was not found on PATH after installing LLVM."
}

if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
    throw "clang++ was not found on PATH after installing LLVM."
}

$buildDir = "build-msvc"
if (Test-Path -LiteralPath $buildDir) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

$configureArgs = @(
    "-S", ".",
    "-B", $buildDir,
    "-DCICEST_BUILD_TESTS=ON",
    "-DLLVM_DIR=$llvmCmakeDir"
)
$buildArgs = @("--build", $buildDir, "--parallel")
$ctestArgs = @("--test-dir", $buildDir, "--output-on-failure")

$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
if ($ninjaCommand) {
    Write-Host "Using Ninja + clang-cl for the MSVC build, matching Rust's preferred Windows CI path."
    $configureArgs += @(
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_COMPILER=clang-cl",
        "-DCMAKE_CXX_COMPILER=clang-cl"
    )
} else {
    Write-Host "Ninja was not found; falling back to Visual Studio 2022 + ClangCL."
    $configureArgs += @(
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-T", "ClangCL"
    )
    $buildArgs += @("--config", "Debug")
    $ctestArgs += @("-C", "Debug")
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed for the Windows MSVC test build."
}

& cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed for the Windows MSVC test build."
}

& ctest @ctestArgs
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed for the Windows MSVC test build."
}

Invoke-EndToEndTests -BuildDir $buildDir
