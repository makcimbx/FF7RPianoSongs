$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "release_package.ps1")

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ".ff7rp-release-package-selftest-$PID-$([Guid]::NewGuid().ToString('N'))"
try {
    $fixture = Join-Path $testRoot "fixture"
    foreach ($directory in @("package/docs", "package/End/Binaries/Win64", "src/game", "out-a", "out-b")) {
        New-Item -ItemType Directory -Path (Join-Path $fixture $directory) -Force | Out-Null
    }
    $releaseJson = @{
        schema = "ff7rpianosongs.release.v2"; product = "FF7RPianoSongs"; version = "0.1.0"
        platform = "win64"; license = "MIT"
        targets = @(
            [ordered]@{
                game_build = "1.005"
                supported_executable_catalog_id = "ff7rebirth-steam-win64-6a16ced2"
                archive_basename = "FF7RPianoSongs-0.1.0-win64-ff7r1.005"
            }
        )
    } | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $fixture "release.json"), $releaseJson)
    [IO.File]::WriteAllText((Join-Path $fixture "package/release.json"), $releaseJson)
    [IO.File]::WriteAllText((Join-Path $fixture "src/game/rva_catalog.json"),
        '{"schema_version":3,"default_build":"ff7rebirth-steam-win64-6a16ced2",' +
        '"builds":[{"id":"ff7rebirth-steam-win64-6a16ced2"}],"addresses":[]}')
    $publicDocuments = @(
        "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md",
        "docs/SongFormat.md"
    )
    foreach ($relative in $publicDocuments) {
        $path = Join-Path (Join-Path $fixture "package") $relative
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllText($path, "$relative`n")
    }
    $asi = Join-Path $fixture "package/End/Binaries/Win64/FF7RPianoSongs.asi"
    [IO.File]::WriteAllBytes($asi, [byte[]](0, 1, 2, 3, 255))
    [IO.File]::WriteAllText((Join-Path $fixture "package/End/Binaries/Win64/FF7RPianoSongs.ini"), "[General]`n")
    $provenance = Join-Path $fixture "provenance.json"
    [IO.File]::WriteAllText($provenance, '{"fixture":true}')
    $generatedHeader = Join-Path $fixture "release_identity.generated.h"
    [IO.File]::WriteAllText($generatedHeader, "synthetic generated release identity`n")
    $commit = "0123456789abcdef0123456789abcdef01234567"
    $a = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $fixture "out-a") $asi $provenance $generatedHeader $commit
    $b = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $fixture "out-b") $asi $provenance $generatedHeader $commit
    foreach ($name in @($a.Archive, $a.Checksum, $a.Identity)) {
        $peer = Join-Path (Join-Path $fixture "out-b") (Split-Path -Leaf $name)
        if ((Get-ReleaseFileSha256 $name) -ne (Get-ReleaseFileSha256 $peer)) {
            throw "Repeated release artifact differs: $(Split-Path -Leaf $name)"
        }
    }
    Add-Type -AssemblyName System.IO.Compression
    $archive = [System.IO.Compression.ZipFile]::OpenRead($a.Archive)
    try {
        $actualInventory = @($archive.Entries | ForEach-Object { $_.FullName })
        foreach ($entry in $archive.Entries) {
            if ($entry.LastWriteTime.DateTime -ne [datetime]::new(2000, 1, 1, 0, 0, 0) -or
                $entry.ExternalAttributes -ne 0) {
                throw "Release ZIP metadata is not fixed for $($entry.FullName): time=$($entry.LastWriteTime.UtcDateTime.ToString('o')) attrs=$($entry.ExternalAttributes)"
            }
        }
    } finally { $archive.Dispose() }
    $expectedInventory = @(
        "CHANGELOG.md", "End/Binaries/Win64/FF7RPianoSongs.asi",
        "End/Binaries/Win64/FF7RPianoSongs.ini", "LICENSE", "README.md",
        "THIRD_PARTY_NOTICES.md", "docs/SongFormat.md", "release.json"
    ) | Sort-Object
    if (($actualInventory -join "`n") -ne ($expectedInventory -join "`n")) {
        throw "Release archive order or exact minimal public inventory changed"
    }
    if (@($actualInventory | Where-Object {
        $_ -match '(^|/)(analysis|build|\.cache|examples|tools)(/|$)' -or
        $_ -match '\.(mid|midi|mp3|flac|wav|bin|pdb|dll)$'
    }).Count -ne 0) { throw "Release archive contains music, cache, analysis, build, or developer artifacts" }
    $identity = Get-Content -LiteralPath $a.Identity -Raw | ConvertFrom-Json
    if ($identity.product -ne "FF7RPianoSongs" -or $identity.platform -ne "win64" -or $identity.license -ne "MIT" -or
        $identity.archive.sha256 -ne (Get-ReleaseFileSha256 $a.Archive) -or
        $identity.asi_sha256 -ne (Get-ReleaseFileSha256 $asi) -or
        $identity.provenance_sha256 -ne (Get-ReleaseFileSha256 $provenance) -or
        $identity.release_authority_sha256 -ne (Get-ReleaseFileSha256 (Join-Path $fixture "release.json")) -or
        $identity.generated_release_header_sha256 -ne (Get-ReleaseFileSha256 $generatedHeader) -or
        $identity.source_commit -ne $commit) { throw "Release identity hashes or commit are incorrect" }
    $authority = Get-ReleaseAuthority $fixture
    $null = Assert-ReleaseIdentity $a.Identity $authority $a.Archive $asi $provenance `
        (Join-Path $fixture "release.json") $generatedHeader $commit
    $identityJson = Get-Content -LiteralPath $a.Identity -Raw
    $identityMutationPath = Join-Path $fixture "mutated.release.json"
    foreach ($mutation in @(
        "wrong-product", "wrong-platform", "wrong-license", "wrong-catalog", "wrong-version", "wrong-source",
        "uppercase-source", "short-source", "long-source", "malformed-source",
        "missing-archive-file", "missing-archive-hash", "extra-archive-field", "wrong-archive-file",
        "wrong-archive-hash", "wrong-asi-hash", "wrong-provenance-hash", "wrong-authority-hash", "wrong-header-hash",
        "uppercase-hash", "short-hash", "long-hash", "malformed-hash", "extra-top-level", "missing-top-level",
        "nonstring-schema", "nonstring-product", "nonstring-version", "nonstring-platform", "nonstring-license",
        "nonstring-catalog", "nonstring-source", "nonstring-archive-file"
    )) {
        $badIdentity = $identityJson | ConvertFrom-Json
        switch ($mutation) {
            "wrong-product" { $badIdentity.product = "OtherProduct" }
            "wrong-platform" { $badIdentity.platform = "linux-x64" }
            "wrong-license" { $badIdentity.license = "Apache-2.0" }
            "wrong-catalog" { $badIdentity.supported_executable_catalog_id = "other-catalog" }
            "wrong-version" { $badIdentity.version = "9.9.9" }
            "wrong-source" { $badIdentity.source_commit = (("1" * 40) -join "") }
            "uppercase-source" { $badIdentity.source_commit = (("A" * 40) -join "") }
            "short-source" { $badIdentity.source_commit = (("a" * 39) -join "") }
            "long-source" { $badIdentity.source_commit = (("a" * 41) -join "") }
            "malformed-source" { $badIdentity.source_commit = (("g" * 40) -join "") }
            "missing-archive-file" { $badIdentity.archive.PSObject.Properties.Remove("file") }
            "missing-archive-hash" { $badIdentity.archive.PSObject.Properties.Remove("sha256") }
            "extra-archive-field" { $badIdentity.archive | Add-Member extra "forbidden" }
            "wrong-archive-file" { $badIdentity.archive.file = "other.zip" }
            "wrong-archive-hash" { $badIdentity.archive.sha256 = (("0" * 64) -join "") }
            "wrong-asi-hash" { $badIdentity.asi_sha256 = (("0" * 64) -join "") }
            "wrong-provenance-hash" { $badIdentity.provenance_sha256 = (("0" * 64) -join "") }
            "wrong-authority-hash" { $badIdentity.release_authority_sha256 = (("0" * 64) -join "") }
            "wrong-header-hash" { $badIdentity.generated_release_header_sha256 = (("0" * 64) -join "") }
            "uppercase-hash" { $badIdentity.archive.sha256 = (("A" * 64) -join "") }
            "short-hash" { $badIdentity.asi_sha256 = (("a" * 63) -join "") }
            "long-hash" { $badIdentity.provenance_sha256 = (("a" * 65) -join "") }
            "malformed-hash" { $badIdentity.generated_release_header_sha256 = (("g" * 64) -join "") }
            "extra-top-level" { $badIdentity | Add-Member unexpected "forbidden" }
            "missing-top-level" { $badIdentity.PSObject.Properties.Remove("license") }
            "nonstring-schema" { $badIdentity.schema = $true }
            "nonstring-product" { $badIdentity.product = $true }
            "nonstring-version" { $badIdentity.version = $true }
            "nonstring-platform" { $badIdentity.platform = $true }
            "nonstring-license" { $badIdentity.license = $true }
            "nonstring-catalog" { $badIdentity.supported_executable_catalog_id = $true }
            "nonstring-source" { $badIdentity.source_commit = $true }
            "nonstring-archive-file" { $badIdentity.archive.file = $true }
        }
        [IO.File]::WriteAllText($identityMutationPath, ($badIdentity | ConvertTo-Json -Depth 5))
        $accepted = $false
        try {
            $null = Assert-ReleaseIdentity $identityMutationPath $authority $a.Archive $asi $provenance `
                (Join-Path $fixture "release.json") $generatedHeader $commit
            $accepted = $true
        } catch {}
        if ($accepted) { throw "Malformed release identity was accepted: $mutation" }
    }
    foreach ($badVersion in @("39", "v39", "01.0.0", "0.1", "0.1.0-beta")) {
        $bad = $releaseJson | ConvertFrom-Json
        $bad.version = $badVersion
        $bad.targets[0].archive_basename = "FF7RPianoSongs-$badVersion-win64-ff7r1.005"
        [IO.File]::WriteAllText((Join-Path $fixture "release.json"), ($bad | ConvertTo-Json -Depth 5))
        $accepted = $false
        try { $null = Get-ReleaseAuthority $fixture; $accepted = $true } catch {}
        if ($accepted) { throw "Malformed public version was accepted: $badVersion" }
    }
    foreach ($mutation in @(
        "extra-field", "wrong-license", "wrong-catalog", "wrong-archive",
        "nonstring-schema", "nonstring-product", "nonstring-version", "nonstring-platform",
        "nonstring-license", "nonstring-catalog", "nonstring-archive",
        "wrong-schema", "targets-scalar", "targets-empty", "extra-target-field",
        "missing-target-field", "nonstring-game-build", "malformed-game-build",
        "malformed-catalog", "duplicate-target", "undeclared-build")) {
        $bad = $releaseJson | ConvertFrom-Json
        switch ($mutation) {
            "extra-field" { $bad | Add-Member -NotePropertyName pipeline_version -NotePropertyValue 39 }
            "wrong-license" { $bad.license = "GPL-3.0-only" }
            "wrong-catalog" { $bad.targets[0].supported_executable_catalog_id = "other-build" }
            "wrong-archive" { $bad.targets[0].archive_basename = "FF7RPianoSongs-latest-win64" }
            "nonstring-schema" { $bad.schema = $true }
            "nonstring-product" { $bad.product = $true }
            "nonstring-version" { $bad.version = $true }
            "nonstring-platform" { $bad.platform = $true }
            "nonstring-license" { $bad.license = $true }
            "nonstring-catalog" { $bad.targets[0].supported_executable_catalog_id = $true }
            "nonstring-archive" { $bad.targets[0].archive_basename = $true }
            "wrong-schema" { $bad.schema = "ff7rpianosongs.release.v1" }
            "targets-scalar" { $bad.targets = "ff7rebirth-steam-win64-6a16ced2" }
            "targets-empty" { $bad.targets = @() }
            "extra-target-field" {
                $bad.targets[0] | Add-Member -NotePropertyName notes -NotePropertyValue "extra"
            }
            "missing-target-field" { $bad.targets[0].PSObject.Properties.Remove("game_build") }
            "nonstring-game-build" { $bad.targets[0].game_build = $true }
            "malformed-game-build" { $bad.targets[0].game_build = "1" }
            "malformed-catalog" { $bad.targets[0].supported_executable_catalog_id = "FF7Rebirth-Steam" }
            "duplicate-target" {
                $bad.targets = @($bad.targets[0], ($releaseJson | ConvertFrom-Json).targets[0])
            }
            "undeclared-build" {
                $bad.targets[0].supported_executable_catalog_id = "ff7rebirth-steam-win64-68fd6fde"
            }
        }
        [IO.File]::WriteAllText((Join-Path $fixture "release.json"), ($bad | ConvertTo-Json -Depth 5))
        $accepted = $false
        try { $null = Get-ReleaseAuthority $fixture; $accepted = $true } catch {}
        if ($accepted) { throw "Malformed release metadata was accepted: $mutation" }
    }
    [IO.File]::WriteAllText((Join-Path $fixture "release.json"), $releaseJson)
    $selected = Get-ReleaseAuthority $fixture "ff7rebirth-steam-win64-6a16ced2"
    if ($selected.game_build -cne "1.005" -or
        $selected.archive_basename -cne "FF7RPianoSongs-0.1.0-win64-ff7r1.005") {
        throw "Named release target selection returned the wrong authority"
    }
    $accepted = $false
    try { $null = Get-ReleaseAuthority $fixture "ff7rebirth-steam-win64-68fd6fde"; $accepted = $true } catch {}
    if ($accepted) { throw "Release authority for an undeclared executable catalog ID was accepted" }
    Write-Output "Release package self-test passed: exact minimal deterministic ZIP/checksum/identity and malformed metadata."
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
