[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Prepare", "Collect", "Finalize", "Recover", "Status")]
    [string]$Action,

    [string]$GameRoot,
    [string]$PackageRoot,
    [string]$RepositoryRoot,
    [string]$StateDirectory,
    [string]$Session,
    [string]$SongId,
    [ValidateSet("loader-valid-sidecar")]
    [string]$Scenario = "loader-valid-sidecar",
    [string]$TestNonce,

    [ValidateSet("Keep", "Rollback", "Uninstall")]
    [string]$Disposition = "Keep",

    [ValidateSet("Packaged", "Development")]
    [string]$Mode = "Packaged"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptPath = $MyInvocation.MyCommand.Path
$ToolsRoot = Split-Path -Parent $ScriptPath
$DefaultRepositoryRoot = Split-Path -Parent $ToolsRoot
$ProcessName = "ff7rebirth_"
$AsiName = "FF7RPianoSongs.asi"
$LogName = "FF7RPianoSongs.log"
$ManifestName = "session.json"
$ManifestSchema = "ff7rpianosongs.runtime-gate.v1"
$ProvenanceSchema = "ff7rpianosongs.build-provenance.v2"
$TerminalStates = @("Accepted", "RolledBack", "Uninstalled", "Recovered")
$GlobalLockName = "Global\FF7RPianoSongs.RuntimeGate.v1"
$MaxExcerptLines = 240
$MaxExcerptLineCharacters = 4096
$MaxExcerptUtf8Bytes = 65536
$SafeExcerptHexFields = @("mabf_size")

. (Join-Path $ToolsRoot "build_provenance.ps1")

function Get-FullPath([string]$Path, [string]$BasePath) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "A required path is empty."
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Test-IsSameOrChildPath([string]$Candidate, [string]$Parent) {
    $relative = [System.IO.Path]::GetRelativePath($Parent, $Candidate)
    return $relative -eq "." -or
        (![System.IO.Path]::IsPathRooted($relative) -and
         $relative -ne ".." -and !$relative.StartsWith("..$([System.IO.Path]::DirectorySeparatorChar)"))
}

function Assert-SafeStateDirectory([string]$Path, [string]$RepoRoot, [string]$NormalizedGameRoot) {
    if ((Test-IsSameOrChildPath $Path $RepoRoot) -or
        (Test-IsSameOrChildPath $Path $NormalizedGameRoot)) {
        throw "Runtime-gate state must be outside the repository and game trees."
    }
    $cursor = $Path
    while (![string]::IsNullOrWhiteSpace($cursor)) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Runtime-gate state may not traverse a reparse point: $cursor"
            }
        }
        $parent = Split-Path -Parent $cursor
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
}

