$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot "build_provenance.ps1")

function Set-FixtureFile {
    param(
        [string]$Root,
        [string]$RelativePath,
        [string]$Contents
    )

    $path = Join-Path $Root $RelativePath
    $parent = Split-Path -Parent $path
    if (!(Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($path, $Contents, [System.Text.UTF8Encoding]::new($false))
}

function Write-FixtureRecord {
    param([string]$Path, [object]$Record)

    $json = $Record | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function New-ProvenanceFixture {
    param([string]$Root)

    New-Item -ItemType Directory -Path $Root | Out-Null
    foreach ($file in ([ordered]@{
        "CMakeLists.txt" = "fixture cmake input`n"
        "build.ps1" = "fixture build input`n"
        "release.json" = "{}`n"
        "cmake/apply_midifile_patch.cmake" = "# fixture patch driver`n"
        "cmake/midifile-running-status.patch" = "fixture dependency patch`n"
        "cmake/release_identity.generated.h.in" = "fixture template`n"
        "src/game/rva_catalog.json" = "{}`n"
        "src/game/chord_voicing_bridge.asm" = "; fixture production adapter`n"
        "src/game/chord_voicing_bridge_harness.asm" = "; excluded test adapter`n"
        "tools/generate_rva_catalog.py" = "# fixture generator input`n"
        "src/pipeline/source.cpp" = "// fixture source input`n"
        "src/pipeline/source.hpp" = "// fixture header input`n"
        "src/tests/excluded.cpp" = "// excluded test input`n"
        "src/tools/excluded.cpp" = "// excluded tool input`n"
        "bin/Release/FF7RPianoSongs.dll" = "fixture dll`n"
        "compiler.exe" = "fixture compiler`n"
        "build/CMakeCache.txt" = "fixture cache`n"
        "build/generated/release_identity.generated.h" = "fixture generated release`n"
    }).GetEnumerator()) {
        Set-FixtureFile -Root $Root -RelativePath $file.Key -Contents $file.Value
    }

    $inputs = @(Get-ProductionInputRelativePaths $Root)
    foreach ($required in @("cmake/apply_midifile_patch.cmake", "cmake/midifile-running-status.patch",
            "src/game/chord_voicing_bridge.asm")) {
        if ($inputs -cnotcontains $required) {
            throw "Production input inventory omitted required input: $required"
        }
    }
    if ($inputs -ccontains "src/game/chord_voicing_bridge_harness.asm") {
        throw "Production input inventory included the test-only MASM harness"
    }
    $inputRecords = @($inputs | ForEach-Object {
        [pscustomobject][ordered]@{
            path = $_
            sha256 = Get-FileSha256 (Join-Path $Root $_)
        }
    })
    $inputIdentity = ($inputRecords | ForEach-Object { "$($_.path)`t$($_.sha256)`n" }) -join ""
    $dllPath = Join-Path $Root "bin/Release/FF7RPianoSongs.dll"
    $compilerPath = Join-Path $Root "compiler.exe"
    $cmakePath = (Get-Command pwsh -CommandType Application -ErrorAction Stop).Source
    $recordPath = Join-Path $Root "FF7RPianoSongs.provenance.json"
    $record = [pscustomobject][ordered]@{
        schema = "ff7rpianosongs.build-provenance.v3"
        configuration = "Release"
        dll = [pscustomobject][ordered]@{
            path = "bin/Release/FF7RPianoSongs.dll"
            sha256 = Get-FileSha256 $dllPath
        }
        toolchain = [pscustomobject][ordered]@{
            visualStudioInstallationPath = "fixture"
            compilerPath = $compilerPath
            compilerVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($compilerPath).FileVersion
            compilerSha256 = Get-FileSha256 $compilerPath
            cmakePath = $cmakePath
            cmakeVersion = (& $cmakePath --version | Select-Object -First 1)
            cmakeSha256 = Get-FileSha256 $cmakePath
            generator = "fixture"
            generatorPlatform = "x64"
            generatorToolset = "fixture"
        }
        cmake = [pscustomobject][ordered]@{
            cachePath = "CMakeCache.txt"
            cacheSha256 = Get-FileSha256 (Join-Path $Root "build/CMakeCache.txt")
        }
        releaseIdentity = [pscustomobject][ordered]@{
            authorityPath = "release.json"
            authoritySha256 = Get-FileSha256 (Join-Path $Root "release.json")
            catalogId = "ff7rebirth-steam-win64-6a16ced2"
            generatedHeaderPath = "generated/release_identity.generated.h"
            generatedHeaderSha256 = Get-FileSha256 (Join-Path $Root "build/generated/release_identity.generated.h")
        }
        productionInputs = $inputRecords
        productionInputSetSha256 = Get-TextSha256 $inputIdentity
    }
    Write-FixtureRecord -Path $recordPath -Record $record

    return [pscustomobject]@{
        Root = $Root
        DllPath = $dllPath
        RecordPath = $recordPath
        CacheRoot = Join-Path $Root "build"
        ExpectedInputs = $inputs
        Record = $record
    }
}

function Invoke-FixtureValidation {
    param([object]$Fixture)

    Assert-BuildProvenance -InputRoot $Fixture.Root -DllPath $Fixture.DllPath `
        -RecordPath $Fixture.RecordPath -ExpectedConfiguration "Release" `
        -ExpectedInputs $Fixture.ExpectedInputs -CacheRoot $Fixture.CacheRoot
}

$adverseCases = @(
    [pscustomobject]@{
        Name = "extra top-level field"; ExpectedMessage = "Build provenance fields must be exactly"
        Mutate = { param($fixture, $record) $record | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "missing top-level field"; ExpectedMessage = "Build provenance fields must be exactly"
        Mutate = { param($fixture, $record) $record.PSObject.Properties.Remove("configuration") }
    },
    [pscustomobject]@{
        Name = "extra DLL field"; ExpectedMessage = "Build provenance DLL fields must be exactly"
        Mutate = { param($fixture, $record) $record.dll | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "missing DLL field"; ExpectedMessage = "Build provenance DLL fields must be exactly"
        Mutate = { param($fixture, $record) $record.dll.PSObject.Properties.Remove("sha256") }
    },
    [pscustomobject]@{
        Name = "extra toolchain field"; ExpectedMessage = "Build provenance toolchain fields must be exactly"
        Mutate = { param($fixture, $record) $record.toolchain | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "missing toolchain field"; ExpectedMessage = "Build provenance toolchain fields must be exactly"
        Mutate = { param($fixture, $record) $record.toolchain.PSObject.Properties.Remove("generatorToolset") }
    },
    [pscustomobject]@{
        Name = "extra CMake identity field"; ExpectedMessage = "Build provenance CMake identity fields must be exactly"
        Mutate = { param($fixture, $record) $record.cmake | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "missing CMake identity field"; ExpectedMessage = "Build provenance CMake identity fields must be exactly"
        Mutate = { param($fixture, $record) $record.cmake.PSObject.Properties.Remove("cacheSha256") }
    },
    [pscustomobject]@{
        Name = "release authority hash drift"; ExpectedMessage = "release authority or generated header"
        Mutate = { param($fixture, $record) $record.releaseIdentity.authoritySha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "generated release header hash drift"; ExpectedMessage = "release authority or generated header"
        Mutate = { param($fixture, $record) $record.releaseIdentity.generatedHeaderSha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "missing release identity game build"
        ExpectedMessage = "Build provenance release identity fields must be exactly"
        Mutate = { param($fixture, $record) $record.releaseIdentity.PSObject.Properties.Remove("catalogId") }
    },
    [pscustomobject]@{
        Name = "extra release identity field"
        ExpectedMessage = "Build provenance release identity fields must be exactly"
        Mutate = { param($fixture, $record) $record.releaseIdentity | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "malformed release identity game build"
        ExpectedMessage = "does not name a well-formed game build"
        Mutate = { param($fixture, $record) $record.releaseIdentity.catalogId = "FF7Rebirth Steam" }
    },
    [pscustomobject]@{
        Name = "non-string release identity game build"
        ExpectedMessage = "does not name a well-formed game build"
        Mutate = { param($fixture, $record) $record.releaseIdentity.catalogId = 39 }
    },
    [pscustomobject]@{
        Name = "extra production input field"; ExpectedMessage = "Production input fields must be exactly"
        Mutate = { param($fixture, $record) $record.productionInputs[0] | Add-Member unexpected $true }
    },
    [pscustomobject]@{
        Name = "missing production input field"; ExpectedMessage = "Production input fields must be exactly"
        Mutate = { param($fixture, $record) $record.productionInputs[0].PSObject.Properties.Remove("sha256") }
    },
    [pscustomobject]@{
        Name = "schema mismatch"; ExpectedMessage = "schema or configuration does not match"
        Mutate = { param($fixture, $record) $record.schema = "ff7rpianosongs.build-provenance.v0" }
    },
    [pscustomobject]@{
        Name = "non-Release configuration"; ExpectedMessage = "schema or configuration does not match"
        Mutate = { param($fixture, $record) $record.configuration = "Debug" }
    },
    [pscustomobject]@{
        Name = "DLL path drift"; ExpectedMessage = "Built DLL does not match its provenance record"
        Mutate = { param($fixture, $record) $record.dll.path = "bin/Debug/FF7RPianoSongs.dll" }
    },
    [pscustomobject]@{
        Name = "DLL hash drift"; ExpectedMessage = "Built DLL does not match its provenance record"
        Mutate = { param($fixture, $record) $record.dll.sha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "DLL source drift"; ExpectedMessage = "Built DLL does not match its provenance record"
        Mutate = { param($fixture, $record) [System.IO.File]::AppendAllText($fixture.DllPath, "drift") }
    },
    [pscustomobject]@{
        Name = "compiler executable drift"; ExpectedMessage = "Current compiler executable does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.compilerPath = Join-Path $fixture.Root "missing-compiler.exe" }
    },
    [pscustomobject]@{
        Name = "compiler hash drift"; ExpectedMessage = "Current compiler executable does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.compilerSha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "compiler version drift"; ExpectedMessage = "Current compiler version does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.compilerVersion = "fixture-version-drift" }
    },
    [pscustomobject]@{
        Name = "CMake executable drift"; ExpectedMessage = "Current CMake executable does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.cmakePath = Join-Path $fixture.Root "missing-cmake.exe" }
    },
    [pscustomobject]@{
        Name = "CMake hash drift"; ExpectedMessage = "Current CMake executable does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.cmakeSha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "CMake version drift"; ExpectedMessage = "Current CMake version does not match build provenance"
        Mutate = { param($fixture, $record) $record.toolchain.cmakeVersion = "fixture-version-drift" }
    },
    [pscustomobject]@{
        Name = "non-normalized CMake cache path"; ExpectedMessage = "CMake cache path must be a normalized relative path"
        Mutate = { param($fixture, $record) $record.cmake.cachePath = "./CMakeCache.txt" }
    },
    [pscustomobject]@{
        Name = "CMake cache drift"; ExpectedMessage = "Current CMake cache does not match build provenance"
        Mutate = { param($fixture, $record) [System.IO.File]::AppendAllText((Join-Path $fixture.CacheRoot "CMakeCache.txt"), "drift") }
    },
    [pscustomobject]@{
        Name = "omitted production inventory"; ExpectedMessage = "Production input inventory does not match build provenance"
        Mutate = { param($fixture, $record) $record.productionInputs = @($record.productionInputs | Select-Object -Skip 1) }
    },
    [pscustomobject]@{
        Name = "duplicate production inventory"; ExpectedMessage = "Duplicate production input"
        Mutate = {
            param($fixture, $record)
            $items = @($record.productionInputs)
            $items[$items.Count - 1] = $items[0]
            $record.productionInputs = $items
        }
    },
    [pscustomobject]@{
        Name = "non-normalized production input path"; ExpectedMessage = "Production input path must be a normalized relative path"
        Mutate = { param($fixture, $record) $record.productionInputs[0].path = ".\CMakeLists.txt" }
    },
    [pscustomobject]@{
        Name = "production input hash drift"; ExpectedMessage = "Production input does not match build provenance"
        Mutate = { param($fixture, $record) $record.productionInputs[0].sha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "production source drift"; ExpectedMessage = "Production input does not match build provenance"
        Mutate = { param($fixture, $record) [System.IO.File]::AppendAllText((Join-Path $fixture.Root $record.productionInputs[0].path), "drift") }
    },
    [pscustomobject]@{
        Name = "production ASM-only source drift"
        ExpectedMessage = "Production input does not match build provenance: src/game/chord_voicing_bridge.asm"
        Mutate = {
            param($fixture, $record)
            [System.IO.File]::AppendAllText((Join-Path $fixture.Root "src/game/chord_voicing_bridge.asm"), "; drift")
        }
    },
    [pscustomobject]@{
        Name = "production input-set identity drift"; ExpectedMessage = "Production input set identity does not match build provenance"
        Mutate = { param($fixture, $record) $record.productionInputSetSha256 = "0" * 64 }
    },
    [pscustomobject]@{
        Name = "production input ordering drift"; ExpectedMessage = "Production input set identity does not match build provenance"
        Mutate = {
            param($fixture, $record)
            $items = @($record.productionInputs)
            [array]::Reverse($items)
            $record.productionInputs = $items
        }
    }
)

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ".ff7rp-build-provenance-selftest-$PID-$([Guid]::NewGuid().ToString('N'))"
$completed = $false
try {
    $validFixture = New-ProvenanceFixture -Root (Join-Path $testRoot "valid")
    Invoke-FixtureValidation -Fixture $validFixture
    [System.IO.File]::AppendAllText(
        (Join-Path $validFixture.Root "src/game/chord_voicing_bridge_harness.asm"), "; test-only drift")
    Invoke-FixtureValidation -Fixture $validFixture

    for ($index = 0; $index -lt $adverseCases.Count; ++$index) {
        $case = $adverseCases[$index]
        $fixture = New-ProvenanceFixture -Root (Join-Path $testRoot ("case-{0:D2}" -f $index))
        & $case.Mutate $fixture $fixture.Record
        Write-FixtureRecord -Path $fixture.RecordPath -Record $fixture.Record

        $message = $null
        try {
            Invoke-FixtureValidation -Fixture $fixture
        } catch {
            $message = $_.Exception.Message
        }
        if ($null -eq $message) {
            throw "Adverse case '$($case.Name)' was accepted"
        }
        if (!$message.Contains($case.ExpectedMessage)) {
            throw "Adverse case '$($case.Name)' failed for the wrong reason: $message"
        }
    }
    $completed = $true
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

if ($completed) {
    Write-Output "Build provenance self-test passed ($($adverseCases.Count) adverse cases)."
}
