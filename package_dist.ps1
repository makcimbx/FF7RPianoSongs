param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateRange(1, 64)]
    [int]$Parallel = [Environment]::ProcessorCount,

    [switch]$SkipBuild,

    [string]$GameBuild = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$PackagePath = Join-Path $Root "package"
$BinaryRelative = "End/Binaries/Win64"
$DocumentManifestPath = Join-Path $Root "package-docs.json"
$RequiredDocumentRoles = [ordered]@{
    "entrypoint" = "package"
    "first-party-license" = "package"
    "third-party-notices" = "package"
    "song-format" = "package"
    "themes-guide" = "package"
    "changelog" = "package"
    "in-game-validation" = "repository"
    "release-status" = "repository"
    "docs-index" = "repository"
    "architecture" = "repository"
    "build-release" = "repository"
    "hca-criteria" = "repository"
    "midi-methodology" = "repository"
    "chart-limits" = "repository"
    "analysis-index" = "repository"
    "audio-lifecycle-evidence" = "repository"
    "midi-calibration-evidence" = "repository"
    "chart-row-limit-evidence" = "repository"
    "cache-optimization-evidence" = "repository"
    "adaptive-mabf-evidence" = "repository"
    "chart-event-abi-evidence" = "repository"
    "developer-tools" = "repository"
}

. (Join-Path $Root "tools/release_package.ps1")
. (Join-Path $Root "tools/two_surface_publication.ps1")
$ReleaseTargets = @((Get-ReleaseDocument $Root).targets)
$SelectedTarget = if ($GameBuild) {
    $matchedTargets = @($ReleaseTargets | Where-Object { $_.game_build -ceq $GameBuild })
    if ($matchedTargets.Count -ne 1) {
        throw "release.json does not declare exactly one target for game build '$GameBuild'"
    }
    $matchedTargets[0]
} elseif ($ReleaseTargets.Count -eq 1) {
    $ReleaseTargets[0]
} else {
    throw "-GameBuild is required because release.json declares more than one game build"
}
$ReleaseCatalogId = [string]$SelectedTarget.supported_executable_catalog_id
$ReleaseAuthority = Get-ReleaseAuthority $Root $ReleaseCatalogId

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

