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

function Remove-PreviousPackageSafely {
    param(
        [string]$Previous,
        [string]$RecoveryArchive,
        [string]$RecoveryValidation,
        [switch]$InjectBackupDeleteFailure
    )
    $archiveValidated = $false
    try {
        [System.IO.Compression.ZipFile]::CreateFromDirectory(
            $Previous, $RecoveryArchive, [System.IO.Compression.CompressionLevel]::Optimal, $false)
        [System.IO.Compression.ZipFile]::ExtractToDirectory($RecoveryArchive, $RecoveryValidation)
        Assert-DirectoryTreesEqual -Expected $Previous -Actual $RecoveryValidation
        $archiveValidated = $true
        Remove-Item -LiteralPath $RecoveryValidation -Recurse -Force
        if ($InjectBackupDeleteFailure) {
            throw "injected old-backup deletion failure"
        }
        try {
            Remove-Item -LiteralPath $Previous -Recurse -Force
        } catch {
            throw "old package deletion failed; validated recovery archive retained at '$RecoveryArchive': $($_.Exception.Message)"
        }
        try {
            Remove-Item -LiteralPath $RecoveryArchive -Force
        } catch {
            throw "publication committed; recoverable old package archive retained at '$RecoveryArchive': $($_.Exception.Message)"
        }
    } catch {
        $failure = $_
        $cleanupErrors = @()
        if (Test-Path -LiteralPath $RecoveryValidation) {
            try { Remove-Item -LiteralPath $RecoveryValidation -Recurse -Force }
            catch { $cleanupErrors += "recovery-validation cleanup failed at '${RecoveryValidation}': $($_.Exception.Message)" }
        }
        $recoverable = @()
        if (Test-Path -LiteralPath $Previous) { $recoverable += "directory '$Previous'" }
        if ($archiveValidated -and (Test-Path -LiteralPath $RecoveryArchive -PathType Leaf)) {
            $recoverable += "validated archive '$RecoveryArchive'"
        } elseif (Test-Path -LiteralPath $RecoveryArchive -PathType Leaf) {
            try { Remove-Item -LiteralPath $RecoveryArchive -Force }
            catch { $cleanupErrors += "unvalidated recovery-archive cleanup failed at '${RecoveryArchive}': $($_.Exception.Message)" }
        }
        $message = "Publication committed and validated, but old-package retirement failed: $($failure.Exception.Message)"
        if ($recoverable.Count -gt 0) { $message += "; recoverable copy retained as " + ($recoverable -join " and ") }
        if ($cleanupErrors.Count -gt 0) { $message += "; " + ($cleanupErrors -join "; ") }
        throw $message
    }
}

