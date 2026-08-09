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

function Get-ReleaseDocument {
    param([string]$Root)
    $path = Join-Path $Root "release.json"
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "release.json is missing" }
    try { $release = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
    catch { throw "release.json is malformed: $($_.Exception.Message)" }
    Assert-ReleaseExactProperties $release @(
        "schema", "product", "version", "platform", "license", "targets") "Release authority"
    foreach ($field in @("schema", "product", "version", "platform", "license")) {
        $release.$field = Assert-ReleaseString $release.$field "Release authority $field"
    }
    if ($release.schema -cne "ff7rpianosongs.release.v2") { throw "Unsupported release schema" }
    if ($release.product -cne "FF7RPianoSongs") { throw "Release product must be FF7RPianoSongs" }
    if ($release.version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Release version must be a canonical three-part semantic version"
    }
    if ($release.platform -cne "win64" -or $release.license -cne "MIT") {
        throw "Release platform/license must be win64/MIT"
    }
    if ($release.targets -isnot [System.Object[]]) { throw "Release targets must be an array" }
    $targets = @($release.targets)
    if ($targets.Count -eq 0) { throw "Release targets must declare at least one game build" }
    $seenBuilds = @{}
    $seenCatalogIds = @{}
    $seenBasenames = @{}
    foreach ($target in $targets) {
        Assert-ReleaseExactProperties $target @(
            "game_build", "supported_executable_catalog_id", "archive_basename") "Release target"
        foreach ($field in @("game_build", "supported_executable_catalog_id", "archive_basename")) {
            $target.$field = Assert-ReleaseString $target.$field "Release target $field"
        }
        if ($target.game_build -notmatch '^(0|[1-9][0-9]*)\.[0-9]+$') {
            throw "Release target game build must be a canonical two-part game version"
        }
        if ($target.supported_executable_catalog_id -notmatch '^[a-z0-9][a-z0-9-]+$') {
            throw "Release supported executable catalog ID is malformed"
        }
        $expectedBasename = "$($release.product)-$($release.version)-$($release.platform)-ff7r$($target.game_build)"
        if ($target.archive_basename -cne $expectedBasename) {
            throw "Release archive basename must derive from product, version, platform, and game build"
        }
        foreach ($unique in @(
            [pscustomobject]@{ Seen = $seenBuilds; Key = $target.game_build; Description = "game build" },
            [pscustomobject]@{ Seen = $seenCatalogIds; Key = $target.supported_executable_catalog_id; Description = "executable catalog ID" },
            [pscustomobject]@{ Seen = $seenBasenames; Key = $target.archive_basename; Description = "archive basename" })) {
            if ($unique.Seen.ContainsKey($unique.Key)) {
                throw "Duplicate release target $($unique.Description): $($unique.Key)"
            }
            $unique.Seen[$unique.Key] = $true
        }
    }
    $catalogPath = Join-Path $Root "src/game/rva_catalog.json"
    if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
        $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
        $declared = @(@($catalog.builds) | ForEach-Object { $_.id })
        foreach ($target in $targets) {
            if ($declared -cnotcontains $target.supported_executable_catalog_id) {
                throw "release.json target names a build rva_catalog.json does not declare: $($target.supported_executable_catalog_id)"
            }
        }
    }
    return $release
}

function Get-ReleaseAuthority {
    param([string]$Root, [string]$CatalogId = "")
    $release = Get-ReleaseDocument $Root
    $targets = @($release.targets)
    $selected = if ([string]::IsNullOrEmpty($CatalogId)) {
        if ($targets.Count -ne 1) {
            throw "release.json declares $($targets.Count) targets; the packaged executable catalog ID must be named"
        }
        $targets[0]
    }
    else {
        $matched = @($targets | Where-Object { $_.supported_executable_catalog_id -ceq $CatalogId })
        if ($matched.Count -ne 1) {
            throw "release.json does not declare exactly one target for executable catalog ID: $CatalogId"
        }
        $matched[0]
    }
    return [pscustomobject][ordered]@{
        schema = $release.schema
        product = $release.product
        version = $release.version
        platform = $release.platform
        license = $release.license
        game_build = $selected.game_build
        supported_executable_catalog_id = $selected.supported_executable_catalog_id
        archive_basename = $selected.archive_basename
    }
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

function Get-ReleaseTextSha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($Text)))).Replace("-", "").ToLowerInvariant()
    } finally { $sha.Dispose() }
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
        [string]$SourceCommit, [string]$ExpectedGeneratedReleaseHeaderSha256 = ""
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
    $generatedHeaderHash = if ([string]::IsNullOrEmpty($ExpectedGeneratedReleaseHeaderSha256)) {
        Get-ReleaseFileSha256 $GeneratedReleaseHeaderPath
    } else {
        Assert-ReleaseLowercaseSha256 $ExpectedGeneratedReleaseHeaderSha256 "Expected generated release header sha256"
        $ExpectedGeneratedReleaseHeaderSha256
    }
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
        $identity.generated_release_header_sha256 -cne $generatedHeaderHash) {
        throw "Release identity does not exactly bind the release authority, package, provenance, archive, and source commit"
    }
    return $identity
}