$DocumentManifest = Get-Content -LiteralPath $DocumentManifestPath -Raw | ConvertFrom-Json
Assert-ExactProperties -Value $DocumentManifest -Expected @("schema", "documents") `
    -Description "Documentation registry"
if ($DocumentManifest.schema -ne "ff7rpianosongs.package-docs.v2") {
    throw "Unsupported documentation registry schema: $($DocumentManifest.schema)"
}
$CanonicalDocuments = @($DocumentManifest.documents)
if ($CanonicalDocuments.Count -ne $RequiredDocumentRoles.Count) {
    throw "Documentation registry must contain the complete closed role set"
}
$documentSources = @{}
$documentDestinations = @{}
foreach ($document in $CanonicalDocuments) {
    $expectedFields = if ($document.distribution -eq "package") {
        @("role", "distribution", "source", "destination")
    } else {
        @("role", "distribution", "source")
    }
    Assert-ExactProperties -Value $document -Expected $expectedFields -Description "Documentation entry"
    if (!$RequiredDocumentRoles.Contains($document.role)) {
        throw "Unknown documentation role: '$($document.role)'"
    }
    if ($document.distribution -ne $RequiredDocumentRoles[$document.role]) {
        throw "Invalid distribution '$($document.distribution)' for documentation role '$($document.role)'"
    }
    if ($documentSources.ContainsKey($document.source)) {
        throw "Duplicate canonical document source: '$($document.source)'"
    }
    Assert-NormalizedRelativePath -Path $document.source -Description "Canonical document source"
    if ([System.IO.Path]::GetFileName($document.source) -ne "LICENSE" -and
        [System.IO.Path]::GetExtension($document.source) -notin @(".md", ".txt")) {
        throw "Canonical document source must be Markdown or text: '$($document.source)'"
    }
    $documentSources[$document.source] = $document.role
    if ($document.distribution -eq "package") {
        Assert-NormalizedRelativePath -Path $document.destination -Description "Package document destination"
        if ([System.IO.Path]::GetFileName($document.destination) -ne "LICENSE" -and
            [System.IO.Path]::GetExtension($document.destination) -notin @(".md", ".txt")) {
            throw "Package document destination must be Markdown or text: '$($document.destination)'"
        }
        if ($documentDestinations.ContainsKey($document.destination)) {
            throw "Duplicate package document destination: '$($document.destination)'"
        }
        $documentDestinations[$document.destination] = $document.role
    }
}
foreach ($required in $RequiredDocumentRoles.Keys) {
    if ($required -notin @($CanonicalDocuments.role)) {
        throw "Missing mandatory documentation role: '$required'"
    }
}
foreach ($document in $CanonicalDocuments) {
    $sourcePath = Join-Path $Root $document.source
    if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Registered canonical document is missing: $($document.source)"
    }
}
$DocumentMappings = @($CanonicalDocuments | Where-Object { $_.distribution -eq "package" })

function Get-RelativeFileInventory {
    param([string]$Directory)
    $prefixLength = ([System.IO.Path]::GetFullPath($Directory).TrimEnd('\')).Length + 1
    return @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Force |
        ForEach-Object { $_.FullName.Substring($prefixLength).Replace('\', '/') } |
        Sort-Object)
}

function Assert-FilesEqual {
    param([string]$Source, [string]$Destination)
    $sourceItem = Get-Item -LiteralPath $Source
    $destinationItem = Get-Item -LiteralPath $Destination
    if ($sourceItem.Length -ne $destinationItem.Length) {
        throw "Byte validation failed (length): $Destination"
    }
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Byte validation failed (SHA-256): $Destination"
    }
}

function Assert-DirectoryTreesEqual {
    param([string]$Expected, [string]$Actual)
    $expectedFiles = Get-RelativeFileInventory -Directory $Expected
    $actualFiles = Get-RelativeFileInventory -Directory $Actual
    $difference = @(Compare-Object -ReferenceObject $expectedFiles -DifferenceObject $actualFiles)
    if ($difference.Count -ne 0) {
        throw "Directory inventory differs: '$Expected' versus '$Actual'"
    }
    foreach ($relative in $expectedFiles) {
        Assert-FilesEqual -Source (Join-Path $Expected $relative) -Destination (Join-Path $Actual $relative)
    }
}

function Copy-ValidatedFile {
    param([string]$Source, [string]$Destination)
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required source file is missing: $Source"
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
    Assert-FilesEqual -Source $Source -Destination $Destination
}

Assert-TwoSurfacePublicationSelfTests
$releaseSelftest = Join-Path $Root "tools/release_package_selftest.ps1"
& (Get-Command pwsh -CommandType Application -ErrorAction Stop).Source -NoProfile -File $releaseSelftest
if ($LASTEXITCODE -ne 0) { throw "Release package/identity self-test failed with exit code $LASTEXITCODE" }

# This gate precedes build/package mutation. It deliberately scopes status to
# this product so unrelated parent-repository work does not contaminate the
# standalone release identity.
$SourceCommit = Get-CleanSourceCommit $Root

if ($SkipBuild) {
    & (Join-Path $Root "build.ps1") -Configuration $Configuration `
        -Target "release_audit_tool" -Parallel $Parallel -CatalogBuildId $ReleaseCatalogId
} else {
    & (Join-Path $Root "build.ps1") -Configuration $Configuration `
        -Target @("FF7RPianoSongsProxy", "release_audit_tool") -Parallel $Parallel -CatalogBuildId $ReleaseCatalogId
}
if ($LASTEXITCODE -ne 0) {
    throw "Build script failed with exit code $LASTEXITCODE"
}

$Artifact = Join-Path $Root "build/proxy/$Configuration/FF7RPianoSongsProxy.dll"
$ProvenancePath = Join-Path $Root "build/proxy/$Configuration/FF7RPianoSongsProxy.provenance.json"
$Ini = Join-Path $Root "FF7RPianoSongs.example.ini"
$Model = Join-Path $Root "build/models/muscriptor-small-f16.gguf"
$AuditCandidates = @(
    (Join-Path $Root "build/$Configuration/release_audit_tool.exe"),
    (Join-Path $Root "build/bin/$Configuration/release_audit_tool.exe"),
    (Join-Path $Root "build/release_audit_tool.exe")
)
$AuditTool = $AuditCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (!$AuditTool) {
    throw "release_audit_tool.exe was not produced by the build"
}

. (Join-Path $Root "tools/build_provenance.ps1")

function Assert-BuildProvenanceAdversePathSelfTests {
    $testRoot = Join-Path ([IO.Path]::GetTempPath()) "ff7rp-provenance-test-$([guid]::NewGuid().ToString('N'))"
    try {
        foreach ($directory in @("src", "cmake", "generated")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $testRoot $directory) | Out-Null
        }
        foreach ($item in @{
            "CMakeLists.txt" = "cmake"; "build.ps1" = "build"; "release.json" = "release";
            "cmake/release_identity.generated.h.in" = "template"; "src/source.cpp" = "source";
            "dll.bin" = "dll"; "compiler.exe" = "compiler"; "CMakeCache.txt" = "cache";
            "generated/release_identity.generated.h" = "generated"
        }.GetEnumerator()) {
            [System.IO.File]::WriteAllText((Join-Path $testRoot $item.Key), $item.Value)
        }
        $inputs = @("CMakeLists.txt", "build.ps1", "cmake/release_identity.generated.h.in", "release.json", "src/source.cpp")
        $inputRecords = @($inputs | ForEach-Object {
            [ordered]@{ path = $_; sha256 = Get-FileSha256 (Join-Path $testRoot $_) }
        })
        $identity = ($inputRecords | ForEach-Object { "$($_.path)`t$($_.sha256)`n" }) -join ""
        $compiler = Join-Path $testRoot "compiler.exe"
        $cmake = (Get-Command pwsh -ErrorAction Stop).Source
        $dll = Join-Path $testRoot "dll.bin"
        $recordPath = Join-Path $testRoot "provenance.json"
        $record = [ordered]@{
            schema = "ff7rpianosongs.build-provenance.v3"; configuration = "Release"
            dll = [ordered]@{ path = "bin/Release/FF7RPianoSongs.dll"; sha256 = Get-FileSha256 $dll }
            toolchain = [ordered]@{
                visualStudioInstallationPath = "fixture"; compilerPath = $compiler
                compilerVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
                compilerSha256 = Get-FileSha256 $compiler; cmakePath = $cmake
                cmakeVersion = (& $cmake --version | Select-Object -First 1); cmakeSha256 = Get-FileSha256 $cmake
                generator = "fixture"; generatorPlatform = "x64"; generatorToolset = "fixture"
            }
            cmake = [ordered]@{
                cachePath = "CMakeCache.txt"; cacheSha256 = Get-FileSha256 (Join-Path $testRoot "CMakeCache.txt")
            }
            releaseIdentity = [ordered]@{
                authorityPath = "release.json"; authoritySha256 = Get-FileSha256 (Join-Path $testRoot "release.json")
                catalogId = "fixture-build"
                generatedHeaderPath = "generated/release_identity.generated.h"
                generatedHeaderSha256 = Get-FileSha256 (Join-Path $testRoot "generated/release_identity.generated.h")
            }
            productionInputs = $inputRecords; productionInputSetSha256 = Get-TextSha256 $identity
        }
        $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath
        Assert-BuildProvenance $testRoot $dll $recordPath "Release" $inputs $testRoot

        foreach ($case in @(
            [pscustomobject]@{ Name = "source"; Path = "src/source.cpp" },
            [pscustomobject]@{ Name = "CMake"; Path = "CMakeLists.txt" },
            [pscustomobject]@{ Name = "compiler"; Path = "compiler.exe" },
            [pscustomobject]@{ Name = "DLL"; Path = "dll.bin" })) {
            $path = Join-Path $testRoot $case.Path
            [System.IO.File]::AppendAllText($path, "drift")
            $accepted = $false
            try {
                Assert-BuildProvenance $testRoot $dll $recordPath "Release" $inputs $testRoot
                $accepted = $true
            } catch {}
            [System.IO.File]::WriteAllText($path, @{
                "src/source.cpp" = "source"; "CMakeLists.txt" = "cmake";
                "compiler.exe" = "compiler"; "dll.bin" = "dll"
            }[$case.Path])
            if ($accepted) { throw "$($case.Name) drift was accepted" }
        }
        $bad = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
        $bad.productionInputSetSha256 = "0" * 64
        $bad | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath
        $accepted = $false
        try {
            Assert-BuildProvenance $testRoot $dll $recordPath "Release" $inputs $testRoot
            $accepted = $true
        } catch {}
        if ($accepted) { throw "Provenance identity drift was accepted" }
        Write-Output "Build provenance adverse-path self-tests passed: source, CMake, compiler, DLL, record"
    } finally {
        if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    }
}

Assert-BuildProvenance $Root $Artifact $ProvenancePath $Configuration `
    (Get-ProductionInputRelativePaths $Root "FF7RPianoSongsProxy") (Join-Path $Root "build") "FF7RPianoSongsProxy"
$BuiltCatalogId = [string](Get-Content -LiteralPath $ProvenancePath -Raw |
    ConvertFrom-Json).releaseIdentity.catalogId
if ($BuiltCatalogId -cne $ReleaseCatalogId) {
    throw "The built artifact targets game build '$BuiltCatalogId', but '$ReleaseCatalogId' is being packaged"
}
Assert-BuildProvenanceAdversePathSelfTests

function Get-ExampleThemeMappings {
    $themes = [ordered]@{
        "steins-gate" = @("artwork.png", "button-disabled.png", "button-hovered.png", "button-normal.png", "button-pressed.png", "frame.png", "header.png", "info-frame.png", "line-record.png", "line.png", "new.png", "panel.png", "pattern.png", "record-deco.png", "row-selected.png", "row.png", "speed-configured.png", "speed-empty.png", "speed-recorded.png", "theme.ini")
        "evangelion" = @("artwork.png", "button-disabled.png", "button-hovered.png", "button-normal.png", "button-pressed.png", "frame.png", "header-rank.png", "header-title.png", "info-frame.png", "line-record.png", "line.png", "new.png", "panel.png", "pattern.png", "record-deco.png", "row-selected.png", "row.png", "speed-configured.png", "speed-empty.png", "speed-recorded.png", "theme.ini")
    }
    foreach ($id in $themes.Keys) {
        foreach ($file in ($themes[$id] + @("cursor.png", "cursor-glow.png"))) {
            [pscustomobject]@{ Source = "examples/Themes/$id/$file"; Destination = "$BinaryRelative/Themes/$id/$file" }
        }
    }
}

function Get-ExpectedPackageInventory {
    $inventory = @(
        "$BinaryRelative/D3DCOMPILER_43.dll",
        "$BinaryRelative/FF7RPianoSongs.ini",
        "$BinaryRelative/muscriptor-small-f16.gguf",
        "release.json"
    )
    foreach ($mapping in $DocumentMappings) { $inventory += $mapping.Destination }
    foreach ($mapping in Get-ExampleThemeMappings) { $inventory += $mapping.Destination }
    return @($inventory | Sort-Object)
}

function Assert-PackageTree {
    param([string]$Candidate, [string]$ExpectedArtifactPath = $Artifact)
    & $AuditTool $Root --staged-docs $Candidate
    if ($LASTEXITCODE -ne 0) {
        throw "Release audit failed for '$Candidate' with exit code $LASTEXITCODE"
    }
    $binaryRoot = Join-Path $Candidate $BinaryRelative
    $candidateDll = Join-Path $binaryRoot "D3DCOMPILER_43.dll"
    if (!(Test-Path -LiteralPath $candidateDll -PathType Leaf)) {
        throw "Package proxy DLL is missing: '$candidateDll'"
    }
    if (![string]::IsNullOrEmpty($ExpectedArtifactPath)) {
        Assert-FilesEqual -Source $ExpectedArtifactPath -Destination $candidateDll
    }
    Assert-FilesEqual -Source $Ini -Destination (Join-Path $binaryRoot "FF7RPianoSongs.ini")
    Assert-FilesEqual -Source $Model -Destination (Join-Path $binaryRoot "muscriptor-small-f16.gguf")
    Assert-FilesEqual -Source (Join-Path $Root "release.json") -Destination (Join-Path $Candidate "release.json")
    foreach ($mapping in $DocumentMappings) {
        Assert-FilesEqual -Source (Join-Path $Root $mapping.Source) `
            -Destination (Join-Path $Candidate $mapping.Destination)
    }
    foreach ($mapping in Get-ExampleThemeMappings) {
        Assert-FilesEqual -Source (Join-Path $Root $mapping.Source) `
            -Destination (Join-Path $Candidate $mapping.Destination)
    }
    $expected = @(Get-ExpectedPackageInventory)
    $actual = @(Get-RelativeFileInventory -Directory $Candidate)
    $difference = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual)
    if ($difference.Count -ne 0) {
        throw "Package inventory mismatch for '$Candidate'"
    }
}