function Invoke-TransactionalDirectoryPublication {
    param(
        [string]$FinalPath,
        [scriptblock]$PopulateAndValidate,
        [scriptblock]$ValidatePublished,
        [switch]$InjectPublishRenameFailure,
        [switch]$InjectRollbackRenameFailure,
        [switch]$InjectQuarantineFailure,
        [switch]$InjectFinalPathObstruction,
        [switch]$InjectBackupDeleteFailure
    )
    $parent = Split-Path -Parent $FinalPath
    $leaf = Split-Path -Leaf $FinalPath
    $token = "$PID-$([Guid]::NewGuid().ToString('N'))"
    $staging = Join-Path $parent ".$leaf.staging-$token"
    $previous = Join-Path $parent ".$leaf.previous-$token"
    $failed = Join-Path $parent ".$leaf.failed-$token"
    $recoveryArchive = Join-Path $parent ".$leaf.recovery-$token.zip"
    $recoveryValidation = Join-Path $parent ".$leaf.recovery-validation-$token"
    $previousMoved = $false
    $published = $false
    try {
        New-Item -ItemType Directory -Path $staging | Out-Null
        & $PopulateAndValidate $staging

        if (Test-Path -LiteralPath $FinalPath) {
            [System.IO.Directory]::Move($FinalPath, $previous)
            $previousMoved = $true
        }
        if ($InjectPublishRenameFailure) {
            throw "injected publish rename failure"
        }
        [System.IO.Directory]::Move($staging, $FinalPath)
        $published = $true

        & $ValidatePublished $FinalPath
    } catch {
        $failure = $_
        $rollbackErrors = @()
        if ($published -and (Test-Path -LiteralPath $FinalPath)) {
            try {
                if ($InjectQuarantineFailure) {
                    throw "injected failed-package quarantine failure"
                }
                [System.IO.Directory]::Move($FinalPath, $failed)
                $published = $false
            } catch {
                $rollbackErrors += "failed package quarantine to '${failed}' failed; new package remains at '${FinalPath}' and prior package is preserved at '${previous}': $($_.Exception.Message)"
            }
        }
        if ($InjectFinalPathObstruction -and $previousMoved -and
            !(Test-Path -LiteralPath $FinalPath)) {
            New-Item -ItemType Directory -Path $FinalPath | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $FinalPath "injected-obstruction.txt"), "obstruction")
            $rollbackErrors += "injected final-path obstruction retained at '${FinalPath}'; failed package quarantine path was '${failed}'"
        }
        if ($previousMoved -and (Test-Path -LiteralPath $previous)) {
            if (Test-Path -LiteralPath $FinalPath) {
                $rollbackErrors += "prior package could not be restored because '${FinalPath}' is occupied; prior package retained at '${previous}'; failed package quarantine path was '${failed}'"
            } elseif ($InjectRollbackRenameFailure) {
                $rollbackErrors += "injected rollback rename failure; prior package retained at '${previous}'; failed package quarantine path was '${failed}'"
            } else {
                try {
                    [System.IO.Directory]::Move($previous, $FinalPath)
                    $previousMoved = $false
                } catch {
                    $rollbackErrors += "rollback to '${FinalPath}' failed; prior package retained at '${previous}'; failed package quarantine path was '${failed}': $($_.Exception.Message)"
                }
            }
        }
        if (Test-Path -LiteralPath $staging) {
            try { Remove-Item -LiteralPath $staging -Recurse -Force }
            catch { $rollbackErrors += "staging cleanup failed at '${staging}': $($_.Exception.Message)" }
        }
        if (Test-Path -LiteralPath $failed) {
            try { Remove-Item -LiteralPath $failed -Recurse -Force }
            catch { $rollbackErrors += "failed-package quarantine retained at '${failed}': $($_.Exception.Message)" }
        }
        if ($previousMoved -and (Test-Path -LiteralPath $previous) -and
            !($rollbackErrors -join "; ").Contains($previous)) {
            $rollbackErrors += "prior package retained at '${previous}'"
        }
        if (Test-Path -LiteralPath $recoveryArchive -PathType Leaf) {
            $rollbackErrors += "recovery archive retained at '${recoveryArchive}'"
        }
        $message = $failure.Exception.Message
        if ($rollbackErrors.Count -gt 0) { $message += "; " + ($rollbackErrors -join "; ") }
        throw $message
    }

    if ($previousMoved) {
        Remove-PreviousPackageSafely -Previous $previous -RecoveryArchive $recoveryArchive `
            -RecoveryValidation $recoveryValidation -InjectBackupDeleteFailure:$InjectBackupDeleteFailure
    }
}

function Assert-TransactionalRollbackSelfTests {
    $testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ".ff7rp-package-transaction-$PID-$([Guid]::NewGuid().ToString('N'))"
    try {
        New-Item -ItemType Directory -Path $testRoot | Out-Null
        foreach ($mode in @("copy", "staging-validation", "publish-rename", "post-publication")) {
            $final = Join-Path $testRoot "package-$mode"
            New-Item -ItemType Directory -Path $final | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $final "prior.txt"), "prior-$mode")
            $failedAsExpected = $false
            try {
                Invoke-TransactionalDirectoryPublication -FinalPath $final -PopulateAndValidate {
                    param($stage)
                    if ($mode -eq "copy") { throw "injected copy failure" }
                    [System.IO.File]::WriteAllText((Join-Path $stage "new.txt"), "new-$mode")
                    if ($mode -eq "staging-validation") { throw "injected staging validation failure" }
                } -ValidatePublished {
                    param($published)
                    if ($mode -eq "post-publication") { throw "injected post-publication audit failure" }
                    if ([System.IO.File]::ReadAllText((Join-Path $published "new.txt")) -ne "new-$mode") {
                        throw "published self-test payload mismatch"
                    }
                } -InjectPublishRenameFailure:($mode -eq "publish-rename")
            } catch {
                $failedAsExpected = $true
            }
            if (!$failedAsExpected -or !(Test-Path -LiteralPath (Join-Path $final "prior.txt")) -or
                [System.IO.File]::ReadAllText((Join-Path $final "prior.txt")) -ne "prior-$mode") {
                throw "Transactional rollback self-test failed for injected $mode failure"
            }
        }

        $rollbackFinal = Join-Path $testRoot "package-rollback-rename"
        New-Item -ItemType Directory -Path $rollbackFinal | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $rollbackFinal "prior.txt"), "prior-rollback")
        try {
            Invoke-TransactionalDirectoryPublication -FinalPath $rollbackFinal -PopulateAndValidate {
                param($stage)
                [System.IO.File]::WriteAllText((Join-Path $stage "new.txt"), "new")
            } -ValidatePublished { param($published) } `
                -InjectPublishRenameFailure -InjectRollbackRenameFailure
            throw "rollback rename failure injection unexpectedly succeeded"
        } catch {
            $rollbackBackups = @(Get-ChildItem -LiteralPath $testRoot -Directory -Force |
                Where-Object { $_.Name -like ".package-rollback-rename.previous-*" })
            if ($rollbackBackups.Count -ne 1 -or
                [System.IO.File]::ReadAllText((Join-Path $rollbackBackups[0].FullName "prior.txt")) -ne "prior-rollback") {
                throw "Rollback rename failure did not retain an identifiable prior package"
            }
            [System.IO.Directory]::Move($rollbackBackups[0].FullName, $rollbackFinal)
        }

        foreach ($mode in @("quarantine", "final-obstruction")) {
            $diagnosticFinal = Join-Path $testRoot "package-$mode"
            New-Item -ItemType Directory -Path $diagnosticFinal | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $diagnosticFinal "prior.txt"), "prior-$mode")
            $reported = ""
            try {
                Invoke-TransactionalDirectoryPublication -FinalPath $diagnosticFinal -PopulateAndValidate {
                    param($stage)
                    [System.IO.File]::WriteAllText((Join-Path $stage "new.txt"), "new-$mode")
                } -ValidatePublished {
                    param($published)
                    throw "injected post-publication failure for $mode"
                } -InjectQuarantineFailure:($mode -eq "quarantine") `
                  -InjectFinalPathObstruction:($mode -eq "final-obstruction")
                throw "$mode diagnostic failure injection unexpectedly succeeded"
            } catch {
                $reported = $_.Exception.Message
            }
            $previousDirectories = @(Get-ChildItem -LiteralPath $testRoot -Directory -Force |
                Where-Object { $_.Name -like ".package-$mode.previous-*" })
            if ($previousDirectories.Count -ne 1) {
                throw "$mode diagnostic failure did not retain exactly one prior package"
            }
            $previousPath = $previousDirectories[0].FullName
            $failedPath = $previousPath -replace '\.previous-', '.failed-'
            if (!$reported.Contains($previousPath) -or !$reported.Contains($diagnosticFinal) -or
                !$reported.Contains($failedPath)) {
                throw "$mode diagnostic omitted a recovery path: $reported"
            }
            if (Test-Path -LiteralPath $diagnosticFinal) {
                Remove-Item -LiteralPath $diagnosticFinal -Recurse -Force
            }
            if (Test-Path -LiteralPath $failedPath) {
                Remove-Item -LiteralPath $failedPath -Recurse -Force
            }
            [System.IO.Directory]::Move($previousPath, $diagnosticFinal)
        }

        $deleteFinal = Join-Path $testRoot "package-backup-delete"
        New-Item -ItemType Directory -Path $deleteFinal | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $deleteFinal "prior.txt"), "prior-delete")
        try {
            Invoke-TransactionalDirectoryPublication -FinalPath $deleteFinal -PopulateAndValidate {
                param($stage)
                [System.IO.File]::WriteAllText((Join-Path $stage "new.txt"), "new-delete")
            } -ValidatePublished {
                param($published)
                if ([System.IO.File]::ReadAllText((Join-Path $published "new.txt")) -ne "new-delete") {
                    throw "published delete-test payload mismatch"
                }
            } -InjectBackupDeleteFailure
            throw "backup deletion failure injection unexpectedly succeeded"
        } catch {
            if (!(Test-Path -LiteralPath (Join-Path $deleteFinal "new.txt"))) {
                throw "Validated new package was rolled back after backup-retirement failure"
            }
            $recoverableDirectories = @(Get-ChildItem -LiteralPath $testRoot -Directory -Force |
                Where-Object { $_.Name -like ".package-backup-delete.previous-*" })
            $recoverableArchives = @(Get-ChildItem -LiteralPath $testRoot -File -Force |
                Where-Object { $_.Name -like ".package-backup-delete.recovery-*.zip" })
            if ($recoverableDirectories.Count -ne 1 -or $recoverableArchives.Count -ne 1) {
                throw "Backup deletion failure did not retain identifiable recoverable copies"
            }
            Remove-Item -LiteralPath $recoverableDirectories[0].FullName -Recurse -Force
            Remove-Item -LiteralPath $recoverableArchives[0].FullName -Force
        }

        $leftovers = @(Get-ChildItem -LiteralPath $testRoot -Force |
            Where-Object { $_.Name -match '^\.package-.*\.(staging|previous|failed|recovery|recovery-validation)-' })
        if ($leftovers.Count -ne 0) {
            throw "Transactional publication self-tests left staging/recovery artifacts"
        }
        Write-Output "Transactional publication adverse-path self-tests passed: copy, staging-validation, publish-rename, post-publication, rollback-rename, quarantine-diagnostic, final-obstruction-diagnostic, backup-delete"
    } finally {
        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
}

Assert-TransactionalRollbackSelfTests
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
        -Target @("FF7RPianoSongs", "release_audit_tool") -Parallel $Parallel -CatalogBuildId $ReleaseCatalogId
}
if ($LASTEXITCODE -ne 0) {
    throw "Build script failed with exit code $LASTEXITCODE"
}

$Artifact = Join-Path $Root "dist/FF7RPianoSongs.asi"
$BuiltDll = Join-Path $Root "build/bin/$Configuration/FF7RPianoSongs.dll"
$ProvenancePath = Join-Path $Root "build/bin/$Configuration/FF7RPianoSongs.provenance.json"
$Ini = Join-Path $Root "FF7RPianoSongs.example.ini"
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

Assert-BuildProvenance $Root $BuiltDll $ProvenancePath $Configuration `
    (Get-ProductionInputRelativePaths $Root) (Join-Path $Root "build")
$BuiltCatalogId = [string](Get-Content -LiteralPath $ProvenancePath -Raw |
    ConvertFrom-Json).releaseIdentity.catalogId
if ($BuiltCatalogId -cne $ReleaseCatalogId) {
    throw "The built artifact targets game build '$BuiltCatalogId', but '$ReleaseCatalogId' is being packaged"
}
Assert-BuildProvenanceAdversePathSelfTests
Assert-FilesEqual -Source $BuiltDll -Destination $Artifact

function Get-ExpectedPackageInventory {
    $inventory = @(
        "$BinaryRelative/FF7RPianoSongs.asi",
        "$BinaryRelative/FF7RPianoSongs.ini",
        "release.json"
    )
    foreach ($mapping in $DocumentMappings) { $inventory += $mapping.Destination }
    return @($inventory | Sort-Object)
}

function Assert-PackageTree {
    param([string]$Candidate)
    & $AuditTool $Root --staged-docs $Candidate
    if ($LASTEXITCODE -ne 0) {
        throw "Release audit failed for '$Candidate' with exit code $LASTEXITCODE"
    }
    $binaryRoot = Join-Path $Candidate $BinaryRelative
    Assert-FilesEqual -Source $Artifact -Destination (Join-Path $binaryRoot "FF7RPianoSongs.asi")
    Assert-FilesEqual -Source $Ini -Destination (Join-Path $binaryRoot "FF7RPianoSongs.ini")
    Assert-FilesEqual -Source (Join-Path $Root "release.json") -Destination (Join-Path $Candidate "release.json")
    foreach ($mapping in $DocumentMappings) {
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
    param([string]$Candidate, [string]$PackageCandidate = $PackagePath)
    $archiveName = "$($ReleaseAuthority.archive_basename).zip"
    $archive = Join-Path $Candidate $archiveName
    $checksum = "$archive.sha256"
    $identityPath = Join-Path $Candidate "$($ReleaseAuthority.archive_basename).release.json"
    $expected = @($archiveName, "$archiveName.sha256", "$($ReleaseAuthority.archive_basename).release.json") | Sort-Object
    $actual = @(Get-RelativeFileInventory $Candidate)
    if (@(Compare-Object $expected $actual).Count -ne 0) { throw "Release artifact inventory mismatch" }
    $archiveHash = Get-ReleaseFileSha256 $archive
    if ([IO.File]::ReadAllText($checksum) -ne "$archiveHash  $archiveName`n") {
        throw "Release checksum sidecar does not match the archive"
    }
    $packageAsi = Join-Path (Join-Path $PackageCandidate $BinaryRelative) "FF7RPianoSongs.asi"
    $null = Assert-ReleaseIdentity $identityPath $ReleaseAuthority $archive $packageAsi $ProvenancePath `
        (Join-Path $PackageCandidate "release.json") `
        (Join-Path $BuildDir "generated/release_identity.generated.h") $SourceCommit
    $extract = Join-Path ([IO.Path]::GetTempPath()) ".ff7rp-release-verify-$PID-$([Guid]::NewGuid().ToString('N'))"
    try {
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $extract)
        Assert-DirectoryTreesEqual $PackageCandidate $extract
    } finally {
        if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    }
}

function Assert-ReleaseSurface {
    param([string]$Candidate, [string]$PackageCandidate = $PackagePath)
    $expected = @($ReleaseAuthority.archive_basename)
    $actual = @(Get-ChildItem -LiteralPath $Candidate -Force | ForEach-Object { $_.Name } | Sort-Object)
    if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
        throw "Release surface must contain exactly the packaged game-build directory"
    }
    Assert-ReleaseTree (Join-Path $Candidate $ReleaseAuthority.archive_basename) $PackageCandidate
}