function Get-Sha256([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file is missing: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-FileRecord([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        length = $item.Length
        sha256 = Get-Sha256 $item.FullName
        lastWriteTimeUtc = $item.LastWriteTimeUtc.ToString("o")
    }
}

function Get-UtcDateTime([object]$Value) {
    if ($Value -is [DateTime]) {
        return ([DateTime]$Value).ToUniversalTime()
    }
    return [DateTime]::Parse(
        [string]$Value,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}

function Write-JsonAtomic([object]$Value, [string]$Path) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporary = "$Path.tmp.$PID.$([guid]::NewGuid().ToString('N'))"
    try {
        $Value | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $temporary -Encoding utf8 -NoNewline
        [System.IO.File]::Move($temporary, $Path, $true)
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Read-Manifest([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Session manifest is missing: $Path"
    }
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.schema -ne $ManifestSchema) {
        throw "Unsupported runtime-gate manifest schema in $Path"
    }
    return $manifest
}

function Resolve-ManifestPath([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "-Session is required for action $Action."
    }
    $resolved = Get-FullPath $Value (Get-Location).Path
    if (Test-Path -LiteralPath $resolved -PathType Container) {
        $resolved = Join-Path $resolved $ManifestName
    }
    return $resolved
}

function Add-Operation([object]$Manifest, [string]$Name, [string]$Detail) {
    $entry = [ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString("o")
        name = $Name
        detail = $Detail
    }
    $Manifest.operations = @($Manifest.operations) + $entry
}

function Save-Manifest([object]$Manifest, [string]$Path, [string]$State) {
    $Manifest.state = $State
    $Manifest.updatedUtc = [DateTime]::UtcNow.ToString("o")
    Write-JsonAtomic $Manifest $Path
}

function Test-IsAuthorizedFixture([object]$Context) {
    if ([string]::IsNullOrWhiteSpace($TestNonce) -or
        $TestNonce -ne $env:FF7RP_RUNTIME_GATE_TEST_NONCE -or
        [string]::IsNullOrWhiteSpace($env:FF7RP_RUNTIME_GATE_TEST_ROOT)) {
        return $false
    }
    $testRoot = Get-FullPath $env:FF7RP_RUNTIME_GATE_TEST_ROOT (Get-Location).Path
    $systemTemp = Get-FullPath ([System.IO.Path]::GetTempPath()) (Get-Location).Path
    return (Test-IsSameOrChildPath $testRoot $systemTemp) -and
        (Test-IsSameOrChildPath ([string]$Context.repositoryRoot) $testRoot) -and
        (Test-IsSameOrChildPath ([string]$Context.gameRoot) $testRoot)
}

function Assert-GameStopped([object]$Context) {
    $override = $env:FF7RP_RUNTIME_GATE_TEST_PROCESS
    if ((Test-IsAuthorizedFixture $Context) -and ![string]::IsNullOrWhiteSpace($override)) {
        switch ($override) {
            "NotRunning" { return }
            "Running" { throw "$ProcessName is running; the requested action is blocked." }
            "QueryFailure" { throw "Unable to establish process state; failing closed." }
            default { throw "Unknown test process override: $override" }
        }
    }

    try {
        $running = @(Get-Process -ErrorAction Stop | Where-Object { $_.ProcessName -eq $ProcessName })
    }
    catch {
        throw "Unable to establish process state; failing closed: $($_.Exception.Message)"
    }
    if ($running.Count -ne 0) {
        throw "$ProcessName is running; exit the game before continuing."
    }
}

function Invoke-TestFailure([string]$Stage, [object]$Context) {
    if ((Test-IsAuthorizedFixture $Context) -and
        $env:FF7RP_RUNTIME_GATE_TEST_FAILURE -eq $Stage) {
        throw "Injected runtime-gate failure at $Stage"
    }
}

function Invoke-TestHardTermination([string]$Stage, [object]$Context) {
    if ((Test-IsAuthorizedFixture $Context) -and
        $env:FF7RP_RUNTIME_GATE_TEST_HARD_EXIT -eq $Stage) {
        [Environment]::Exit(197)
    }
}

function Get-PeIdentity([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Executable is not a valid PE image: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 88 -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Executable has an invalid PE header: $Path"
    }
    $optionalOffset = $peOffset + 24
    return [ordered]@{
        path = [System.IO.Path]::GetFullPath($Path)
        length = $bytes.Length
        sha256 = Get-Sha256 $Path
        peTimestamp = ('0x{0:x8}' -f [BitConverter]::ToUInt32($bytes, $peOffset + 8))
        sizeOfImage = ('0x{0:x8}' -f [BitConverter]::ToUInt32($bytes, $optionalOffset + 56))
        peChecksum = ('0x{0:x8}' -f [BitConverter]::ToUInt32($bytes, $optionalOffset + 64))
    }
}

function Assert-ArtifactIdentity(
    [string]$RepoRoot,
    [string]$PackagePath,
    [string]$ArtifactMode) {
    $provenancePath = Join-Path $RepoRoot "build/bin/Release/FF7RPianoSongs.provenance.json"
    $buildDll = Join-Path $RepoRoot "build/bin/Release/FF7RPianoSongs.dll"
    Assert-BuildProvenance $RepoRoot $buildDll $provenancePath "Release" `
        (Get-ProductionInputRelativePaths $RepoRoot) (Join-Path $RepoRoot "build")
    $provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
    $distAsi = Join-Path $RepoRoot "dist/$AsiName"
    $expected = ([string]$provenance.dll.sha256).ToLowerInvariant()
    foreach ($candidate in @($buildDll, $distAsi)) {
        if ((Get-Sha256 $candidate) -ne $expected) {
            throw "Artifact does not match Release provenance: $candidate"
        }
    }
    $packageAsi = ""
    $sourcePath = $distAsi
    if ($ArtifactMode -eq "Packaged") {
        $packageAsi = Join-Path $PackagePath "End/Binaries/Win64/$AsiName"
        if ((Get-Sha256 $packageAsi) -ne $expected) {
            throw "Artifact does not match Release provenance: $packageAsi"
        }
        $sourcePath = $packageAsi
    }
    return [ordered]@{
        mode = $ArtifactMode
        provenancePath = $provenancePath
        buildDll = $buildDll
        distAsi = $distAsi
        packageAsi = $packageAsi
        sourcePath = $sourcePath
        sha256 = $expected
        productionInputSetSha256 = [string]$provenance.productionInputSetSha256
        releaseAuthoritySha256 = [string]$provenance.releaseIdentity.authoritySha256
        generatedReleaseHeaderSha256 = [string]$provenance.releaseIdentity.generatedHeaderSha256
    }
}

function Assert-ExecutableIdentity([string]$RepoRoot, [string]$ExecutablePath) {
    $catalogPath = Join-Path $RepoRoot "src/game/rva_catalog.json"
    $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
    $actual = Get-PeIdentity $ExecutablePath
    $expected = $catalog.build
    if ($actual.length -ne [int64]$expected.file_size -or
        $actual.sha256 -ne ([string]$expected.sha256).ToLowerInvariant() -or
        $actual.peTimestamp -ne ([string]$expected.pe_timestamp).ToLowerInvariant() -or
        $actual.sizeOfImage -ne ([string]$expected.size_of_image).ToLowerInvariant() -or
        $actual.peChecksum -ne ([string]$expected.pe_checksum).ToLowerInvariant()) {
        throw "Game executable does not match the supported RVA catalog identity."
    }
    $actual.catalogId = [string]$catalog.build.id
    return $actual
}

function Enter-StateLock {
    $mutex = [System.Threading.Mutex]::new($false, $GlobalLockName)
    try {
        try {
            $acquired = $mutex.WaitOne(0)
        }
        catch [System.Threading.AbandonedMutexException] {
            $acquired = $true
        }
        if (!$acquired) {
            $mutex.Dispose()
            throw "Another runtime-gate action is active."
        }
        return $mutex
    }
    catch {
        if ($null -ne $mutex) { $mutex.Dispose() }
        throw
    }
}

function Exit-StateLock([System.Threading.Mutex]$Lock) {
    if ($null -eq $Lock) {
        return
    }
    try { $Lock.ReleaseMutex() } finally { $Lock.Dispose() }
}

function Invoke-WithSessionLock([string]$ManifestPath, [scriptblock]$Operation) {
    $lock = $null
    try {
        $lock = Enter-StateLock
        return & $Operation
    }
    finally {
        Exit-StateLock $lock
    }
}

function Get-OwnershipRoot([object]$Context) {
    if ((Test-IsAuthorizedFixture $Context) -and
        ![string]::IsNullOrWhiteSpace($env:FF7RP_RUNTIME_GATE_TEST_OWNER_ROOT)) {
        return Get-FullPath $env:FF7RP_RUNTIME_GATE_TEST_OWNER_ROOT (Get-Location).Path
    }
    return Join-Path $env:LOCALAPPDATA "FF7RPianoSongs/runtime-gates/owners"
}

function Get-OwnershipPath([object]$Context, [string]$ExecutableHash) {
    return Join-Path (Get-OwnershipRoot $Context) "$ExecutableHash.json"
}

function Assert-OwnershipAvailable([string]$OwnershipPath) {
    if (!(Test-Path -LiteralPath $OwnershipPath -PathType Leaf)) { return }
    $owner = Get-Content -LiteralPath $OwnershipPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace([string]$owner.manifestPath) -or
        !(Test-Path -LiteralPath ([string]$owner.manifestPath) -PathType Leaf)) {
        throw "Runtime-gate ownership is unresolved; preserve and inspect $OwnershipPath."
    }
    $manifest = Read-Manifest ([string]$owner.manifestPath)
    if ($TerminalStates -contains $manifest.state) {
        throw "A terminal runtime-gate ownership receipt remains; run Recover before another Prepare: $($owner.manifestPath)"
    }
    throw "An unresolved runtime-gate session owns this executable: $($owner.manifestPath)"
}

function Write-Ownership([object]$Manifest, [string]$ManifestPath) {
    Write-JsonAtomic ([ordered]@{
        schema = "ff7rpianosongs.runtime-gate-owner.v1"
        manifestPath = $ManifestPath
        gameRoot = [string]$Manifest.gameRoot
        executableSha256 = [string]$Manifest.executable.sha256
    }) ([string]$Manifest.paths.ownership)
}

function Ensure-Ownership([object]$Manifest, [string]$ManifestPath) {
    $ownershipPath = [string]$Manifest.paths.ownership
    if (!(Test-Path -LiteralPath $ownershipPath -PathType Leaf)) {
        Write-Ownership $Manifest $ManifestPath
        return
    }
    $owner = Get-Content -LiteralPath $ownershipPath -Raw | ConvertFrom-Json
    if ([string]$owner.manifestPath -ne $ManifestPath -or
        [string]$owner.executableSha256 -ne [string]$Manifest.executable.sha256) {
        throw "Runtime-gate ownership is held by another session: $ownershipPath"
    }
}

function Remove-Ownership([object]$Manifest, [string]$ManifestPath) {
    $ownershipPath = [string]$Manifest.paths.ownership
    if (!(Test-Path -LiteralPath $ownershipPath -PathType Leaf)) { return }
    $owner = Get-Content -LiteralPath $ownershipPath -Raw | ConvertFrom-Json
    if ([string]$owner.manifestPath -ne $ManifestPath) {
        throw "Refusing to remove ownership held by another session: $ownershipPath"
    }
    Remove-Item -LiteralPath $ownershipPath -Force
}

function Copy-Verified([string]$Source, [string]$Destination, [string]$ExpectedHash) {
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    if ((Get-Sha256 $Destination) -ne $ExpectedHash) {
        throw "Copied file failed SHA-256 verification: $Destination"
    }
}

function Remove-IfKnown([string]$Path, [string]$ExpectedHash) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    if ((Get-Sha256 $Path) -ne $ExpectedHash) {
        throw "Refusing to remove a file with an unknown hash: $Path"
    }
    Remove-Item -LiteralPath $Path -Force
}

function Get-VerifiedPriorSource([object]$Manifest) {
    $priorHash = [string]$Manifest.priorAsi.sha256
    foreach ($candidate in @(
        [string]$Manifest.paths.previousAsi,
        [string]$Manifest.priorAsi.backupPath)) {
        if (![string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf) -and
            (Get-Sha256 $candidate) -eq $priorHash) {
            return $candidate
        }
    }
    throw "The verified previous ASI and session backup are unavailable for rollback."
}

function Ensure-PreservedCopy(
    [string]$Source,
    [string]$Destination,
    [string]$ExpectedHash) {
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        if ((Get-Sha256 $Destination) -ne $ExpectedHash) {
            throw "A preservation artifact has an unexpected hash: $Destination"
        }
        return
    }
    Copy-Verified $Source $Destination $ExpectedHash
}

function Preserve-UnknownAsi(
    [string]$Source,
    [string]$SessionRoot,
    [string]$Hash) {
    $destination = Join-Path $SessionRoot "unknown-installed-$Hash.asi"
    Ensure-PreservedCopy $Source $destination $Hash
    return $destination
}

function Reconcile-FailedAsi(
    [object]$Manifest,
    [string]$ManifestPath,
    [switch]$QuarantineUnknown) {
    $failed = [string]$Manifest.paths.failedAsi
    if (!(Test-Path -LiteralPath $failed -PathType Leaf)) { return }
    $hash = Get-Sha256 $failed
    $sessionRoot = Split-Path -Parent $ManifestPath
    $stagedHash = [string]$Manifest.artifact.sha256
    $priorHash = [string]$Manifest.priorAsi.sha256
    if ($hash -eq $stagedHash) {
        Ensure-PreservedCopy $failed (Join-Path $sessionRoot "tested-or-failed.asi") $hash
    }
    elseif ($Manifest.priorAsi.existed -and $hash -eq $priorHash) {
        $null = Get-VerifiedPriorSource $Manifest
    }
    elseif ($QuarantineUnknown) {
        $null = Preserve-UnknownAsi $failed $sessionRoot $hash
    }
    else {
        Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
        throw "A failed transaction sibling has an unknown hash; use Recover to quarantine it: $failed"
    }
    Remove-IfKnown $failed $hash
}

function Reconcile-AsiTransactionFiles(
    [object]$Manifest,
    [string]$ManifestPath,
    [switch]$QuarantineUnknown) {
    Reconcile-FailedAsi $Manifest $ManifestPath -QuarantineUnknown:$QuarantineUnknown
    $stagedHash = [string]$Manifest.artifact.sha256
    $priorHash = [string]$Manifest.priorAsi.sha256
    $staging = [string]$Manifest.paths.stagingAsi
    if (Test-Path -LiteralPath $staging -PathType Leaf) {
        $hash = Get-Sha256 $staging
        if ($hash -eq $stagedHash) {
            Remove-IfKnown $staging $stagedHash
        }
        elseif ($QuarantineUnknown) {
            $null = Preserve-UnknownAsi $staging (Split-Path -Parent $ManifestPath) $hash
            Remove-IfKnown $staging $hash
        }
        else {
            Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
            throw "A staging transaction sibling has an unknown hash; use Recover to quarantine it: $staging"
        }
    }

    $previous = [string]$Manifest.paths.previousAsi
    if (Test-Path -LiteralPath $previous -PathType Leaf) {
        $hash = Get-Sha256 $previous
        if ($Manifest.priorAsi.existed -and $hash -eq $priorHash) {
            Remove-IfKnown $previous $priorHash
        }
        elseif ($QuarantineUnknown) {
            $null = Preserve-UnknownAsi $previous (Split-Path -Parent $ManifestPath) $hash
            Remove-IfKnown $previous $hash
        }
        else {
            Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
            throw "A previous transaction sibling has an unknown hash; use Recover to quarantine it: $previous"
        }
    }

    $restore = "$([string]$Manifest.paths.targetAsi).$([string]$Manifest.sessionId).restore"
    if (Test-Path -LiteralPath $restore -PathType Leaf) {
        $hash = Get-Sha256 $restore
        if ($Manifest.priorAsi.existed -and $hash -eq $priorHash) {
            Remove-IfKnown $restore $priorHash
        }
        elseif ($QuarantineUnknown) {
            $null = Preserve-UnknownAsi $restore (Split-Path -Parent $ManifestPath) $hash
            Remove-IfKnown $restore $hash
        }
        else {
            Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
            throw "An ASI restore sibling has an unknown hash; use Recover to quarantine it: $restore"
        }
    }
}

function Reconcile-DisplacedLog([object]$Manifest, [string]$ManifestPath) {
    $log = [string]$Manifest.paths.log
    $failed = "$log.$([string]$Manifest.sessionId).failed"
    if (!(Test-Path -LiteralPath $failed -PathType Leaf)) { return }
    $hash = Get-Sha256 $failed
    Ensure-PreservedCopy $failed (Join-Path (Split-Path -Parent $ManifestPath) "displaced-log-$hash.log") $hash
    Remove-IfKnown $failed $hash
}

function Restore-ArchivedLog([object]$Manifest, [string]$ManifestPath) {
    if ($null -eq $Manifest.priorLog) { return }
    $log = [string]$Manifest.paths.log
    $priorHash = [string]$Manifest.priorLog.sha256
    $archive = Join-Path (Split-Path -Parent $ManifestPath) "log-before.log"
    Reconcile-DisplacedLog $Manifest $ManifestPath
    $restore = "$log.$([string]$Manifest.sessionId).restore"
    if ((Test-Path -LiteralPath $log -PathType Leaf) -and
        (Get-Sha256 $log) -eq $priorHash) {
        Remove-IfKnown $restore $priorHash
        return
    }
    if (!(Test-Path -LiteralPath $archive -PathType Leaf) -or
        (Get-Sha256 $archive) -ne $priorHash) {
        throw "The archived pre-session log is unavailable or invalid: $archive"
    }

    if (Test-Path -LiteralPath $restore -PathType Leaf) {
        if ((Get-Sha256 $restore) -ne $priorHash) {
            throw "A log restore sibling has an unknown hash: $restore"
        }
    }
    else {
        Copy-Verified $archive $restore $priorHash
    }
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        [System.IO.File]::Replace(
            $restore,
            $log,
            "$log.$([string]$Manifest.sessionId).failed",
            $true)
    }
    else {
        [System.IO.File]::Move($restore, $log)
    }
    if ((Get-Sha256 $log) -ne $priorHash) {
        throw "Rollback did not restore the archived pre-session log."
    }
    Reconcile-DisplacedLog $Manifest $ManifestPath
}

function Assert-RollbackReconciled([object]$Manifest) {
    $target = [string]$Manifest.paths.targetAsi
    if ($Manifest.priorAsi.existed) {
        if (!(Test-Path -LiteralPath $target -PathType Leaf) -or
            (Get-Sha256 $target) -ne [string]$Manifest.priorAsi.sha256) {
            throw "Rollback reconciliation did not preserve the exact prior ASI."
        }
    }
    elseif (Test-Path -LiteralPath $target -PathType Leaf) {
        throw "Rollback reconciliation left an ASI from a clean-install session."
    }
    foreach ($path in @(
        [string]$Manifest.paths.stagingAsi,
        [string]$Manifest.paths.previousAsi,
        [string]$Manifest.paths.failedAsi,
        "$target.$([string]$Manifest.sessionId).restore")) {
        if (Test-Path -LiteralPath $path) {
            throw "Rollback reconciliation left a transaction sibling: $path"
        }
    }
    if ($null -ne $Manifest.priorLog) {
        $log = [string]$Manifest.paths.log
        if (!(Test-Path -LiteralPath $log -PathType Leaf) -or
            (Get-Sha256 $log) -ne [string]$Manifest.priorLog.sha256) {
            throw "Rollback reconciliation did not preserve the exact archived log."
        }
    }
}

function Invoke-Rollback(
    [object]$Manifest,
    [string]$ManifestPath,
    [string]$FinalState,
    [switch]$QuarantineUnknown) {
    Assert-GameStopped $Manifest
    $target = [string]$Manifest.paths.targetAsi
    $failedSibling = [string]$Manifest.paths.failedAsi
    $sessionRoot = Split-Path -Parent $ManifestPath
    $stagedHash = [string]$Manifest.artifact.sha256
    $priorHash = [string]$Manifest.priorAsi.sha256
    $targetHash = if (Test-Path -LiteralPath $target -PathType Leaf) { Get-Sha256 $target } else { "" }

    Add-Operation $Manifest "rollback-intent" $FinalState
    Save-Manifest $Manifest $ManifestPath "RollingBack"

    if ($targetHash -and $targetHash -ne $stagedHash -and
        (!$Manifest.priorAsi.existed -or $targetHash -ne $priorHash)) {
        if (!$QuarantineUnknown) {
            Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
            throw "Refusing rollback because the installed ASI has an unknown hash; use Recover to quarantine it."
        }
        $null = Preserve-UnknownAsi $target $sessionRoot $targetHash
        Add-Operation $Manifest "recover" "quarantined-unknown-installed-asi:$targetHash"
    }

    Reconcile-FailedAsi $Manifest $ManifestPath -QuarantineUnknown:$QuarantineUnknown

    if ($Manifest.priorAsi.existed) {
        if (!$targetHash -or $targetHash -ne $priorHash) {
            $priorSource = Get-VerifiedPriorSource $Manifest
            $restoreSource = $priorSource
            if ((Split-Path -Parent $priorSource) -ne (Split-Path -Parent $target)) {
                $restoreSource = "$target.$($Manifest.sessionId).restore"
                Copy-Verified $priorSource $restoreSource $priorHash
            }
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                [System.IO.File]::Replace($restoreSource, $target, $failedSibling, $true)
            }
            else {
                [System.IO.File]::Move($restoreSource, $target)
            }
            Invoke-TestHardTermination "AfterRollbackReplacement" $Manifest
            Reconcile-FailedAsi $Manifest $ManifestPath -QuarantineUnknown:$QuarantineUnknown
        }
        if ((Get-Sha256 $target) -ne $priorHash) {
            Save-Manifest $Manifest $ManifestPath "RecoveryRequired"
            throw "Rollback did not restore the previous ASI hash."
        }
    }
    elseif (Test-Path -LiteralPath $target -PathType Leaf) {
        $removalHash = Get-Sha256 $target
        [System.IO.File]::Move($target, $failedSibling)
        Invoke-TestHardTermination "AfterRollbackReplacement" $Manifest
        Reconcile-FailedAsi $Manifest $ManifestPath -QuarantineUnknown:$QuarantineUnknown
    }

    Reconcile-AsiTransactionFiles $Manifest $ManifestPath -QuarantineUnknown:$QuarantineUnknown
    Invoke-TestHardTermination "BeforePriorLogRestore" $Manifest
    Restore-ArchivedLog $Manifest $ManifestPath
    Assert-RollbackReconciled $Manifest
    Add-Operation $Manifest "rollback" $FinalState
    Save-Manifest $Manifest $ManifestPath $FinalState
    Remove-Ownership $Manifest $ManifestPath
}

function Invoke-Prepare {
    $repo = Get-FullPath $(if ($RepositoryRoot) { $RepositoryRoot } else { $DefaultRepositoryRoot }) (Get-Location).Path
    $package = if ($Mode -eq "Packaged") {
        Get-FullPath $(if ($PackageRoot) { $PackageRoot } else { Join-Path $repo "package" }) $repo
    }
    else { "" }
    $game = Get-FullPath $GameRoot (Get-Location).Path
    $stateRoot = Get-FullPath $(if ($StateDirectory) { $StateDirectory } else { Join-Path $env:LOCALAPPDATA "FF7RPianoSongs/runtime-gates" }) (Get-Location).Path
    $win64 = Join-Path $game "End/Binaries/Win64"
    $target = Join-Path $win64 $AsiName
    $log = Join-Path $win64 $LogName
    $executable = Join-Path $win64 "ff7rebirth_.exe"
    if (!(Test-Path -LiteralPath $win64 -PathType Container)) {
        throw "Game Win64 directory is missing: $win64"
    }
    $context = [pscustomobject]@{ repositoryRoot = $repo; gameRoot = $game }
    Assert-SafeStateDirectory $stateRoot $repo $game

    Assert-GameStopped $context
    $artifact = Assert-ArtifactIdentity $repo $package $Mode
    $executableIdentity = Assert-ExecutableIdentity $repo $executable
    $ownershipPath = Get-OwnershipPath $context ([string]$executableIdentity.sha256)

    if (!$PSCmdlet.ShouldProcess($target, "Install reviewed FF7RPianoSongs ASI transactionally")) {
        return [pscustomobject][ordered]@{
            action = "Prepare"
            mode = $Mode
            target = $target
            artifactSha256 = $artifact.sha256
            executableSha256 = $executableIdentity.sha256
            stateDirectory = $stateRoot
            ownership = $ownershipPath
        }
    }

    $lock = $null
    $manifestPath = $null
    try {
        $lock = Enter-StateLock
        Assert-OwnershipAvailable $ownershipPath
        New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null

        $sessionId = "{0}-{1}" -f [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ"), [guid]::NewGuid().ToString("N").Substring(0, 12)
        $sessionRoot = Join-Path $stateRoot $sessionId
        $manifestPath = Join-Path $sessionRoot $ManifestName
        $staging = Join-Path $win64 ".$AsiName.$sessionId.staging"
        $previous = Join-Path $win64 ".$AsiName.$sessionId.previous"
        $failed = Join-Path $win64 ".$AsiName.$sessionId.failed"
        $prior = Get-FileRecord $target
        $priorRecord = [ordered]@{
            existed = $null -ne $prior
            sha256 = if ($null -ne $prior) { $prior.sha256 } else { "" }
            length = if ($null -ne $prior) { $prior.length } else { 0 }
            backupPath = if ($null -ne $prior) { Join-Path $sessionRoot "installed-before.asi" } else { "" }
        }
        $selectedSidecar = if ($SongId) { Join-Path $win64 "Music/$SongId/.cache/song.mabf.bin" } else { "" }
        $manifest = [pscustomobject][ordered]@{
            schema = $ManifestSchema
            sessionId = $sessionId
            state = "Preparing"
            createdUtc = [DateTime]::UtcNow.ToString("o")
            updatedUtc = [DateTime]::UtcNow.ToString("o")
            processName = $ProcessName
            mode = $Mode
            repositoryRoot = $repo
            gameRoot = $game
            scenario = $Scenario
            songId = $SongId
            artifact = $artifact
            executable = $executableIdentity
            priorAsi = $priorRecord
            priorLog = Get-FileRecord $log
            initialSidecar = if ($selectedSidecar) { Get-FileRecord $selectedSidecar } else { $null }
            paths = [ordered]@{
                targetAsi = $target
                log = $log
                selectedSidecar = $selectedSidecar
                stagingAsi = $staging
                previousAsi = $previous
                failedAsi = $failed
                ownership = $ownershipPath
            }
            evidence = $null
            operations = @()
        }

        New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null
        Add-Operation $manifest "prepare" "session-created"
        Save-Manifest $manifest $manifestPath "Preparing"
        Write-Ownership $manifest $manifestPath
        Invoke-TestHardTermination "AfterOwnershipReceipt" $manifest
        if ($priorRecord.existed) {
            Copy-Verified $target $priorRecord.backupPath $priorRecord.sha256
        }
        if ($null -ne $manifest.priorLog) {
            Copy-Verified $log (Join-Path $sessionRoot "log-before.log") $manifest.priorLog.sha256
            Remove-IfKnown $log $manifest.priorLog.sha256
        }
        $manifest | Add-Member -NotePropertyName runBoundaryUtc -NotePropertyValue ([DateTime]::UtcNow.ToString("o"))
        Save-Manifest $manifest $manifestPath "Preparing"

        Invoke-TestFailure "BeforeStagingCopy" $manifest
        Copy-Verified $artifact.sourcePath $staging $artifact.sha256
        Invoke-TestFailure "AfterStagingCopy" $manifest
        Assert-GameStopped $manifest
        Add-Operation $manifest "publish" "process-gate-passed"
        Save-Manifest $manifest $manifestPath "Publishing"
        if ($priorRecord.existed) {
            [System.IO.File]::Replace($staging, $target, $previous, $true)
        }
        else {
            [System.IO.File]::Move($staging, $target)
        }
        Invoke-TestFailure "AfterPublish" $manifest
        if ((Get-Sha256 $target) -ne $artifact.sha256) {
            throw "Installed ASI does not match the reviewed staged artifact."
        }
        Add-Operation $manifest "publish" "installed-and-verified"
        Save-Manifest $manifest $manifestPath "AwaitingUserRun"
        return $manifestPath
    }
    catch {
        if ($null -ne $manifestPath -and (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            $failedManifest = Read-Manifest $manifestPath
            try {
                Invoke-Rollback $failedManifest $manifestPath "RolledBack"
            }
            catch {
                throw "Prepare failed and automatic rollback requires recovery. Manifest: $manifestPath. Error: $($_.Exception.Message)"
            }
        }
        throw
    }
    finally {
        Exit-StateLock $lock
    }
}

function ConvertTo-SanitizedExcerptLine(
    [string]$Line,
    [string]$GamePattern,
    [string]$RepositoryPattern) {
    $sanitized = $Line -replace $GamePattern, '<game-root>' -replace $RepositoryPattern, '<repository-root>'
    $sanitized = $sanitized -replace '(?i)\b([a-z0-9_]*path)=(?:"(?:[a-z]:[\\/]|/)[^"]*"|(?:[a-z]:[\\/]|/).*?(?=\s+[a-z0-9_]+=|$))', '$1="<redacted-path>"'
    $sanitized = $sanitized -replace '(?i)[a-z]:[\\/]Users[\\/][^\\/\s"]+', '<user-home>'
    $sanitized = $sanitized -replace '(?i)/home/[^/\s"]+', '<user-home>'
    $hexAssignment = [regex]::new('(?i)\b([a-z0-9_]+)=0x[0-9a-f]+\b')
    $sanitized = $hexAssignment.Replace($sanitized, [System.Text.RegularExpressions.MatchEvaluator]{
        param($match)
        $name = $match.Groups[1].Value
        if ($SafeExcerptHexFields -contains $name.ToLowerInvariant()) {
            return $match.Value
        }
        return "$name=<redacted>"
    })
    if ($sanitized.Length -gt $MaxExcerptLineCharacters) {
        $sanitized = $sanitized.Substring(0, $MaxExcerptLineCharacters) + '<truncated>'
    }
    return $sanitized
}

function Write-Utf8LfFile([string[]]$Lines, [string]$Path) {
    $text = if ($Lines.Count -eq 0) { "" } else { ($Lines -join "`n") + "`n" }
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($text)
    if ($bytes.Length -gt $MaxExcerptUtf8Bytes) {
        throw "Sanitized excerpt exceeds its exact UTF-8 byte cap."
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Get-LogInspection([string]$Path, [object]$Manifest) {
    $gamePattern = [regex]::Escape([string]$Manifest.gameRoot)
    $repoPattern = [regex]::Escape([string]$Manifest.repositoryRoot)
    $sidecarPattern = if ($Manifest.songId) {
        '\[audio_sead_sidecar\].*id="' + [regex]::Escape([string]$Manifest.songId) + '".*status=ready.*mabf_size=0x(?!0+\b)[0-9a-fA-F]+'
    }
    else {
        '\[audio_sead_sidecar\].*status=ready.*mabf_size=0x(?!0+\b)[0-9a-fA-F]+'
    }
    $excerpt = [System.Collections.Generic.List[string]]::new()
    $excerptByteCounts = [System.Collections.Generic.List[int]]::new()
    $excerptUtf8Bytes = 0
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $indices = @{ version = -1; registry = -1; route = -1; hooks = -1; initialized = -1; sidecar = -1; settled = -1 }
    $terminal = 0
    $lineIndex = -1
    $reader = [System.IO.StreamReader]::new($Path, $true)
    try {
        while (!$reader.EndOfStream) {
            $lineIndex++
            $line = $reader.ReadLine()
            if ($indices.version -lt 0 -and
                ($line -match '\[startup\] product=\S+\s+version=\S+' -or
                 $line -match '\[startup\] version=')) { $indices.version = $lineIndex }
            if ($indices.registry -lt 0 -and $line -match '\[song\] status=registry_ready count=') { $indices.registry = $lineIndex }
            if ($indices.route -lt 0 -and $line -match '\[audio_sead\] status=prepared_playsetup_route') { $indices.route = $lineIndex }
            if ($indices.hooks -lt 0 -and $line -match '\[hooks\] status=installed_release_hooks') { $indices.hooks = $lineIndex }
            if ($indices.initialized -lt 0 -and $line -match '\[startup\] status=initialized mode=core_skeleton') { $indices.initialized = $lineIndex }
            if ($indices.sidecar -lt 0 -and $line -match $sidecarPattern) { $indices.sidecar = $lineIndex }
            if ($indices.settled -lt 0 -and $line -match '\[song\] status=repository_settled\b') { $indices.settled = $lineIndex }
            if ($line -match '\[startup\] status=disabled_' -or
                $line -match '\[song\] status=repository_failed\b') { $terminal++ }
            if ($line -notmatch '\[(startup|song|song_load|audio_sead|audio_sead_sidecar|hooks|runtime)\]') { continue }

            $sanitized = ConvertTo-SanitizedExcerptLine $line $gamePattern $repoPattern
            $lineBytes = $utf8.GetByteCount($sanitized) + 1
            $excerpt.Add($sanitized)
            $excerptByteCounts.Add($lineBytes)
            $excerptUtf8Bytes += $lineBytes
            while ($excerpt.Count -gt $MaxExcerptLines -or
                $excerptUtf8Bytes -gt $MaxExcerptUtf8Bytes) {
                $excerptUtf8Bytes -= $excerptByteCounts[0]
                $excerptByteCounts.RemoveAt(0)
                $excerpt.RemoveAt(0)
            }
        }
    }
    finally {
        $reader.Dispose()
    }
    $legacyOrdered = $indices.version -ge 0 -and $indices.registry -gt $indices.version -and
        $indices.route -gt $indices.registry -and $indices.hooks -gt $indices.route -and
        $indices.initialized -gt $indices.hooks
    $progressiveOrdered = $indices.version -ge 0 -and $indices.route -gt $indices.version -and
        $indices.hooks -gt $indices.route -and $indices.initialized -gt $indices.hooks -and
        $indices.settled -gt $indices.hooks
    $startupOrdering = if ($legacyOrdered) { "Legacy" }
        elseif ($progressiveOrdered) { "Progressive" }
        else { "None" }
    $orderedStartup = $legacyOrdered -or $progressiveOrdered
    $sidecarOrdered = $indices.sidecar -gt $indices.version -and
        (!$progressiveOrdered -or $indices.sidecar -gt $indices.hooks)
    $assessment = if ($terminal -ne 0) { "Failed" }
        elseif ($orderedStartup -and $sidecarOrdered) { "Passed" }
        elseif ($indices.version -lt 0) { "NotObservable" }
        else { "NotObserved" }
    return [pscustomobject][ordered]@{
        assessment = $assessment
        orderedStartupMarkers = $orderedStartup
        startupOrdering = $startupOrdering
        legacyStartupMarkers = $legacyOrdered
        progressiveStartupMarkers = $progressiveOrdered
        repositorySettled = $indices.settled -ge 0
        sidecarReady = $indices.sidecar -ge 0
        terminalDisabledMarkers = $terminal
        excerpt = @($excerpt)
    }
}

function Invoke-Collect([string]$ManifestPath) {
    $manifest = Read-Manifest $ManifestPath
    Assert-GameStopped $manifest
    if ($manifest.state -ne "AwaitingUserRun") {
        throw "Collect requires an AwaitingUserRun session, got $($manifest.state)."
    }
    $targetHash = Get-Sha256 ([string]$manifest.paths.targetAsi)
    if ($targetHash -ne [string]$manifest.artifact.sha256) {
        throw "Installed ASI changed before evidence collection; refusing to attribute the run."
    }
    $logPath = [string]$manifest.paths.log
    if (!(Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "The post-run FF7RPianoSongs log is missing."
    }
    $liveLog = Get-FileRecord $logPath
    if ($null -ne $manifest.priorLog -and $liveLog.sha256 -eq [string]$manifest.priorLog.sha256) {
        throw "The live log is identical to the pre-launch log; refusing stale evidence."
    }
    if ((Get-UtcDateTime $liveLog.lastWriteTimeUtc) -lt
        (Get-UtcDateTime $manifest.runBoundaryUtc)) {
        throw "The live log predates the prepared runtime boundary."
    }
    if (!$PSCmdlet.ShouldProcess($ManifestPath, "Capture runtime-gate evidence")) {
        return $ManifestPath
    }

    $sessionRoot = Split-Path -Parent $ManifestPath
    $postLog = Join-Path $sessionRoot "log-after.log"
    $postHash = Get-Sha256 $logPath
    Copy-Verified $logPath $postLog $postHash
    $inspection = Get-LogInspection $postLog $manifest
    $excerptPath = Join-Path $sessionRoot "excerpt.log"
    Write-Utf8LfFile @($inspection.excerpt) $excerptPath
    $evidencePath = Join-Path $sessionRoot "evidence.json"
    $evidence = [pscustomobject][ordered]@{
        schema = "ff7rpianosongs.runtime-evidence.v1"
        sessionId = [string]$manifest.sessionId
        capturedUtc = [DateTime]::UtcNow.ToString("o")
        scenario = [string]$manifest.scenario
        songId = [string]$manifest.songId
        assessment = $inspection.assessment
        orderedStartupMarkers = $inspection.orderedStartupMarkers
        startupOrdering = $inspection.startupOrdering
        legacyStartupMarkers = $inspection.legacyStartupMarkers
        progressiveStartupMarkers = $inspection.progressiveStartupMarkers
        repositorySettled = $inspection.repositorySettled
        sidecarReady = $inspection.sidecarReady
        terminalDisabledMarkers = $inspection.terminalDisabledMarkers
        executable = $manifest.executable
        installedAsiSha256 = $targetHash
        postLog = Get-FileRecord $postLog
        selectedSidecar = if ($manifest.paths.selectedSidecar) { Get-FileRecord ([string]$manifest.paths.selectedSidecar) } else { $null }
        excerptPath = $excerptPath
        notTested = @("list/UI/chart mechanics", "complete audio lifecycle", "native-HCA parity")
    }
    Write-JsonAtomic $evidence $evidencePath
    $manifest.evidence = [ordered]@{
        path = $evidencePath
        assessment = $inspection.assessment
        postLogPath = $postLog
        postLogSha256 = $postHash
        excerptPath = $excerptPath
    }
    Add-Operation $manifest "collect" $inspection.assessment
    Save-Manifest $manifest $ManifestPath "Collected"
    return $ManifestPath
}

function Invoke-Finalize([string]$ManifestPath) {
    $manifest = Read-Manifest $ManifestPath
    Assert-GameStopped $manifest
    if ($manifest.state -notin @("AwaitingUserRun", "Collected")) {
        throw "Finalize requires an AwaitingUserRun or Collected session, got $($manifest.state)."
    }
    if ($Disposition -eq "Keep" -and $manifest.state -ne "Collected") {
        throw "Keep requires immutable collected evidence. Use Rollback or Uninstall before collection."
    }
    if (!$PSCmdlet.ShouldProcess($manifest.paths.targetAsi, "Finalize runtime gate as $Disposition")) {
        return $ManifestPath
    }
    switch ($Disposition) {
        "Rollback" {
            Invoke-Rollback $manifest $ManifestPath "RolledBack"
        }
        "Uninstall" {
            if ($manifest.priorAsi.existed) {
                throw "Uninstall is only valid for a clean install. Use Rollback to restore the prior ASI."
            }
            Invoke-Rollback $manifest $ManifestPath "Uninstalled"
        }
        "Keep" {
            $targetHash = Get-Sha256 ([string]$manifest.paths.targetAsi)
            if ($targetHash -ne [string]$manifest.artifact.sha256) {
                throw "Refusing Keep because the installed ASI has an unknown hash."
            }
            Add-Operation $manifest "finalize-intent" "keep-reviewed-artifact"
            Save-Manifest $manifest $ManifestPath "FinalizingKeep"
            $previous = [string]$manifest.paths.previousAsi
            if ($manifest.priorAsi.existed) {
                Remove-IfKnown $previous ([string]$manifest.priorAsi.sha256)
            }
            Invoke-TestFailure "AfterPreviousDelete" $manifest
            Reconcile-AsiTransactionFiles $manifest $ManifestPath
            Add-Operation $manifest "finalize" "kept-reviewed-artifact"
            Save-Manifest $manifest $ManifestPath "Accepted"
            Remove-Ownership $manifest $ManifestPath
        }
    }
    return $ManifestPath
}

function Invoke-AcceptedReconciliation([object]$Manifest, [string]$ManifestPath) {
    Assert-GameStopped $Manifest
    $target = [string]$Manifest.paths.targetAsi
    if (!(Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-Sha256 $target) -ne [string]$Manifest.artifact.sha256) {
        throw "Accepted-session recovery found an ASI other than the reviewed artifact."
    }
    Add-Operation $Manifest "recover-intent" "reconcile-accepted-terminal-state"
    Save-Manifest $Manifest $ManifestPath "Accepted"
    Reconcile-AsiTransactionFiles $Manifest $ManifestPath -QuarantineUnknown
    foreach ($path in @(
        [string]$Manifest.paths.stagingAsi,
        [string]$Manifest.paths.previousAsi,
        [string]$Manifest.paths.failedAsi,
        "$target.$([string]$Manifest.sessionId).restore")) {
        if (Test-Path -LiteralPath $path) {
            throw "Accepted-session reconciliation left a transaction sibling: $path"
        }
    }
    Add-Operation $Manifest "recover" "accepted-terminal-state-reconciled"
    Save-Manifest $Manifest $ManifestPath "Accepted"
    Remove-Ownership $Manifest $ManifestPath
}

function Invoke-Recover([string]$ManifestPath) {
    $manifest = Read-Manifest $ManifestPath
    if (!$PSCmdlet.ShouldProcess($manifest.paths.targetAsi, "Recover runtime-gate session by verified hashes")) {
        return $ManifestPath
    }
    Ensure-Ownership $manifest $ManifestPath
    if ($manifest.state -eq "Accepted") {
        Invoke-AcceptedReconciliation $manifest $ManifestPath
    }
    elseif ($TerminalStates -contains $manifest.state) {
        Invoke-Rollback $manifest $ManifestPath ([string]$manifest.state) -QuarantineUnknown
    }
    else {
        Invoke-Rollback $manifest $ManifestPath "Recovered" -QuarantineUnknown
    }
    return $ManifestPath
}

if ($Action -eq "Prepare") {
    if ([string]::IsNullOrWhiteSpace($GameRoot)) {
        throw "-GameRoot is required for Prepare."
    }
    Invoke-Prepare
    return
}

if ($PSBoundParameters.ContainsKey("Mode")) {
    throw "-Mode is valid only for Prepare."
}

$resolvedManifest = Resolve-ManifestPath $Session
switch ($Action) {
    "Collect" { Invoke-WithSessionLock $resolvedManifest { Invoke-Collect $resolvedManifest } }
    "Finalize" { Invoke-WithSessionLock $resolvedManifest { Invoke-Finalize $resolvedManifest } }
    "Recover" { Invoke-WithSessionLock $resolvedManifest { Invoke-Recover $resolvedManifest } }
    "Status" {
        $status = Read-Manifest $resolvedManifest
        [pscustomobject][ordered]@{
            session = $resolvedManifest
            state = $status.state
            mode = if ($null -ne $status.PSObject.Properties["mode"]) { [string]$status.mode } else { "Packaged" }
            scenario = $status.scenario
            songId = $status.songId
            artifactSha256 = $status.artifact.sha256
            evidence = $status.evidence
        } | ConvertTo-Json -Depth 8
    }
}