function Assert-PublishedBuildProvenance {
    param(
        [string]$Root, [string]$ProvenancePath, [string]$AsiPath,
        [object]$ReleaseAuthority, [string[]]$ExpectedProductionInputs
    )
    try { $record = Get-Content -LiteralPath $ProvenancePath -Raw | ConvertFrom-Json }
    catch { throw "Published build provenance JSON is malformed: $($_.Exception.Message)" }
    Assert-ReleaseExactProperties $record @(
        "schema", "configuration", "dll", "toolchain", "cmake", "releaseIdentity",
        "productionInputs", "productionInputSetSha256") "Published build provenance"
    if ($record.schema -cne "ff7rpianosongs.build-provenance.v3" -or
        $record.configuration -cne "Release") {
        throw "Published build provenance must be the Release v3 schema"
    }
    Assert-ReleaseExactProperties $record.dll @("path", "sha256") "Published build provenance DLL"
    Assert-ReleaseLowercaseSha256 $record.dll.sha256 "Published build provenance DLL sha256"
    if ($record.dll.path -cne "bin/Release/FF7RPianoSongs.dll" -or
        $record.dll.sha256 -cne (Get-ReleaseFileSha256 $AsiPath)) {
        throw "Published ASI does not match its build provenance"
    }
    Assert-ReleaseExactProperties $record.toolchain @(
        "visualStudioInstallationPath", "compilerPath", "compilerVersion", "compilerSha256",
        "cmakePath", "cmakeVersion", "cmakeSha256", "generator", "generatorPlatform",
        "generatorToolset") "Published build provenance toolchain"
    foreach ($field in $record.toolchain.PSObject.Properties.Name) {
        if ($field -notin @("compilerSha256", "cmakeSha256")) {
            $record.toolchain.$field = Assert-ReleaseString $record.toolchain.$field `
                "Published build provenance toolchain $field"
        }
    }
    Assert-ReleaseLowercaseSha256 $record.toolchain.compilerSha256 "Published compiler sha256"
    Assert-ReleaseLowercaseSha256 $record.toolchain.cmakeSha256 "Published CMake sha256"
    Assert-ReleaseExactProperties $record.cmake @("cachePath", "cacheSha256") "Published build provenance CMake identity"
    $record.cmake.cachePath = Assert-ReleaseString $record.cmake.cachePath "Published CMake cache path"
    if ([string]::IsNullOrWhiteSpace($record.cmake.cachePath) -or
        [IO.Path]::IsPathRooted($record.cmake.cachePath) -or $record.cmake.cachePath.Contains('\') -or
        $record.cmake.cachePath.StartsWith('/') -or $record.cmake.cachePath.EndsWith('/') -or
        @($record.cmake.cachePath.Split('/')) -contains '..' -or
        @($record.cmake.cachePath.Split('/')) -contains '.') {
        throw "Published CMake cache path is not normalized"
    }
    Assert-ReleaseLowercaseSha256 $record.cmake.cacheSha256 "Published CMake cache sha256"
    Assert-ReleaseExactProperties $record.releaseIdentity @(
        "authorityPath", "authoritySha256", "catalogId", "generatedHeaderPath",
        "generatedHeaderSha256") "Published build provenance release identity"
    foreach ($field in @("authoritySha256", "generatedHeaderSha256")) {
        Assert-ReleaseLowercaseSha256 $record.releaseIdentity.$field "Published release identity $field"
    }
    foreach ($field in @("authorityPath", "catalogId", "generatedHeaderPath")) {
        $record.releaseIdentity.$field = Assert-ReleaseString $record.releaseIdentity.$field `
            "Published release identity $field"
    }
    if ($record.releaseIdentity.authorityPath -cne "release.json" -or
        $record.releaseIdentity.generatedHeaderPath -cne "generated/release_identity.generated.h" -or
        $record.releaseIdentity.catalogId -cne $ReleaseAuthority.supported_executable_catalog_id -or
        $record.releaseIdentity.authoritySha256 -cne (Get-ReleaseFileSha256 (Join-Path $Root "release.json"))) {
        throw "Published build provenance does not match the current release authority and target"
    }
    Assert-ReleaseLowercaseSha256 $record.productionInputSetSha256 "Published production input set sha256"
    $actualInputs = @($record.productionInputs)
    if ($actualInputs.Count -ne $ExpectedProductionInputs.Count) {
        throw "Published production input inventory does not match current source authority"
    }
    $seen = @{}
    $identity = ""
    foreach ($input in $actualInputs) {
        Assert-ReleaseExactProperties $input @("path", "sha256") "Published production input"
        if ($input.path -isnot [string] -or [string]::IsNullOrWhiteSpace($input.path) -or
            [IO.Path]::IsPathRooted($input.path) -or $input.path.Contains('\') -or
            $input.path.StartsWith('/') -or $input.path.EndsWith('/') -or
            @($input.path.Split('/')) -contains '..' -or @($input.path.Split('/')) -contains '.' -or
            $seen.ContainsKey($input.path)) {
            throw "Published production input path is malformed or duplicated: '$($input.path)'"
        }
        Assert-ReleaseLowercaseSha256 $input.sha256 "Published production input sha256"
        $seen[$input.path] = $true
        $source = Join-Path $Root $input.path
        if (!(Test-Path -LiteralPath $source -PathType Leaf) -or
            $input.sha256 -cne (Get-ReleaseFileSha256 $source)) {
            throw "Published production input does not match current source: $($input.path)"
        }
        $identity += "$($input.path)`t$($input.sha256)`n"
    }
    if (@(Compare-Object -ReferenceObject @($ExpectedProductionInputs | Sort-Object) `
            -DifferenceObject @($actualInputs.path | Sort-Object)).Count -ne 0 -or
        $record.productionInputSetSha256 -cne (Get-ReleaseTextSha256 $identity)) {
        throw "Published production input set identity does not match current source authority"
    }
    return $record
}

function Copy-ValidatedReleaseSiblings {
    param(
        [string]$ExistingSurface, [string]$StagingSurface,
        [string]$SelectedBasename, [string[]]$DeclaredBasenames,
        [scriptblock]$ValidateTarget
    )
    if (!(Test-Path -LiteralPath $ExistingSurface)) { return }
    if (!(Test-Path -LiteralPath $ExistingSurface -PathType Container)) {
        throw "Existing release surface is not a directory"
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $ExistingSurface -Force | Sort-Object Name)) {
        if (!$entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $DeclaredBasenames -cnotcontains $entry.Name) {
            throw "Existing release surface contains a stale, foreign, or unsafe entry: $($entry.Name)"
        }
        if ($entry.Name -ceq $SelectedBasename) { continue }
        $null = & $ValidateTarget $entry.FullName $entry.Name
        $destination = Join-Path $StagingSurface $entry.Name
        Copy-Item -LiteralPath $entry.FullName -Destination $destination -Recurse
        $null = & $ValidateTarget $destination $entry.Name
    }
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
        [string]$SourceCommit,
        [string]$CatalogId = ""
    )
    $release = Get-ReleaseAuthority $Root $CatalogId
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
    $publishedProvenance = Join-Path $OutputDirectory "$($release.archive_basename).provenance.json"
    Copy-Item -LiteralPath $ProvenancePath -Destination $publishedProvenance
    if ((Get-ReleaseFileSha256 $publishedProvenance) -cne (Get-ReleaseFileSha256 $ProvenancePath)) {
        throw "Published provenance sidecar does not match the build provenance"
    }
    return [pscustomobject]@{
        Archive = $archivePath
        Checksum = $checksumPath
        Identity = $identityPath
        Provenance = $publishedProvenance
    }
}