$publicationToken = "$PID-$([Guid]::NewGuid().ToString('N'))"
$packageStaging = Join-Path $Root ".package.staging-$publicationToken"
$releaseStaging = Join-Path $Root ".release.staging-$publicationToken"
$binaryRoot = Join-Path $packageStaging $BinaryRelative
Copy-ValidatedFile $Artifact (Join-Path $binaryRoot "FF7RPianoSongs.asi")
Copy-ValidatedFile $Ini (Join-Path $binaryRoot "FF7RPianoSongs.ini")
Copy-ValidatedFile (Join-Path $Root "release.json") (Join-Path $packageStaging "release.json")
foreach ($mapping in $DocumentMappings) {
    Copy-ValidatedFile (Join-Path $Root $mapping.Source) (Join-Path $packageStaging $mapping.Destination)
}
Assert-PackageTree $packageStaging
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
if (!$publicationResult.Committed) { throw "Two-surface publication returned without a committed pair" }
foreach ($debt in $publicationResult.CleanupDebt) {
    Write-Warning "Validated publication committed; exact prior backup retained for explicit cleanup: $debt"
}

Get-ChildItem -LiteralPath $PackagePath -File -Recurse | Sort-Object FullName |
    Select-Object FullName, Length
Get-ChildItem -LiteralPath $ReleasePath -File -Recurse | Sort-Object FullName |
    Select-Object FullName, Length
