param()
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $Root 'build.ps1') -Raw
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'build.ps1 did not parse' }
$start = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$Catalog'
}, $true)
$finish = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$CatalogBuildArgument'
}, $true)
if (!$start -or !$finish) { throw 'Build catalog selection was not found' }
$selection = [scriptblock]::Create($source.Substring($start.Extent.StartOffset,
    $finish.Extent.EndOffset - $start.Extent.StartOffset))
$catalog = Get-Content -LiteralPath (Join-Path $Root 'src/game/rva_catalog.json') -Raw | ConvertFrom-Json
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) "ff7rp-catalog-selection-$([guid]::NewGuid().ToString('N'))"
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    @'
cmake_minimum_required(VERSION 3.24)
project(CatalogSelection NONE)
set(FF7RP_GAME_BUILD "unused-cache-default" CACHE STRING "Selected build")
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${FF7RP_GAME_BUILD}")
'@ | Set-Content -LiteralPath (Join-Path $temporary 'CMakeLists.txt')
    $build = Join-Path $temporary 'build'
    & cmake -S $temporary -B $build '-DFF7RP_GAME_BUILD=stale-build' | Out-Null
    if ($LASTEXITCODE) { throw 'Initial configure failed' }
    foreach ($requested in @([string]$catalog.builds[-1].id, '')) {
        $CatalogBuildId = $requested
        . $selection
        $arguments = $CatalogBuildArgument.Trim().Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
        & cmake -S $temporary -B $build @arguments | Out-Null
        if ($LASTEXITCODE) { throw 'Selected configure failed' }
        $expected = if ($requested) { $requested } else { [string]$catalog.default_build }
        if ([System.IO.File]::ReadAllText((Join-Path $build 'selected.txt')) -ne $expected) {
            throw 'Build wrapper retained a stale catalog selection'
        }
    }
    $CatalogBuildId = 'unknown-build'
    $rejected = $false
    try { . $selection } catch { $rejected = $true }
    if (!$rejected) { throw 'Unknown catalog selection was accepted' }
    Write-Output 'build_catalog_selection_selftest: ok'
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
