param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidatePattern('^[A-Za-z0-9_.+-]+$')]
    [string[]]$Target = @(),

    [ValidateRange(1, 64)]
    [int]$Parallel = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $VsWhere)) {
    throw "vswhere.exe not found: $VsWhere"
}

$VsPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$VsPath) {
    throw "Visual Studio C++ x64 tools were not found"
}

$VsDevCmd = Join-Path $VsPath "Common7\Tools\VsDevCmd.bat"
if (!(Test-Path -LiteralPath $VsDevCmd)) {
    throw "VsDevCmd.bat not found: $VsDevCmd"
}

$BuildDir = Join-Path $Root "build"
$OutDir = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$CachePath = Join-Path $BuildDir "CMakeCache.txt"

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-ProductionInputPaths {
    $relative = [System.Collections.Generic.List[string]]::new()
    foreach ($path in @("CMakeLists.txt", "build.ps1", "release.json",
            "cmake/release_identity.generated.h.in", "src/game/rva_catalog.json", "tools/generate_rva_catalog.py")) {
        $relative.Add($path)
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root "src") -Recurse -File) {
        $path = [System.IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
        if ($path.StartsWith("src/tests/") -or $path.StartsWith("src/tools/")) { continue }
        if ($file.Extension -in @(".c", ".cpp", ".h", ".hpp", ".inc")) {
            $relative.Add($path)
        }
    }
    return @($relative | Sort-Object -Unique)
}

function Get-CMakeCacheValue([string]$Name) {
    $line = Get-Content -LiteralPath $CachePath | Where-Object { $_ -match "^$([regex]::Escape($Name)):[^=]*=" } | Select-Object -First 1
    if (!$line) { return "" }
    return ($line -split "=", 2)[1]
}

function Get-ConfiguredCompilerPath {
    $compilerFile = Get-ChildItem -LiteralPath (Join-Path $BuildDir "CMakeFiles") `
        -Filter "CMakeCXXCompiler.cmake" -Recurse -File | Select-Object -First 1
    if (!$compilerFile) { throw "CMake compiler identity was not generated" }
    $match = [regex]::Match(
        (Get-Content -LiteralPath $compilerFile.FullName -Raw),
        '(?m)^set\(CMAKE_CXX_COMPILER "([^"]+)"\)')
    if (!$match.Success) { throw "CMake compiler identity does not name the configured compiler" }
    return $match.Groups[1].Value
}

function Write-BuildProvenance {
    $dll = Join-Path $BuildDir "bin/$Configuration/FF7RPianoSongs.dll"
    if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw "Built DLL is missing: $dll" }
    if (!(Test-Path -LiteralPath $CachePath -PathType Leaf)) { throw "CMake cache is missing: $CachePath" }

    $compiler = Get-ConfiguredCompilerPath
    if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Configured C++ compiler is missing: $compiler"
    }
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $generatedReleaseHeader = Join-Path $BuildDir "generated/release_identity.generated.h"
    if (!(Test-Path -LiteralPath $generatedReleaseHeader -PathType Leaf)) {
        throw "Generated release identity header is missing"
    }
    $inputs = @()
    foreach ($relative in Get-ProductionInputPaths) {
        $absolute = Join-Path $Root $relative
        if (!(Test-Path -LiteralPath $absolute -PathType Leaf)) {
            throw "Production build input is missing: $relative"
        }
        $inputs += [ordered]@{ path = $relative; sha256 = Get-FileSha256 $absolute }
    }
    $inputIdentity = (($inputs | ForEach-Object { "$($_.path)`t$($_.sha256)`n" }) -join "")
    $record = [ordered]@{
        schema = "ff7rpianosongs.build-provenance.v2"
        configuration = $Configuration
        dll = [ordered]@{
            path = "bin/$Configuration/FF7RPianoSongs.dll"
            sha256 = Get-FileSha256 $dll
        }
        toolchain = [ordered]@{
            visualStudioInstallationPath = $VsPath
            compilerPath = $compiler
            compilerVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
            compilerSha256 = Get-FileSha256 $compiler
            cmakePath = $cmake
            cmakeVersion = (& $cmake --version | Select-Object -First 1)
            cmakeSha256 = Get-FileSha256 $cmake
            generator = Get-CMakeCacheValue "CMAKE_GENERATOR"
            generatorPlatform = Get-CMakeCacheValue "CMAKE_GENERATOR_PLATFORM"
            generatorToolset = Get-CMakeCacheValue "CMAKE_GENERATOR_TOOLSET"
        }
        cmake = [ordered]@{
            cachePath = "CMakeCache.txt"
            cacheSha256 = Get-FileSha256 $CachePath
        }
        releaseIdentity = [ordered]@{
            authorityPath = "release.json"
            authoritySha256 = Get-FileSha256 (Join-Path $Root "release.json")
            generatedHeaderPath = "generated/release_identity.generated.h"
            generatedHeaderSha256 = Get-FileSha256 $generatedReleaseHeader
        }
        productionInputs = $inputs
        productionInputSetSha256 = Get-TextSha256 $inputIdentity
    }
    $path = Join-Path (Split-Path -Parent $dll) "FF7RPianoSongs.provenance.json"
    [System.IO.File]::WriteAllText(
        $path,
        (($record | ConvertTo-Json -Depth 8) + "`n"),
        [System.Text.UTF8Encoding]::new($false))
}
$ConfigureCommands = if (Test-Path -LiteralPath $CachePath -PathType Leaf) {
    "rem Reusing existing CMake configuration; ZERO_CHECK preserves regeneration."
} else {
    @"
cmake -S "$Root" -B "$BuildDir" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b %errorlevel%
"@
}
$TargetArguments = if ($Target.Count -gt 0) { " --target " + ($Target -join " ") } else { "" }
$BuildsArtifact = $Target.Count -eq 0 -or $Target -contains "FF7RPianoSongs"
$CopyCommands = if ($BuildsArtifact) {
    @"
copy /Y "$BuildDir\bin\$Configuration\FF7RPianoSongs.dll" "$OutDir\FF7RPianoSongs.asi"
if errorlevel 1 exit /b %errorlevel%
"@
} else {
    "rem FF7RPianoSongs was not selected; no ASI copy is required."
}

$CmdPath = Join-Path $BuildDir "build_ff7r_piano_songs.cmd"
@"
@echo on
call "$VsDevCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
$ConfigureCommands
cmake --build "$BuildDir" --config $Configuration$TargetArguments --parallel $Parallel
if errorlevel 1 exit /b %errorlevel%
$CopyCommands
"@ | Set-Content -LiteralPath $CmdPath -Encoding ASCII

cmd.exe /d /s /c "`"$CmdPath`""
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

if ($BuildsArtifact) {
    Write-BuildProvenance
}

try {
    & (Join-Path $Root "configure_lsp.ps1")
} catch {
    Write-Warning "The build succeeded, but the clangd compilation database could not be refreshed: $($_.Exception.Message)"
}

$SelectedTargets = if ($Target.Count -gt 0) { $Target -join ", " } else { "default ALL_BUILD" }
if ($BuildsArtifact) {
    $Artifact = Join-Path $OutDir "FF7RPianoSongs.asi"
    if (!(Test-Path -LiteralPath $Artifact)) {
        throw "Expected artifact was not created: $Artifact"
    }
    Get-Item -LiteralPath $Artifact | Select-Object FullName, Length, LastWriteTime
} else {
    Write-Output "Build completed for targets: $SelectedTargets"
}
