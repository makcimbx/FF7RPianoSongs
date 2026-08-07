Set-StrictMode -Version Latest

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Expected, [string]$Description)
    $actual = @($Value.PSObject.Properties.Name | Sort-Object)
    $expectedSorted = @($Expected | Sort-Object)
    if ($actual.Count -ne $expectedSorted.Count -or
        @(Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actual).Count -ne 0) {
        throw "$Description fields must be exactly: $($Expected -join ', ')"
    }
}

function Assert-NormalizedRelativePath {
    param([string]$Path, [string]$Description)
    if ([string]::IsNullOrWhiteSpace($Path) -or [System.IO.Path]::IsPathRooted($Path) -or
        $Path.Contains('\') -or $Path.StartsWith('/') -or $Path.EndsWith('/') -or
        @($Path.Split('/')) -contains '..' -or @($Path.Split('/')) -contains '.') {
        throw "$Description must be a normalized relative path: '$Path'"
    }
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash(
            [System.Text.Encoding]::UTF8.GetBytes($Text)))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-ProductionInputRelativePaths([string]$InputRoot) {
    $relative = [System.Collections.Generic.List[string]]::new()
    foreach ($path in @("CMakeLists.txt", "build.ps1", "release.json",
            "cmake/release_identity.generated.h.in", "src/game/rva_catalog.json", "tools/generate_rva_catalog.py")) {
        $relative.Add($path)
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $InputRoot "src") -Recurse -File) {
        $path = [System.IO.Path]::GetRelativePath($InputRoot, $file.FullName).Replace('\', '/')
        if ($path.StartsWith("src/tests/") -or $path.StartsWith("src/tools/")) {
            continue
        }
        if ($file.Extension -in @(".c", ".cpp", ".h", ".hpp", ".inc")) {
            $relative.Add($path)
        }
    }
    return @($relative | Sort-Object -Unique)
}

function Assert-BuildProvenance {
    param(
        [string]$InputRoot,
        [string]$DllPath,
        [string]$RecordPath,
        [string]$ExpectedConfiguration,
        [string[]]$ExpectedInputs,
        [string]$CacheRoot
    )
    foreach ($required in @($DllPath, $RecordPath)) {
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Required provenance artifact is missing: $required"
        }
    }
    $record = Get-Content -LiteralPath $RecordPath -Raw | ConvertFrom-Json
    Assert-ExactProperties $record `
        @("schema", "configuration", "dll", "toolchain", "cmake", "releaseIdentity", "productionInputs", "productionInputSetSha256") `
        "Build provenance"
    if ($record.schema -ne "ff7rpianosongs.build-provenance.v2" -or
        $record.configuration -ne $ExpectedConfiguration) {
        throw "Build provenance schema or configuration does not match the requested artifact"
    }
    Assert-ExactProperties $record.dll @("path", "sha256") "Build provenance DLL"
    if ($record.dll.path -ne "bin/$ExpectedConfiguration/FF7RPianoSongs.dll" -or
        $record.dll.sha256 -ne (Get-FileSha256 $DllPath)) {
        throw "Built DLL does not match its provenance record"
    }
    Assert-ExactProperties $record.toolchain `
        @("visualStudioInstallationPath", "compilerPath", "compilerVersion", "compilerSha256", `
          "cmakePath", "cmakeVersion", "cmakeSha256", "generator", "generatorPlatform", "generatorToolset") `
        "Build provenance toolchain"
    foreach ($tool in @(
        [pscustomobject]@{ Name = "compiler"; Path = $record.toolchain.compilerPath; Hash = $record.toolchain.compilerSha256 },
        [pscustomobject]@{ Name = "CMake"; Path = $record.toolchain.cmakePath; Hash = $record.toolchain.cmakeSha256 })) {
        if (!(Test-Path -LiteralPath $tool.Path -PathType Leaf) -or
            (Get-FileSha256 $tool.Path) -ne $tool.Hash) {
            throw "Current $($tool.Name) executable does not match build provenance"
        }
    }
    if ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($record.toolchain.compilerPath).FileVersion -ne
        $record.toolchain.compilerVersion) {
        throw "Current compiler version does not match build provenance"
    }
    if ((& $record.toolchain.cmakePath --version | Select-Object -First 1) -ne $record.toolchain.cmakeVersion) {
        throw "Current CMake version does not match build provenance"
    }
    Assert-ExactProperties $record.cmake @("cachePath", "cacheSha256") "Build provenance CMake identity"
    Assert-NormalizedRelativePath $record.cmake.cachePath "CMake cache path"
    $cache = Join-Path $CacheRoot $record.cmake.cachePath
    if (!(Test-Path -LiteralPath $cache -PathType Leaf) -or
        (Get-FileSha256 $cache) -ne $record.cmake.cacheSha256) {
        throw "Current CMake cache does not match build provenance"
    }
    Assert-ExactProperties $record.releaseIdentity `
        @("authorityPath", "authoritySha256", "generatedHeaderPath", "generatedHeaderSha256") `
        "Build provenance release identity"
    if ($record.releaseIdentity.authorityPath -ne "release.json" -or
        $record.releaseIdentity.generatedHeaderPath -ne "generated/release_identity.generated.h") {
        throw "Build provenance release identity paths are not canonical"
    }
    $authority = Join-Path $InputRoot $record.releaseIdentity.authorityPath
    $generatedHeader = Join-Path $CacheRoot $record.releaseIdentity.generatedHeaderPath
    if (!(Test-Path -LiteralPath $authority -PathType Leaf) -or
        !(Test-Path -LiteralPath $generatedHeader -PathType Leaf) -or
        (Get-FileSha256 $authority) -ne $record.releaseIdentity.authoritySha256 -or
        (Get-FileSha256 $generatedHeader) -ne $record.releaseIdentity.generatedHeaderSha256) {
        throw "Current release authority or generated header does not match build provenance"
    }

    $actualInputs = @($record.productionInputs)
    if ($actualInputs.Count -ne $ExpectedInputs.Count) {
        throw "Production input inventory does not match build provenance"
    }
    $seen = @{}
    $identity = ""
    foreach ($input in $actualInputs) {
        Assert-ExactProperties $input @("path", "sha256") "Production input"
        Assert-NormalizedRelativePath $input.path "Production input path"
        if ($seen.ContainsKey($input.path)) {
            throw "Duplicate production input: $($input.path)"
        }
        $seen[$input.path] = $true
        $absolute = Join-Path $InputRoot $input.path
        if (!(Test-Path -LiteralPath $absolute -PathType Leaf) -or
            (Get-FileSha256 $absolute) -ne $input.sha256) {
            throw "Production input does not match build provenance: $($input.path)"
        }
        $identity += "$($input.path)`t$($input.sha256)`n"
    }
    if (@(Compare-Object -ReferenceObject @($ExpectedInputs | Sort-Object) `
            -DifferenceObject @($actualInputs.path | Sort-Object)).Count -ne 0 -or
        (Get-TextSha256 $identity) -ne $record.productionInputSetSha256) {
        throw "Production input set identity does not match build provenance"
    }
}
