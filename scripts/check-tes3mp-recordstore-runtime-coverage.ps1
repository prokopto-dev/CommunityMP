[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingCoverage
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required source file was not found: $path"
    }

    return Get-Content -LiteralPath $path -Raw
}

function Get-LuaTableBlock {
    param(
        [string]$Text,
        [string]$AssignmentPattern
    )

    $assignment = [regex]::Match($Text, $AssignmentPattern)
    if (-not $assignment.Success) {
        throw "Unable to find Lua table assignment: $AssignmentPattern"
    }

    $start = $Text.IndexOf("{", $assignment.Index)
    if ($start -lt 0) {
        throw "Unable to find Lua table opening brace for: $AssignmentPattern"
    }

    $depth = 0
    for ($index = $start; $index -lt $Text.Length; $index++) {
        $char = $Text[$index]
        if ($char -eq "{") {
            $depth++
        } elseif ($char -eq "}") {
            $depth--
            if ($depth -eq 0) {
                return $Text.Substring($start, $index - $start + 1)
            }
        }
    }

    throw "Unable to find Lua table closing brace for: $AssignmentPattern"
}

function Get-QuotedNames {
    param([string]$Text)

    return @(
        [regex]::Matches($Text, '"([^"]+)"') |
            ForEach-Object { $_.Groups[1].Value } |
            Sort-Object -Unique
    )
}

function Get-MarkerNames {
    param([string]$Text)

    return @(
        [regex]::Matches($Text, '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=', [System.Text.RegularExpressions.RegexOptions]::Multiline) |
            ForEach-Object { $_.Groups[1].Value } |
            Sort-Object -Unique
    )
}

function Compare-NameSet {
    param(
        [string[]]$Expected,
        [string[]]$Actual,
        [string]$Label,
        [System.Collections.Generic.List[string]]$Missing
    )

    $missingItems = @($Expected | Where-Object { $_ -notin $Actual } | Sort-Object)
    $extraItems = @($Actual | Where-Object { $_ -notin $Expected } | Sort-Object)

    if ($missingItems.Count -gt 0) {
        $Missing.Add("$Label missing: $($missingItems -join ', ')")
    }

    if ($extraItems.Count -gt 0) {
        $Missing.Add("$Label extra: $($extraItems -join ', ')")
    }
}

$config = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$runtimeSmoke = Get-SourceText "scripts\smoke-tes3mp-runtime.ps1"

$recordStoreLoadOrderBlock = Get-LuaTableBlock -Text $config -AssignmentPattern 'config\.recordStoreLoadOrder\s*='
$configuredRecordStores = Get-QuotedNames $recordStoreLoadOrderBlock

$snapshotMarkerBlock = Get-LuaTableBlock -Text $runtimeSmoke -AssignmentPattern 'local\s+expectedRecordStoreSnapshotMarkers\s*='
$snapshotMarkerStores = Get-MarkerNames $snapshotMarkerBlock

$snapshotLoop = [regex]::Match(
    $runtimeSmoke,
    'for\s+_,\s*storeType\s+in\s+pairs\s*\(\s*\{(?<body>.*?)\}\s*\)\s*do\s*\r?\n\s*local\s+snapshot\s*=\s*Database:GetSingleValue\("recordstore_data"',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $snapshotLoop.Success) {
    throw "Unable to find SQLite recordstore_data snapshot loop in runtime smoke"
}
$snapshotLoopStores = Get-QuotedNames $snapshotLoop.Groups["body"].Value

$issues = [System.Collections.Generic.List[string]]::new()
Compare-NameSet -Expected $configuredRecordStores -Actual $snapshotMarkerStores -Label "Snapshot marker stores" -Missing $issues
Compare-NameSet -Expected $configuredRecordStores -Actual $snapshotLoopStores -Label "Snapshot loop stores" -Missing $issues

Write-Host "TES3MP record-store runtime coverage check"
Write-Host "Source root: $SourceRoot"
Write-Host "Configured record stores: $($configuredRecordStores.Count)"
Write-Host "Snapshot marker stores: $($snapshotMarkerStores.Count)"
Write-Host "Snapshot loop stores: $($snapshotLoopStores.Count)"
Write-Host "Coverage issues: $($issues.Count)"

if ($issues.Count -gt 0) {
    Write-Host ""
    Write-Host "Record-store runtime coverage mismatches:"
    foreach ($issue in $issues) {
        Write-Host "  $issue"
    }

    if ($FailOnMissingCoverage) {
        exit 1
    }
}
