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
            },
            [ordered]@{
                game_build = "1.004"
                supported_executable_catalog_id = "ff7rebirth-steam-win64-68fd6fde"
                archive_basename = "FF7RPianoSongs-0.1.0-win64-ff7r1.004"
            }
        )
    } | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $fixture "release.json"), $releaseJson)
    [IO.File]::WriteAllText((Join-Path $fixture "package/release.json"), $releaseJson)
    [IO.File]::WriteAllText((Join-Path $fixture "src/game/rva_catalog.json"),
        '{"schema_version":3,"default_build":"ff7rebirth-steam-win64-6a16ced2",' +
        '"builds":[{"id":"ff7rebirth-steam-win64-6a16ced2"},{"id":"ff7rebirth-steam-win64-68fd6fde"}],"addresses":[]}')
    $publicDocuments = @(
        "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md",
        "docs/SongFormat.md"
    )
    foreach ($relative in $publicDocuments) {
        $path = Join-Path (Join-Path $fixture "package") $relative
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllText($path, "$relative`n")
    }
    $dll = Join-Path $fixture "package/End/Binaries/Win64/D3DCOMPILER_43.dll"
    [IO.File]::WriteAllBytes($dll, [byte[]](0, 1, 2, 3, 255))
    [IO.File]::WriteAllText((Join-Path $fixture "package/End/Binaries/Win64/FF7RPianoSongs.ini"), "[General]`n")
    [IO.File]::WriteAllText((Join-Path $fixture "package/End/Binaries/Win64/muscriptor-small-f16.gguf"), "fixture model`n")
    $provenance = Join-Path $fixture "provenance.json"
    $generatedHeader = Join-Path $fixture "release_identity.generated.h"
    [IO.File]::WriteAllText($generatedHeader, "synthetic generated release identity`n")
    $inputHash = Get-ReleaseFileSha256 (Join-Path $fixture "release.json")
    $inputIdentity = "release.json`t$inputHash`n"
    $fixtureProvenance = [ordered]@{
        schema = "ff7rpianosongs.build-provenance.v3"
        configuration = "Release"
        dll = [ordered]@{ path = "proxy/Release/FF7RPianoSongsProxy.dll"; sha256 = Get-ReleaseFileSha256 $dll }
        toolchain = [ordered]@{
            visualStudioInstallationPath = "fixture"; compilerPath = "fixture-compiler"
            compilerVersion = "fixture"; compilerSha256 = "1" * 64
            cmakePath = "fixture-cmake"; cmakeVersion = "fixture"; cmakeSha256 = "2" * 64
            generator = "fixture"; generatorPlatform = "x64"; generatorToolset = "fixture"
        }
        cmake = [ordered]@{ cachePath = "CMakeCache.txt"; cacheSha256 = "3" * 64 }
        releaseIdentity = [ordered]@{
            authorityPath = "release.json"; authoritySha256 = $inputHash
            catalogId = "ff7rebirth-steam-win64-6a16ced2"
            generatedHeaderPath = "generated/release_identity.generated.h"
            generatedHeaderSha256 = Get-ReleaseFileSha256 $generatedHeader
        }
        productionInputs = @([ordered]@{ path = "release.json"; sha256 = $inputHash })
        productionInputSetSha256 = Get-ReleaseTextSha256 $inputIdentity
    }
    [IO.File]::WriteAllText($provenance, ($fixtureProvenance | ConvertTo-Json -Depth 8))
    $commit = "0123456789abcdef0123456789abcdef01234567"
    $catalogA = "ff7rebirth-steam-win64-6a16ced2"
    $catalogB = "ff7rebirth-steam-win64-68fd6fde"
    $a = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $fixture "out-a") $dll $provenance $generatedHeader $commit $catalogA
    $b = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $fixture "out-b") $dll $provenance $generatedHeader $commit $catalogA
    foreach ($name in @($a.Archive, $a.Checksum, $a.Identity, $a.Provenance)) {
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
        "CHANGELOG.md", "End/Binaries/Win64/D3DCOMPILER_43.dll",
        "End/Binaries/Win64/FF7RPianoSongs.ini", "End/Binaries/Win64/muscriptor-small-f16.gguf", "LICENSE", "README.md",
        "THIRD_PARTY_NOTICES.md", "docs/SongFormat.md", "release.json"
    ) | Sort-Object
    $packageSource = [IO.File]::ReadAllText((Join-Path (Split-Path -Parent $PSScriptRoot) "package_dist.ps1"))
    $tokens = $null; $parseErrors = $null
    $packageAst = [Management.Automation.Language.Parser]::ParseInput($packageSource, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw "package_dist.ps1 did not parse" }
    $inventoryFunction = $packageAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq "Get-ExpectedPackageInventory"
    }, $true)
    if (!$inventoryFunction) { throw "Package inventory authority was not found" }
    $scriptInventory = @(& {
        $BinaryRelative = "End/Binaries/Win64"
        $DocumentMappings = @($publicDocuments | ForEach-Object { [pscustomobject]@{ Destination = $_ } })
        . ([scriptblock]::Create($inventoryFunction.Extent.Text))
        Get-ExpectedPackageInventory
    })
    if (($scriptInventory -join "`n") -cne ($expectedInventory -join "`n")) {
        throw "Canonical package inventory does not match the proxy archive inventory"
    }
    if (($actualInventory -join "`n") -ne ($expectedInventory -join "`n")) {
        throw "Release archive order or exact minimal public inventory changed"
    }
    if (@($actualInventory | Where-Object {
        $_ -match '(^|/)(analysis|build|\.cache|examples|tools)(/|$)' -or
        $_ -match '\.(mid|midi|mp3|flac|wav|bin|pdb|asi)$' -or
        ($_ -match '\.dll$' -and $_ -cne "End/Binaries/Win64/D3DCOMPILER_43.dll")
    }).Count -ne 0) { throw "Release archive contains music, cache, analysis, build, or developer artifacts" }
    $identity = Get-Content -LiteralPath $a.Identity -Raw | ConvertFrom-Json
    if ($identity.product -ne "FF7RPianoSongs" -or $identity.platform -ne "win64" -or $identity.license -ne "MIT" -or
        $identity.archive.sha256 -ne (Get-ReleaseFileSha256 $a.Archive) -or
        $identity.schema -cne "ff7rpianosongs.release-identity.v3" -or
        $identity.artifact.path -cne "End/Binaries/Win64/D3DCOMPILER_43.dll" -or
        $identity.artifact.sha256 -ne (Get-ReleaseFileSha256 $dll) -or
        $identity.provenance_sha256 -ne (Get-ReleaseFileSha256 $provenance) -or
        $identity.release_authority_sha256 -ne (Get-ReleaseFileSha256 (Join-Path $fixture "release.json")) -or
        $identity.generated_release_header_sha256 -ne (Get-ReleaseFileSha256 $generatedHeader) -or
        $identity.source_commit -ne $commit) { throw "Release identity hashes or commit are incorrect" }
    $authority = Get-ReleaseAuthority $fixture $catalogA
    $null = Assert-ReleaseIdentity $a.Identity $authority $a.Archive $dll $provenance `
        (Join-Path $fixture "release.json") $generatedHeader $commit
    $identityJson = Get-Content -LiteralPath $a.Identity -Raw
    $identityMutationPath = Join-Path $fixture "mutated.release.json"
    foreach ($mutation in @(
        "wrong-product", "wrong-platform", "wrong-license", "wrong-catalog", "wrong-version", "wrong-source",
        "uppercase-source", "short-source", "long-source", "malformed-source",
        "missing-archive-file", "missing-archive-hash", "extra-archive-field", "wrong-archive-file",
        "wrong-archive-hash", "wrong-artifact-hash", "wrong-artifact-path", "missing-artifact-path",
        "missing-artifact-hash", "extra-artifact-field", "nonstring-artifact-path", "legacy-identity",
        "wrong-provenance-hash", "wrong-authority-hash", "wrong-header-hash",
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
            "wrong-artifact-hash" { $badIdentity.artifact.sha256 = (("0" * 64) -join "") }
            "wrong-artifact-path" { $badIdentity.artifact.path = "End/Binaries/Win64/FF7RPianoSongs.asi" }
            "missing-artifact-path" { $badIdentity.artifact.PSObject.Properties.Remove("path") }
            "missing-artifact-hash" { $badIdentity.artifact.PSObject.Properties.Remove("sha256") }
            "extra-artifact-field" { $badIdentity.artifact | Add-Member extra "forbidden" }
            "nonstring-artifact-path" { $badIdentity.artifact.path = $true }
            "legacy-identity" {
                $badIdentity.schema = "ff7rpianosongs.release-identity.v2"
                $badIdentity | Add-Member asi_sha256 $badIdentity.artifact.sha256
                $badIdentity.PSObject.Properties.Remove("artifact")
            }
            "wrong-provenance-hash" { $badIdentity.provenance_sha256 = (("0" * 64) -join "") }
            "wrong-authority-hash" { $badIdentity.release_authority_sha256 = (("0" * 64) -join "") }
            "wrong-header-hash" { $badIdentity.generated_release_header_sha256 = (("0" * 64) -join "") }
            "uppercase-hash" { $badIdentity.archive.sha256 = (("A" * 64) -join "") }
            "short-hash" { $badIdentity.artifact.sha256 = (("a" * 63) -join "") }
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
            $null = Assert-ReleaseIdentity $identityMutationPath $authority $a.Archive $dll $provenance `
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
        try { $null = Get-ReleaseDocument $fixture; $accepted = $true } catch {}
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
        try { $null = Get-ReleaseDocument $fixture; $accepted = $true } catch {}
        if ($accepted) { throw "Malformed release metadata was accepted: $mutation" }
    }
    [IO.File]::WriteAllText((Join-Path $fixture "release.json"), $releaseJson)
    foreach ($mutation in @("legacy-dll-path", "wrong-dll-hash", "wrong-catalog", "wrong-source-hash", "wrong-input-set")) {
        $bad = Get-Content -LiteralPath $provenance -Raw | ConvertFrom-Json
        switch ($mutation) {
            "legacy-dll-path" { $bad.dll.path = "bin/Release/FF7RPianoSongs.dll" }
            "wrong-dll-hash" { $bad.dll.sha256 = "0" * 64 }
            "wrong-catalog" { $bad.releaseIdentity.catalogId = $catalogB }
            "wrong-source-hash" { $bad.productionInputs[0].sha256 = "0" * 64 }
            "wrong-input-set" { $bad.productionInputSetSha256 = "0" * 64 }
        }
        $badPath = Join-Path $fixture "mutated.provenance.json"
        [IO.File]::WriteAllText($badPath, ($bad | ConvertTo-Json -Depth 8))
        $accepted = $false
        try {
            $null = Assert-PublishedBuildProvenance $fixture $badPath $dll $authority @("release.json")
            $accepted = $true
        } catch {}
        if ($accepted) { throw "Malformed published build provenance was accepted: $mutation" }
    }
    $selected = Get-ReleaseAuthority $fixture $catalogA
    if ($selected.game_build -cne "1.005" -or
        $selected.archive_basename -cne "FF7RPianoSongs-0.1.0-win64-ff7r1.005") {
        throw "Named release target selection returned the wrong authority"
    }
    $accepted = $false
    try { $null = Get-ReleaseAuthority $fixture "ff7rebirth-steam-win64-deadbeef"; $accepted = $true } catch {}
    if ($accepted) { throw "Release authority for an undeclared executable catalog ID was accepted" }

    function Assert-FixtureTarget([string]$candidate, [string]$basename) {
        $target = @((Get-ReleaseDocument $fixture).targets | Where-Object { $_.archive_basename -ceq $basename })
        if ($target.Count -ne 1) { throw "Fixture target is not declared exactly once: $basename" }
        $targetAuthority = Get-ReleaseAuthority $fixture $target[0].supported_executable_catalog_id
        $archivePath = Join-Path $candidate "$basename.zip"
        $checksumPath = "$archivePath.sha256"
        $identityPath = Join-Path $candidate "$basename.release.json"
        $provenancePath = Join-Path $candidate "$basename.provenance.json"
        $expected = @("$basename.zip", "$basename.zip.sha256", "$basename.release.json", "$basename.provenance.json") | Sort-Object
        $actual = @(Get-ChildItem -LiteralPath $candidate -File -Force | ForEach-Object Name | Sort-Object)
        if (@(Compare-Object $expected $actual).Count -ne 0) { throw "Fixture release target inventory mismatch: $basename" }
        $hash = Get-ReleaseFileSha256 $archivePath
        if ([IO.File]::ReadAllText($checksumPath) -cne "$hash  $basename.zip`n") {
            throw "Fixture release checksum mismatch: $basename"
        }
        $extract = Join-Path $testRoot "extract-$([Guid]::NewGuid().ToString('N'))"
        try {
            [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $extract)
            $files = @(Get-ChildItem -LiteralPath $extract -File -Recurse -Force |
                ForEach-Object { [IO.Path]::GetRelativePath($extract, $_.FullName).Replace('\', '/') } | Sort-Object)
            if (($files -join "`n") -cne ($expectedInventory -join "`n")) {
                throw "Retained target package inventory differs: $basename"
            }
            $archiveDll = Join-Path $extract "End/Binaries/Win64/D3DCOMPILER_43.dll"
            $record = Assert-PublishedBuildProvenance $fixture $provenancePath $archiveDll `
                $targetAuthority @("release.json")
            $null = Assert-ReleaseIdentity $identityPath $targetAuthority $archivePath $archiveDll $provenancePath `
                (Join-Path $extract "release.json") "" $commit $record.releaseIdentity.generatedHeaderSha256
        } finally {
            if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
        }
    }

    $surface = Join-Path $fixture "sequential-release"
    $stageA = Join-Path $fixture "sequential-stage-a"
    $stageB = Join-Path $fixture "sequential-stage-b"
    [IO.Directory]::CreateDirectory($stageA) | Out-Null
    $targetAName = "FF7RPianoSongs-0.1.0-win64-ff7r1.005"
    $targetBName = "FF7RPianoSongs-0.1.0-win64-ff7r1.004"
    $null = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $stageA $targetAName) `
        $dll $provenance $generatedHeader $commit $catalogA
    Move-Item -LiteralPath $stageA -Destination $surface
    [IO.Directory]::CreateDirectory($stageB) | Out-Null
    $validateFixtureTarget = { param($candidate, $basename) Assert-FixtureTarget $candidate $basename }
    Copy-ValidatedReleaseSiblings $surface $stageB $targetBName @($targetAName, $targetBName) $validateFixtureTarget
    $provenanceB = Join-Path $fixture "provenance-b.json"
    $fixtureProvenanceB = Get-Content -LiteralPath $provenance -Raw | ConvertFrom-Json
    $fixtureProvenanceB.releaseIdentity.catalogId = $catalogB
    [IO.File]::WriteAllText($provenanceB, ($fixtureProvenanceB | ConvertTo-Json -Depth 8))
    $null = New-ReleaseArtifacts $fixture (Join-Path $fixture "package") (Join-Path $stageB $targetBName) `
        $dll $provenanceB $generatedHeader $commit $catalogB
    foreach ($name in @($targetAName, $targetBName)) { Assert-FixtureTarget (Join-Path $stageB $name) $name }
    $finalDirectories = @(Get-ChildItem -LiteralPath $stageB -Directory -Force | ForEach-Object Name | Sort-Object)
    $expectedDirectories = @($targetAName, $targetBName) | Sort-Object
    if (($finalDirectories -join "`n") -cne ($expectedDirectories -join "`n")) {
        throw "Sequential release staging did not retain the complete declared target inventory"
    }

    $surfaceWitness = @(Get-ChildItem -LiteralPath $surface -File -Recurse -Force | Sort-Object FullName |
        ForEach-Object { "$(Get-ReleaseFileSha256 $_.FullName) $([IO.Path]::GetRelativePath($surface, $_.FullName))" }) -join "`n"
    foreach ($rejection in @("stale", "foreign", "mismatched", "previous-version", "legacy-provenance")) {
        $badSurface = Join-Path $fixture "bad-$rejection"
        Copy-Item -LiteralPath $surface -Destination $badSurface -Recurse
        switch ($rejection) {
            "stale" { Remove-Item -LiteralPath (Join-Path $badSurface "$targetAName/$targetAName.provenance.json") }
            "foreign" { [IO.Directory]::CreateDirectory((Join-Path $badSurface "foreign-target")) | Out-Null }
            "previous-version" {
                Move-Item -LiteralPath (Join-Path $badSurface $targetAName) `
                    -Destination (Join-Path $badSurface "FF7RPianoSongs-0.0.9-win64-ff7r1.005")
            }
            "legacy-provenance" {
                $path = Join-Path $badSurface "$targetAName/$targetAName.provenance.json"
                $badRecord = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
                $badRecord.dll.path = "bin/Release/FF7RPianoSongs.dll"
                [IO.File]::WriteAllText($path, ($badRecord | ConvertTo-Json -Depth 8))
            }
            "mismatched" {
                $badIdentityPath = Join-Path $badSurface "$targetAName/$targetAName.release.json"
                $badIdentity = Get-Content -LiteralPath $badIdentityPath -Raw | ConvertFrom-Json
                $badIdentity.source_commit = "1" * 40
                [IO.File]::WriteAllText($badIdentityPath, ($badIdentity | ConvertTo-Json -Depth 5))
            }
        }
        $badWitness = @(Get-ChildItem -LiteralPath $badSurface -File -Recurse -Force | Sort-Object FullName |
            ForEach-Object { "$(Get-ReleaseFileSha256 $_.FullName) $([IO.Path]::GetRelativePath($badSurface, $_.FullName))" }) -join "`n"
        $rejectionStage = Join-Path $fixture "reject-stage-$rejection"
        [IO.Directory]::CreateDirectory($rejectionStage) | Out-Null
        $rejected = $false
        try {
            Copy-ValidatedReleaseSiblings $badSurface $rejectionStage $targetBName @($targetAName, $targetBName) $validateFixtureTarget
        } catch { $rejected = $true }
        $afterWitness = @(Get-ChildItem -LiteralPath $badSurface -File -Recurse -Force | Sort-Object FullName |
            ForEach-Object { "$(Get-ReleaseFileSha256 $_.FullName) $([IO.Path]::GetRelativePath($badSurface, $_.FullName))" }) -join "`n"
        if (!$rejected -or $afterWitness -cne $badWitness) {
            throw "Invalid sibling was accepted or prior release surface was replaced: $rejection"
        }
    }
    $surfaceWitnessAfter = @(Get-ChildItem -LiteralPath $surface -File -Recurse -Force | Sort-Object FullName |
        ForEach-Object { "$(Get-ReleaseFileSha256 $_.FullName) $([IO.Path]::GetRelativePath($surface, $_.FullName))" }) -join "`n"
    if ($surfaceWitnessAfter -cne $surfaceWitness) { throw "Sequential rejection tests changed the authoritative prior release surface" }
    Write-Output "Release package self-test passed: deterministic target artifacts, exact identities, sequential aggregate inventory, rejection/non-replacement, and malformed metadata."
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
