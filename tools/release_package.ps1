Set-StrictMode -Version Latest

function Assert-ReleaseExactProperties {
    param([object]$Value, [string[]]$Expected, [string]$Description)
    $actual = @($Value.PSObject.Properties.Name | Sort-Object)
    $expectedSorted = @($Expected | Sort-Object)
    if ($actual.Count -ne $expectedSorted.Count -or
        @(Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actual).Count -ne 0) {
        throw "$Description fields must be exactly: $($Expected -join ', ')"
    }
}

function Assert-ReleaseString {
    param([object]$Value, [string]$Description)
    if ($Value -isnot [string]) { throw "$Description must be a string" }
    return [string]$Value
}

function Get-ReleaseAuthority {
    param([string]$Root)
    $path = Join-Path $Root "release.json"
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "release.json is missing" }
    try { $release = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
    catch { throw "release.json is malformed: $($_.Exception.Message)" }
    Assert-ReleaseExactProperties $release @(
        "schema", "product", "version", "platform", "license",
        "supported_executable_catalog_id", "archive_basename") "Release authority"
    foreach ($field in @("schema", "product", "version", "platform", "license", "supported_executable_catalog_id", "archive_basename")) {
        $release.$field = Assert-ReleaseString $release.$field "Release authority $field"
    }
    if ($release.schema -cne "ff7rpianosongs.release.v1") { throw "Unsupported release schema" }
    if ($release.product -cne "FF7RPianoSongs") { throw "Release product must be FF7RPianoSongs" }
    if ($release.version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Release version must be a canonical three-part semantic version"
    }
    if ($release.platform -cne "win64" -or $release.license -cne "MIT") {
        throw "Release platform/license must be win64/MIT"
    }
    if ($release.supported_executable_catalog_id -notmatch '^[a-z0-9][a-z0-9-]+$') {
        throw "Release supported executable catalog ID is malformed"
    }
    $expectedBasename = "$($release.product)-$($release.version)-$($release.platform)"
    if ($release.archive_basename -cne $expectedBasename) {
        throw "Release archive basename must derive from product, version, and platform"
    }
    $catalogPath = Join-Path $Root "src/game/rva_catalog.json"
    if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
        $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
        if ($catalog.build.id -isnot [string] -or $catalog.build.id -cne $release.supported_executable_catalog_id) {
            throw "release.json supported executable catalog ID does not match rva_catalog.json"
        }
    }
    return $release
}

function Get-CleanSourceCommit {
    param([string]$Root)
    $inside = (& git -C $Root rev-parse --is-inside-work-tree 2>$null)
    if ($LASTEXITCODE -ne 0 -or $inside -ne "true") { throw "Release packaging requires a Git worktree" }
    $dirty = @(& git -C $Root status --porcelain=v1 --untracked-files=all -- .)
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect release source worktree" }
    if ($dirty.Count -ne 0) { throw "Release packaging requires a clean product source checkpoint" }
    $commit = (& git -C $Root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Could not resolve the release source commit"
    }
    return $commit.ToLowerInvariant()
}

function Get-ReleaseFileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-ReleaseLowercaseSha256([object]$Value, [string]$Description) {
    if ($Value -isnot [string] -or $Value -notmatch '^[0-9a-f]{64}$') {
        throw "$Description must be exactly 64 lowercase hexadecimal characters"
    }
}

