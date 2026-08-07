[CmdletBinding()]
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"

$allowed = @(
    "src/tests/fixtures/encoded_audio/fixture.flac",
    "src/tests/fixtures/encoded_audio/fixture.mp3",
    "src/tests/fixtures/encoded_audio/source.wav"
)
$expectedSha256 = @{
    "src/tests/fixtures/encoded_audio/source.wav" = "ee81afb67e3bb0290ed862a3d744ab89811809f85d6d5586ec5731f8831c5bd7"
    "src/tests/fixtures/encoded_audio/fixture.mp3" = "1c148d618841e1548b6f3b196d11e7ce70f4e57ed72ffe63a8c2102eac1c46f0"
    "src/tests/fixtures/encoded_audio/fixture.flac" = "2bc57379745761fc488332a39501b42b26e25729d9f969cb781dedd9e7c22ba3"
}
$binaryPattern = '\.(mid|midi|mp3|flac|wav|bin|asi|dll|pdb|zip)$'
$paths = @(& git -C $Root ls-files --cached --others --exclude-standard -- .)
if ($LASTEXITCODE -ne 0) { throw "git source-binary inventory failed" }
$binaries = @(
    $paths |
        ForEach-Object { $_ -replace '\\', '/' } |
        Where-Object { $_ -match $binaryPattern } |
        Sort-Object
)
if (@(Compare-Object $allowed $binaries).Count -ne 0) {
    throw "Source binary inventory must contain only the audited encoded-audio fixtures; actual: $($binaries -join ', ')"
}
foreach ($relative in $allowed) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Root $relative)).Hash.ToLowerInvariant()
    if ($actual -ne $expectedSha256[$relative]) { throw "Audited fixture hash changed: $relative" }
}
Write-Output "Tracked binary audit passed: audited synthetic WAV/MP3/FLAC fixtures only."
