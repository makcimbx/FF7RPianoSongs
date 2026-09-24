param(
    [Parameter(Mandatory=$true)][string]$OfflineTool,
    [Parameter(Mandatory=$true)][string]$RuntimeTool,
    [Parameter(Mandatory=$true)][string]$FixtureRoot
)

$ErrorActionPreference = 'Stop'
$sourceWav = Join-Path $PSScriptRoot '../src/tests/fixtures/encoded_audio/source.wav'
foreach ($tool in @($OfflineTool, $RuntimeTool)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing tool: $tool" }
}
if (-not (Test-Path -LiteralPath $sourceWav -PathType Leaf)) { throw "Missing source WAV: $sourceWav" }

# Format-0 MIDI, 480 ticks per beat, one C4 quarter-note at 120 BPM.
[byte[]]$midi = @(
    0x4d,0x54,0x68,0x64, 0,0,0,6, 0,0, 0,1, 1,0xe0,
    0x4d,0x54,0x72,0x6b, 0,0,0,0x14,
    0,0xff,0x51,3,7,0xa1,0x20,
    0,0x90,0x3c,0x64,
    0x83,0x60,0x80,0x3c,0,
    0,0xff,0x2f,0
)
$authored = '{"schema":"ff7rpianosongs.song.v2","title":"Parity Authored","bpm":120,"loudness_normalization":false,"notes":[{"beat":0,"duration_beats":1,"pitch":"C4"}]}'
$generated = '{"schema":"ff7rpianosongs.song.v2","title":"Parity MIDI","midi_audio_alignment_seconds":0.0,"midi_audio_offset_seconds":0.0,"midi_minimum_lead_in_seconds":0.0,"loudness_normalization":false}'

$observations = @{}
foreach ($kind in @('authored', 'midi')) {
    foreach ($configuration in @('offline', 'runtime')) {
        $tool = if ($configuration -eq 'offline') { $OfflineTool } else { $RuntimeTool }
        # Runtime-cache payloads contain absolute artifact paths, so rebuild at
        # the same isolated path for each configuration, never on a warm cache.
        $song = Join-Path $FixtureRoot "$kind/song"
        if (Test-Path -LiteralPath $song) { Remove-Item -LiteralPath $song -Recurse -Force }
        New-Item -ItemType Directory -Path $song -Force | Out-Null
        Copy-Item -LiteralPath $sourceWav -Destination (Join-Path $song 'song.wav')
        [System.IO.File]::WriteAllText((Join-Path $song 'song.json'),
            $(if ($kind -eq 'authored') { $authored } else { $generated }),
            [System.Text.UTF8Encoding]::new($false))
        if ($kind -eq 'midi') {
            [System.IO.File]::WriteAllBytes((Join-Path $song 'song.mid'), $midi)
        }
        $log = & $tool $song 2>&1
        if ($LASTEXITCODE -ne 0 -or ($log -join "`n") -notmatch 'cache_source=generated') {
            throw "$kind/$configuration cold build failed: $($log -join "`n")"
        }
        $policy = @($log | Where-Object { $_ -match '^policy accepted_rows=' })
        if ($policy.Count -ne 1) { throw "$kind/$configuration did not report one policy snapshot" }
        $manifest = [System.IO.File]::ReadAllText((Join-Path $song '.cache/manifest.json'))
        if ($manifest -notmatch '(?m)^cache_key=([0-9a-fA-F]+)') {
            throw "$kind/$configuration manifest lacks a cache key"
        }
        $observations["$kind/$configuration"] = [pscustomobject]@{
            Key = $Matches[1]
            Policy = $policy[0]
            Runtime = (Get-FileHash -LiteralPath (Join-Path $song '.cache/runtime.bin') -Algorithm SHA256).Hash
            Mabf = (Get-FileHash -LiteralPath (Join-Path $song '.cache/song.mabf.bin') -Algorithm SHA256).Hash
        }
    }
    $left = $observations["$kind/offline"]
    $right = $observations["$kind/runtime"]
    if ($left.Key -ne $right.Key -or $left.Policy -ne $right.Policy -or
        $left.Runtime -ne $right.Runtime -or $left.Mabf -ne $right.Mabf) {
        throw "$kind cross-configuration artifact mismatch: offline=$($left | ConvertTo-Json -Compress) runtime=$($right | ConvertTo-Json -Compress)"
    }
    "${kind}: cache_key=$($left.Key) $($left.Policy) runtime_sha256=$($left.Runtime) mabf_sha256=$($left.Mabf) byte_parity=passed"
}