function Assert-ReleaseIdentity {
    param(
        [string]$IdentityPath, [object]$ReleaseAuthority,
        [string]$ArchivePath, [string]$AsiPath, [string]$ProvenancePath,
        [string]$ReleaseAuthorityPath, [string]$GeneratedReleaseHeaderPath,
        [string]$SourceCommit
    )
    try { $identity = Get-Content -LiteralPath $IdentityPath -Raw | ConvertFrom-Json }
    catch { throw "Release identity JSON is malformed: $($_.Exception.Message)" }
    Assert-ReleaseExactProperties $identity @(
        "schema", "product", "version", "platform", "license",
        "supported_executable_catalog_id", "source_commit", "archive",
        "asi_sha256", "provenance_sha256", "release_authority_sha256",
        "generated_release_header_sha256") "Release identity"
    if ($null -eq $identity.archive) { throw "Release identity archive object is missing" }
    Assert-ReleaseExactProperties $identity.archive @("file", "sha256") "Release identity archive"
    foreach ($field in @("schema", "product", "version", "platform", "license", "supported_executable_catalog_id", "source_commit")) {
        $identity.$field = Assert-ReleaseString $identity.$field "Release identity $field"
    }
    $identity.archive.file = Assert-ReleaseString $identity.archive.file "Release identity archive file"
    if ($SourceCommit -notmatch '^[0-9a-f]{40}$') { throw "Expected source commit is malformed" }
    if ($identity.source_commit -isnot [string] -or $identity.source_commit -notmatch '^[0-9a-f]{40}$') {
        throw "Release identity source commit must be exactly 40 lowercase hexadecimal characters"
    }
    foreach ($hashField in @("asi_sha256", "provenance_sha256", "release_authority_sha256", "generated_release_header_sha256")) {
        Assert-ReleaseLowercaseSha256 $identity.$hashField "Release identity $hashField"
    }
    Assert-ReleaseLowercaseSha256 $identity.archive.sha256 "Release identity archive sha256"
    $archiveName = Split-Path -Leaf $ArchivePath
    if ($identity.schema -cne "ff7rpianosongs.release-identity.v2" -or
        $identity.product -cne $ReleaseAuthority.product -or
        $identity.version -cne $ReleaseAuthority.version -or
        $identity.platform -cne $ReleaseAuthority.platform -or
        $identity.license -cne $ReleaseAuthority.license -or
        $identity.supported_executable_catalog_id -cne $ReleaseAuthority.supported_executable_catalog_id -or
        $identity.source_commit -cne $SourceCommit -or
        $identity.archive.file -cne $archiveName -or
        $identity.archive.file -cne "$($ReleaseAuthority.archive_basename).zip" -or
        $identity.archive.sha256 -cne (Get-ReleaseFileSha256 $ArchivePath) -or
        $identity.asi_sha256 -cne (Get-ReleaseFileSha256 $AsiPath) -or
        $identity.provenance_sha256 -cne (Get-ReleaseFileSha256 $ProvenancePath) -or
        $identity.release_authority_sha256 -cne (Get-ReleaseFileSha256 $ReleaseAuthorityPath) -or
        $identity.generated_release_header_sha256 -cne (Get-ReleaseFileSha256 $GeneratedReleaseHeaderPath)) {
        throw "Release identity does not exactly bind the release authority, package, provenance, archive, and source commit"
    }
    return $identity
}

function New-DeterministicZip {
    param([string]$SourceDirectory, [string]$Destination)
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    $files = @(Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse -Force |
        ForEach-Object {
            [pscustomobject]@{
                File = $_
                Relative = [System.IO.Path]::GetRelativePath($SourceDirectory, $_.FullName).Replace('\', '/')
            }
        } | Sort-Object Relative)
    $stream = [System.IO.File]::Open($Destination, [System.IO.FileMode]::CreateNew)
    try {
        $zip = [System.IO.Compression.ZipArchive]::new(
            $stream, [System.IO.Compression.ZipArchiveMode]::Create, $false,
            [System.Text.Encoding]::UTF8)
        try {
            foreach ($item in $files) {
                $entry = $zip.CreateEntry($item.Relative, [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $entry.ExternalAttributes = 0
                $input = [System.IO.File]::OpenRead($item.File.FullName)
                $output = $entry.Open()
                try { $input.CopyTo($output) }
                finally { $output.Dispose(); $input.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}

function New-ReleaseArtifacts {
    param(
        [string]$Root,
        [string]$PackageRoot,
        [string]$OutputDirectory,
        [string]$AsiPath,
        [string]$ProvenancePath,
        [string]$GeneratedReleaseHeaderPath,
        [string]$SourceCommit
    )
    $release = Get-ReleaseAuthority $Root
    if ($SourceCommit -notmatch '^[0-9a-f]{40}$') { throw "Release source commit must be a lowercase full Git SHA" }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $archiveName = "$($release.archive_basename).zip"
    $archivePath = Join-Path $OutputDirectory $archiveName
    New-DeterministicZip -SourceDirectory $PackageRoot -Destination $archivePath
    $archiveHash = Get-ReleaseFileSha256 $archivePath
    $checksumPath = "$archivePath.sha256"
    [System.IO.File]::WriteAllText($checksumPath, "$archiveHash  $archiveName`n", [System.Text.UTF8Encoding]::new($false))
    $identity = [ordered]@{
        schema = "ff7rpianosongs.release-identity.v2"
        product = $release.product
        version = $release.version
        platform = $release.platform
        license = $release.license
        supported_executable_catalog_id = $release.supported_executable_catalog_id
        source_commit = $SourceCommit
        archive = [ordered]@{ file = $archiveName; sha256 = $archiveHash }
        asi_sha256 = Get-ReleaseFileSha256 $AsiPath
        provenance_sha256 = Get-ReleaseFileSha256 $ProvenancePath
        release_authority_sha256 = Get-ReleaseFileSha256 (Join-Path $Root "release.json")
        generated_release_header_sha256 = Get-ReleaseFileSha256 $GeneratedReleaseHeaderPath
    }
    $identityPath = Join-Path $OutputDirectory "$($release.archive_basename).release.json"
    [System.IO.File]::WriteAllText(
        $identityPath, (($identity | ConvertTo-Json -Depth 5) + "`n"),
        [System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{
        Archive = $archivePath
        Checksum = $checksumPath
        Identity = $identityPath
    }
}
