$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build"
$CachePath = Join-Path $BuildDir "CMakeCache.txt"

if (!(Test-Path -LiteralPath $CachePath -PathType Leaf)) {
    throw "CMake is not configured. Run ./build.ps1 before checking the RVA catalog."
}

& cmake --build $BuildDir --config Release --target check_rva_catalog
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