$ReleasePath = Join-Path $Root "release"
function Assert-ReleaseTree {
    param(
        [string]$Candidate, [object]$Authority,
        [string]$PackageCandidate = ""
    )
    $archiveName = "$($Authority.archive_basename).zip"
    $archive = Join-Path $Candidate $archiveName
    $checksum = "$archive.sha256"
    $identityPath = Join-Path $Candidate "$($Authority.archive_basename).release.json"
    $publishedProvenance = Join-Path $Candidate "$($Authority.archive_basename).provenance.json"
    $expected = @(
        $archiveName, "$archiveName.sha256", "$($Authority.archive_basename).release.json",
        "$($Authority.archive_basename).provenance.json") | Sort-Object
    $actual = @(Get-RelativeFileInventory $Candidate)
    if (@(Compare-Object $expected $actual).Count -ne 0) { throw "Release artifact inventory mismatch" }
    $archiveHash = Get-ReleaseFileSha256 $archive
    if ([IO.File]::ReadAllText($checksum) -ne "$archiveHash  $archiveName`n") {
        throw "Release checksum sidecar does not match the archive"
    }
    $extract = Join-Path ([IO.Path]::GetTempPath()) ".ff7rp-release-verify-$PID-$([Guid]::NewGuid().ToString('N'))"
    try {
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $extract)
        Assert-PackageTree $extract ""
        if (![string]::IsNullOrEmpty($PackageCandidate)) {
            Assert-DirectoryTreesEqual $PackageCandidate $extract
        }
        $packageDll = Join-Path (Join-Path $extract $BinaryRelative) "D3DCOMPILER_43.dll"
        $publishedRecord = Assert-PublishedBuildProvenance $Root $publishedProvenance $packageDll `
            $Authority (Get-ProductionInputRelativePaths $Root "FF7RPianoSongsProxy")
        $null = Assert-ReleaseIdentity $identityPath $Authority $archive $packageDll $publishedProvenance `
            (Join-Path $extract "release.json") "" $SourceCommit `
            $publishedRecord.releaseIdentity.generatedHeaderSha256
    } finally {
        if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    }
}

function Assert-ReleaseSurface {
    param([string]$Candidate, [string]$PackageCandidate = "")
    $declaredBasenames = @($ReleaseTargets | ForEach-Object { [string]$_.archive_basename })
    $entries = @(Get-ChildItem -LiteralPath $Candidate -Force | Sort-Object Name)
    if ($entries.Count -eq 0 -or @($entries | Where-Object {
            !$_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $declaredBasenames -cnotcontains $_.Name
        }).Count -ne 0 -or $entries.Name -cnotcontains $ReleaseAuthority.archive_basename) {
        throw "Release surface must contain only declared game-build directories and include the selected target"
    }
    foreach ($entry in $entries) {
        $target = @($ReleaseTargets | Where-Object { $_.archive_basename -ceq $entry.Name })
        if ($target.Count -ne 1) { throw "Release target authority is ambiguous for '$($entry.Name)'" }
        $authority = Get-ReleaseAuthority $Root ([string]$target[0].supported_executable_catalog_id)
        $targetPackage = if ($entry.Name -ceq $ReleaseAuthority.archive_basename) { $PackageCandidate } else { "" }
        Assert-ReleaseTree $entry.FullName $authority $targetPackage
    }
}

function Copy-PackageTree {
    param([string]$Candidate)
    $binaryRoot = Join-Path $Candidate $BinaryRelative
    Copy-ValidatedFile $Artifact (Join-Path $binaryRoot "D3DCOMPILER_43.dll")
    Copy-ValidatedFile $Ini (Join-Path $binaryRoot "FF7RPianoSongs.ini")
    Copy-ValidatedFile $Model (Join-Path $binaryRoot "muscriptor-small-f16.gguf")
    Copy-ValidatedFile (Join-Path $Root "release.json") (Join-Path $Candidate "release.json")
    foreach ($mapping in $DocumentMappings) {
        Copy-ValidatedFile (Join-Path $Root $mapping.Source) (Join-Path $Candidate $mapping.Destination)
    }
    foreach ($mapping in Get-ExampleThemeMappings) {
        Copy-ValidatedFile (Join-Path $Root $mapping.Source) (Join-Path $Candidate $mapping.Destination)
    }
}

$publicationToken = "$PID-$([Guid]::NewGuid().ToString('N'))"
$packageStaging = Join-Path $Root ".package.staging-$publicationToken"
$releaseStaging = Join-Path $Root ".release.staging-$publicationToken"
try {
    Copy-PackageTree $packageStaging
    Assert-PackageTree $packageStaging
    New-Item -ItemType Directory -Path $releaseStaging | Out-Null
    $declaredBasenames = @($ReleaseTargets | ForEach-Object { [string]$_.archive_basename })
    Copy-ValidatedReleaseSiblings $ReleasePath $releaseStaging $ReleaseAuthority.archive_basename `
        $declaredBasenames {
            param($candidate, $basename)
            $target = @($ReleaseTargets | Where-Object { $_.archive_basename -ceq $basename })
            if ($target.Count -ne 1) { throw "Release target authority is ambiguous for '$basename'" }
            Assert-ReleaseTree $candidate `
                (Get-ReleaseAuthority $Root ([string]$target[0].supported_executable_catalog_id)) ""
        }
    $null = New-ReleaseArtifacts $Root $packageStaging `
        (Join-Path $releaseStaging $ReleaseAuthority.archive_basename) $Artifact $ProvenancePath `
        (Join-Path $BuildDir "generated/release_identity.generated.h") $SourceCommit $ReleaseCatalogId
    Assert-ReleaseSurface $releaseStaging $packageStaging
    $publicationResult = Invoke-TwoSurfacePublication $PackagePath $ReleasePath $packageStaging $releaseStaging `
        { param($packageCandidate, $releaseCandidate)
            Assert-PackageTree $packageCandidate
            Assert-ReleaseSurface $releaseCandidate $packageCandidate
        } `
        { param($packageCandidate, $releaseCandidate)
            Assert-PackageTree $packageCandidate
            Assert-ReleaseSurface $releaseCandidate $packageCandidate
        }
} catch {
    foreach ($partial in @($packageStaging, $releaseStaging)) {
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Recurse -Force }
    }
    throw
}
if (!$publicationResult.Committed) { throw "Two-surface publication returned without a committed pair" }
foreach ($debt in $publicationResult.CleanupDebt) {
    Write-Warning "Validated publication committed; exact prior backup retained for explicit cleanup: $debt"
}

Get-ChildItem -LiteralPath $PackagePath -File -Recurse | Sort-Object FullName |
    Select-Object FullName, Length
Get-ChildItem -LiteralPath $ReleasePath -File -Recurse | Sort-Object FullName |
    Select-Object FullName, Length
