Set-StrictMode -Version Latest

function Invoke-TwoSurfacePublication {
    param(
        [string]$FirstFinal, [string]$SecondFinal,
        [string]$FirstStaging, [string]$SecondStaging,
        [scriptblock]$ValidateStaged, [scriptblock]$ValidatePublished,
        [string]$FailurePoint = ""
    )
    $null = & $ValidateStaged $FirstStaging $SecondStaging
    if ($FailurePoint -eq "before-publication") { throw "injected failure before publication" }
    $parent = Split-Path -Parent $FirstFinal
    if ($parent -ne (Split-Path -Parent $SecondFinal)) { throw "Publication surfaces must share one parent" }
    $token = "$PID-$([Guid]::NewGuid().ToString('N'))"
    $firstBackup = Join-Path $parent ".$([IO.Path]::GetFileName($FirstFinal)).previous-$token"
    $secondBackup = Join-Path $parent ".$([IO.Path]::GetFileName($SecondFinal)).previous-$token"
    $firstFailed = Join-Path $parent ".$([IO.Path]::GetFileName($FirstFinal)).failed-$token"
    $secondFailed = Join-Path $parent ".$([IO.Path]::GetFileName($SecondFinal)).failed-$token"
    $firstRetired = Join-Path $parent ".$([IO.Path]::GetFileName($FirstFinal)).retired-$token"
    $secondRetired = Join-Path $parent ".$([IO.Path]::GetFileName($SecondFinal)).retired-$token"
    $firstHad = Test-Path -LiteralPath $FirstFinal
    $secondHad = Test-Path -LiteralPath $SecondFinal
    $firstBacked = $false; $secondBacked = $false; $firstNew = $false; $secondNew = $false
    $committed = $false
    try {
        if ($firstHad) { Move-Item -LiteralPath $FirstFinal -Destination $firstBackup; $firstBacked = $true }
        if ($secondHad) { Move-Item -LiteralPath $SecondFinal -Destination $secondBackup; $secondBacked = $true }
        Move-Item -LiteralPath $FirstStaging -Destination $FirstFinal; $firstNew = $true
        if ($FailurePoint -eq "first-publish") { throw "injected failure after first publish" }
        Move-Item -LiteralPath $SecondStaging -Destination $SecondFinal; $secondNew = $true
        if ($FailurePoint -in @("second-publish", "recovery", "persistent-first-quarantine", "persistent-second-quarantine", "persistent-first-restore", "persistent-second-restore")) {
            throw "injected precommit failure after second publish"
        }
        $null = & $ValidatePublished $FirstFinal $SecondFinal
        # This is the only commit point. Recovery must never run after it.
        $committed = $true
    } catch {
        if ($committed) { throw "internal transaction error: committed publication entered recovery" }
        $original = $_.Exception.Message
        $recoveryErrors = [Collections.Generic.List[string]]::new()
        foreach ($quarantine in @(
            [pscustomobject]@{ Name="second"; New=$secondNew; Final=$SecondFinal; Failed=$secondFailed; Injection="persistent-second-quarantine" },
            [pscustomobject]@{ Name="first"; New=$firstNew; Final=$FirstFinal; Failed=$firstFailed; Injection="persistent-first-quarantine" }
        )) {
            if (!$quarantine.New -or !(Test-Path -LiteralPath $quarantine.Final)) { continue }
            try {
                if ($FailurePoint -eq $quarantine.Injection) { throw "injected persistent quarantine failure" }
                Move-Item -LiteralPath $quarantine.Final -Destination $quarantine.Failed
            } catch { $recoveryErrors.Add("$($quarantine.Name) quarantine: $($_.Exception.Message)") }
        }
        foreach ($restore in @(
            [pscustomobject]@{ Name="second"; Backed=$secondBacked; Backup=$secondBackup; Final=$SecondFinal; Injection="persistent-second-restore" },
            [pscustomobject]@{ Name="first"; Backed=$firstBacked; Backup=$firstBackup; Final=$FirstFinal; Injection="persistent-first-restore" }
        )) {
            if (!$restore.Backed) { continue }
            if (Test-Path -LiteralPath $restore.Final) {
                $recoveryErrors.Add("$($restore.Name) restore: final path remains occupied; backup preserved at $($restore.Backup)")
                continue
            }
            try {
                if ($FailurePoint -eq $restore.Injection) { throw "injected persistent restore failure" }
                Move-Item -LiteralPath $restore.Backup -Destination $restore.Final
            } catch { $recoveryErrors.Add("$($restore.Name) restore: $($_.Exception.Message)") }
        }
        $evidence = @($FirstFinal, $SecondFinal, $FirstStaging, $SecondStaging,
            $firstBackup, $secondBackup, $firstFailed, $secondFailed) |
            Where-Object { Test-Path -LiteralPath $_ }
        $message = "Two-surface publication failed before commit: $original"
        if ($recoveryErrors.Count -ne 0) { $message += "; recovery errors: $($recoveryErrors -join ' | ')" }
        if ($evidence.Count -ne 0) { $message += "; preserved paths: $($evidence -join ', ')" }
        throw $message
    }

    $cleanupDebt = [Collections.Generic.List[string]]::new()
    $cleanupWarnings = [Collections.Generic.List[string]]::new()
    foreach ($retirement in @(
        [pscustomobject]@{ Name="first"; Backed=$firstBacked; Backup=$firstBackup; Retired=$firstRetired; Injection="first-backup-retirement" },
        [pscustomobject]@{ Name="second"; Backed=$secondBacked; Backup=$secondBackup; Retired=$secondRetired; Injection="second-backup-retirement" }
    )) {
        if (!$retirement.Backed) { continue }
        try {
            if ($FailurePoint -eq $retirement.Injection) { throw "injected backup-retirement failure" }
            # Retirement is an atomic rename only. Recursive deletion is deliberately outside the transaction.
            Move-Item -LiteralPath $retirement.Backup -Destination $retirement.Retired
            $cleanupDebt.Add($retirement.Retired)
        } catch {
            $cleanupDebt.Add($retirement.Backup)
            $cleanupWarnings.Add("$($retirement.Name) backup retained at $($retirement.Backup): $($_.Exception.Message)")
        }
    }
    foreach ($warning in $cleanupWarnings) { Write-Warning $warning }
    return [pscustomobject]@{ Committed=$true; CleanupDebt=@($cleanupDebt); CleanupWarnings=@($cleanupWarnings) }
}

