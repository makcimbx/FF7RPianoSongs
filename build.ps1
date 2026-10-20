param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidatePattern('^[A-Za-z0-9_.+-]+$')]
    [string[]]$Target = @(),

    [ValidatePattern('^[a-z0-9][a-z0-9-]+$')]
    [string]$CatalogBuildId,

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

. (Join-Path $Root "tools/build_provenance.ps1")

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

# Pass the default explicitly so a prior catalog selection cannot survive in CMakeCache.
$Catalog = Get-Content -LiteralPath (Join-Path $Root "src/game/rva_catalog.json") -Raw | ConvertFrom-Json
if (!$CatalogBuildId) { $CatalogBuildId = [string]$Catalog.default_build }
if ($CatalogBuildId -notmatch '^[a-z0-9][a-z0-9._-]*$' -or $CatalogBuildId -notin @($Catalog.builds.id)) {
    throw "Unknown catalog build: $CatalogBuildId"
}
$CatalogBuildArgument = " -D FF7RP_GAME_BUILD=$CatalogBuildId"
$ConfigureCommands = @"
cmake -S "$Root" -B "$BuildDir" -G "Visual Studio 17 2022" -A x64$CatalogBuildArgument
if errorlevel 1 exit /b %errorlevel%
"@
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

foreach ($builtTarget in @("FF7RPianoSongs", "FF7RPianoSongsProxy")) {
    if ($Target.Count -eq 0 -or $Target -contains $builtTarget) {
        Write-ProductBuildProvenance -InputRoot $Root -BuildRoot $BuildDir `
            -Configuration $Configuration -Target $builtTarget `
            -VisualStudioInstallationPath $VsPath `
            -CompilerPath (Get-ConfiguredCompilerPath) `
            -CMakePath (Get-Command cmake -ErrorAction Stop).Source
    }
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
