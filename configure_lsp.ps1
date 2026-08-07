param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build-lsp"
$DatabasePath = Join-Path $BuildDir "compile_commands.json"
$FingerprintPath = Join-Path $BuildDir ".configure-inputs.sha256"

function Get-RelativeProjectPath([string]$Path) {
    return $Path.Substring($Root.Length).TrimStart('\', '/').Replace('\', '/')
}

function Get-ConfigureFingerprint {
    $Lines = [System.Collections.Generic.List[string]]::new()
    $ConfigureInputs = @(
        (Join-Path $Root "CMakeLists.txt"),
        $MyInvocation.MyCommand.Path
    )

    $CMakeDir = Join-Path $Root "cmake"
    if (Test-Path -LiteralPath $CMakeDir -PathType Container) {
        $ConfigureInputs += Get-ChildItem -LiteralPath $CMakeDir -Recurse -File -Filter "*.cmake" |
            Select-Object -ExpandProperty FullName
    }

    foreach ($Path in ($ConfigureInputs | Sort-Object -Unique)) {
        $Hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        $Lines.Add("config:$((Get-RelativeProjectPath $Path)):$Hash")
    }

    foreach ($SourceRootName in @("src", "tools")) {
        $SourceRoot = Join-Path $Root $SourceRootName
        if (!(Test-Path -LiteralPath $SourceRoot -PathType Container)) {
            continue
        }

        Get-ChildItem -LiteralPath $SourceRoot -Recurse -File |
            Where-Object { $_.Extension -in @(".c", ".cc", ".cpp", ".cxx") } |
            ForEach-Object { $Lines.Add("source:$((Get-RelativeProjectPath $_.FullName))") }
    }

    $Payload = [Text.Encoding]::UTF8.GetBytes((($Lines | Sort-Object) -join "`n"))
    $Sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($Sha256.ComputeHash($Payload))).Replace("-", "").ToLowerInvariant()
    } finally {
        $Sha256.Dispose()
    }
}

$Fingerprint = Get-ConfigureFingerprint
if (!$Force -and
    (Test-Path -LiteralPath $DatabasePath -PathType Leaf) -and
    (Test-Path -LiteralPath $FingerprintPath -PathType Leaf) -and
    ((Get-Content -LiteralPath $FingerprintPath -Raw).Trim() -eq $Fingerprint)) {
    Write-Output "clangd compilation database is current."
    return
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $VsWhere -PathType Leaf)) {
    throw "vswhere.exe not found: $VsWhere"
}

$VsPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$VsPath) {
    throw "Visual Studio C++ x64 tools were not found"
}

$VsDevCmd = Join-Path $VsPath "Common7\Tools\VsDevCmd.bat"
$Ninja = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (!(Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) {
    throw "VsDevCmd.bat not found: $VsDevCmd"
}
if (!(Test-Path -LiteralPath $Ninja -PathType Leaf)) {
    throw "Visual Studio Ninja executable not found: $Ninja"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$CachePath = Join-Path $BuildDir "CMakeCache.txt"
if ((Test-Path -LiteralPath $CachePath -PathType Leaf) -and
    !(Select-String -LiteralPath $CachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=Ninja$' -Quiet)) {
    throw "The existing build-lsp directory does not use the Ninja generator; remove it and retry"
}

$CmdPath = Join-Path $BuildDir "configure_lsp.cmd"
@"
@echo on
call "$VsDevCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cmake -S "$Root" -B "$BuildDir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_MAKE_PROGRAM="$Ninja"
if errorlevel 1 exit /b %errorlevel%
"@ | Set-Content -LiteralPath $CmdPath -Encoding ASCII

cmd.exe /d /s /c "`"$CmdPath`""
if ($LASTEXITCODE -ne 0) {
    throw "clangd compilation database configuration failed with exit code $LASTEXITCODE"
}
if (!(Test-Path -LiteralPath $DatabasePath -PathType Leaf)) {
    throw "CMake did not create the expected compilation database: $DatabasePath"
}

Set-Content -LiteralPath $FingerprintPath -Value $Fingerprint -Encoding ASCII -NoNewline
Write-Output "Updated clangd compilation database: $DatabasePath"