function Assert-TwoSurfacePublicationSelfTests {
    $root = Join-Path ([IO.Path]::GetTempPath()) ".ff7rp-two-surface-$PID-$([Guid]::NewGuid().ToString('N'))"
    function New-Case([string]$Name, [bool]$HadFirst, [bool]$HadSecond) {
        $case = Join-Path $root $Name; [IO.Directory]::CreateDirectory($case) | Out-Null
        $first = Join-Path $case "package"; $second = Join-Path $case "release"
        $firstStage = Join-Path $case ".package.staging-case"; $secondStage = Join-Path $case ".release.staging-case"
        if ($HadFirst) { [IO.Directory]::CreateDirectory($first) | Out-Null; [IO.File]::WriteAllText((Join-Path $first "id"), "old") }
        if ($HadSecond) { [IO.Directory]::CreateDirectory($second) | Out-Null; [IO.File]::WriteAllText((Join-Path $second "id"), "old") }
        [IO.Directory]::CreateDirectory($firstStage) | Out-Null; [IO.File]::WriteAllText((Join-Path $firstStage "id"), "new")
        [IO.Directory]::CreateDirectory($secondStage) | Out-Null; [IO.File]::WriteAllText((Join-Path $secondStage "id"), "new")
        return [pscustomobject]@{ First=$first; Second=$second; FirstStage=$firstStage; SecondStage=$secondStage }
    }
    $validateIdentity = { param($a,$b)
        if ([IO.File]::ReadAllText((Join-Path $a "id")) -ne [IO.File]::ReadAllText((Join-Path $b "id"))) { throw "mixed two-surface identity" }
    }
    try {
        foreach ($hadFirst in @($false, $true)) { foreach ($hadSecond in @($false, $true)) {
            foreach ($failure in @("staged-validation", "before-publication", "first-publish", "second-publish", "published-validation", "recovery")) {
                $c = New-Case "$hadFirst-$hadSecond-$failure" $hadFirst $hadSecond
                $failed = $false
                $stagedValidator = if ($failure -eq "staged-validation") {
                    { param($a,$b) throw "injected staged validation failure" }
                } else { $validateIdentity }
                $publishedValidator = if ($failure -eq "published-validation") {
                    { param($a,$b) throw "injected published validation failure" }
                } else { $validateIdentity }
                try { Invoke-TwoSurfacePublication $c.First $c.Second $c.FirstStage $c.SecondStage $stagedValidator $publishedValidator $failure } catch { $failed = $true }
                if (!$failed) { throw "Precommit failure '$failure' was accepted" }
                foreach ($surface in @(@($c.First,$hadFirst), @($c.Second,$hadSecond))) {
                    if ($surface[1] -and (!(Test-Path $surface[0]) -or [IO.File]::ReadAllText((Join-Path $surface[0] "id")) -ne "old")) { throw "Previous surface was not restored" }
                    if (!$surface[1] -and (Test-Path $surface[0])) { throw "Previously absent surface appeared" }
                }
            }
            $c = New-Case "$hadFirst-$hadSecond-success" $hadFirst $hadSecond
            $result = Invoke-TwoSurfacePublication $c.First $c.Second $c.FirstStage $c.SecondStage $validateIdentity $validateIdentity
            if (!$result.Committed -or [IO.File]::ReadAllText((Join-Path $c.First "id")) -ne "new" -or
                [IO.File]::ReadAllText((Join-Path $c.Second "id")) -ne "new") { throw "Matching pair was not committed" }
            foreach ($debt in $result.CleanupDebt) {
                if (!(Test-Path $debt) -or [IO.File]::ReadAllText((Join-Path $debt "id")) -ne "old") { throw "Retired backup was not preserved exactly" }
            }
        }}
        $persistentCases = @()
        foreach ($hadFirst in @($false, $true)) { foreach ($hadSecond in @($false, $true)) {
            $persistentCases += [pscustomobject]@{ Failure="persistent-first-quarantine"; HadFirst=$hadFirst; HadSecond=$hadSecond }
            $persistentCases += [pscustomobject]@{ Failure="persistent-second-quarantine"; HadFirst=$hadFirst; HadSecond=$hadSecond }
            if ($hadFirst) { $persistentCases += [pscustomobject]@{ Failure="persistent-first-restore"; HadFirst=$hadFirst; HadSecond=$hadSecond } }
            if ($hadSecond) { $persistentCases += [pscustomobject]@{ Failure="persistent-second-restore"; HadFirst=$hadFirst; HadSecond=$hadSecond } }
        }}
        foreach ($persistent in $persistentCases) {
            $failure = $persistent.Failure; $hadFirst = $persistent.HadFirst; $hadSecond = $persistent.HadSecond
            $c = New-Case "persistent-$failure-$hadFirst-$hadSecond" $hadFirst $hadSecond
            $message = ""
            try { Invoke-TwoSurfacePublication $c.First $c.Second $c.FirstStage $c.SecondStage $validateIdentity $validateIdentity $failure } catch { $message = $_.Exception.Message }
            if ($message -notmatch "recovery errors:" -or $message -notmatch "preserved paths:") { throw "Persistent recovery failure did not report aggregate evidence: $failure" }
            $caseRoot = Split-Path $c.First -Parent
            $copies = @(Get-ChildItem -LiteralPath $caseRoot -Directory -Force | Where-Object { Test-Path (Join-Path $_.FullName "id") })
            $oldCopies = @($copies | Where-Object { [IO.File]::ReadAllText((Join-Path $_.FullName "id")) -eq "old" })
            $newCopies = @($copies | Where-Object { [IO.File]::ReadAllText((Join-Path $_.FullName "id")) -eq "new" })
            $expectedOld = [int]$hadFirst + [int]$hadSecond
            if ($oldCopies.Count -ne $expectedOld -or $newCopies.Count -ne 2) {
                throw "Persistent recovery failure lost an authoritative old or new copy: $failure/$hadFirst/$hadSecond"
            }
            $packagePrevious = @(Get-ChildItem $caseRoot -Directory -Force -Filter ".package.previous-*")
            $releasePrevious = @(Get-ChildItem $caseRoot -Directory -Force -Filter ".release.previous-*")
            $packageFailed = @(Get-ChildItem $caseRoot -Directory -Force -Filter ".package.failed-*")
            $releaseFailed = @(Get-ChildItem $caseRoot -Directory -Force -Filter ".release.failed-*")
            switch ($failure) {
                "persistent-first-quarantine" {
                    if ([IO.File]::ReadAllText((Join-Path $c.First "id")) -ne "new" -or
                        ($hadSecond -and [IO.File]::ReadAllText((Join-Path $c.Second "id")) -ne "old") -or
                        (!$hadSecond -and (Test-Path $c.Second)) -or
                        $packagePrevious.Count -ne [int]$hadFirst -or $releaseFailed.Count -ne 1 -or
                        $packageFailed.Count -ne 0 -or $releasePrevious.Count -ne 0) { throw "First quarantine evidence layout changed" }
                }
                "persistent-second-quarantine" {
                    if (($hadFirst -and [IO.File]::ReadAllText((Join-Path $c.First "id")) -ne "old") -or
                        (!$hadFirst -and (Test-Path $c.First)) -or
                        [IO.File]::ReadAllText((Join-Path $c.Second "id")) -ne "new" -or
                        $releasePrevious.Count -ne [int]$hadSecond -or $packageFailed.Count -ne 1 -or
                        $releaseFailed.Count -ne 0 -or $packagePrevious.Count -ne 0) { throw "Second quarantine evidence layout changed" }
                }
                "persistent-first-restore" {
                    if ((Test-Path $c.First) -or
                        ($hadSecond -and [IO.File]::ReadAllText((Join-Path $c.Second "id")) -ne "old") -or
                        (!$hadSecond -and (Test-Path $c.Second)) -or
                        $packagePrevious.Count -ne 1 -or $packageFailed.Count -ne 1 -or $releaseFailed.Count -ne 1) { throw "First restore evidence layout changed" }
                }
                "persistent-second-restore" {
                    if ((Test-Path $c.Second) -or
                        ($hadFirst -and [IO.File]::ReadAllText((Join-Path $c.First "id")) -ne "old") -or
                        (!$hadFirst -and (Test-Path $c.First)) -or
                        $releasePrevious.Count -ne 1 -or $packageFailed.Count -ne 1 -or $releaseFailed.Count -ne 1) { throw "Second restore evidence layout changed" }
                }
            }
        }
        foreach ($hadFirst in @($false, $true)) { foreach ($hadSecond in @($false, $true)) {
            foreach ($failure in @("first-backup-retirement", "second-backup-retirement")) {
                if (($failure -eq "first-backup-retirement" -and !$hadFirst) -or
                    ($failure -eq "second-backup-retirement" -and !$hadSecond)) { continue }
                $c = New-Case "postcommit-$failure-$hadFirst-$hadSecond" $hadFirst $hadSecond
                $result = Invoke-TwoSurfacePublication $c.First $c.Second $c.FirstStage $c.SecondStage $validateIdentity $validateIdentity $failure
                if (!$result.Committed -or $result.CleanupWarnings.Count -ne 1 -or
                    [IO.File]::ReadAllText((Join-Path $c.First "id")) -ne "new" -or
                    [IO.File]::ReadAllText((Join-Path $c.Second "id")) -ne "new") { throw "Postcommit cleanup failure changed success semantics" }
                $expectedDebt = [int]$hadFirst + [int]$hadSecond
                if ($result.CleanupDebt.Count -ne $expectedDebt) { throw "Every exact old backup must remain explicit cleanup debt" }
                $expectedDebtPaths = @()
                if ($hadFirst) {
                    $expectedDebtPaths += if ($failure -eq "first-backup-retirement") { ".package.previous-*" } else { ".package.retired-*" }
                }
                if ($hadSecond) {
                    $expectedDebtPaths += if ($failure -eq "second-backup-retirement") { ".release.previous-*" } else { ".release.retired-*" }
                }
                foreach ($pattern in $expectedDebtPaths) {
                    $matches = @($result.CleanupDebt | Where-Object { (Split-Path -Leaf $_) -like $pattern })
                    if ($matches.Count -ne 1 -or !(Test-Path -LiteralPath $matches[0]) -or
                        [IO.File]::ReadAllText((Join-Path $matches[0] "id")) -ne "old") {
                        throw "Postcommit cleanup debt path or content changed: $failure/$hadFirst/$hadSecond/$pattern"
                    }
                }
            }
        }}
        $scriptText = [IO.File]::ReadAllText($PSCommandPath).Split("function Assert-TwoSurfacePublicationSelfTests")[0]
        if ($scriptText -match 'Remove-Item[^\r\n]*-Recurse') { throw "Transaction must not recursively delete backup evidence" }
    } finally { if (Test-Path $root) { Remove-Item $root -Recurse -Force } }
    Write-Output "Two-surface publication precommit recovery and postcommit cleanup matrix passed."
}
