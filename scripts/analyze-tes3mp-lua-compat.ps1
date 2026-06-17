[CmdletBinding()]
param(
    [string]$ScriptRoot = "",
    [string]$SourceRoot = "",
    [string]$LuaExe = "",
    [switch]$FailOnUnknownTes3mpCall,
    [switch]$FailOnSyntaxError
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

if ($ScriptRoot -eq "") {
    $ScriptRoot = Join-Path $SourceRoot "files\tes3mp\server\scripts"
}

if (-not (Test-Path -LiteralPath $ScriptRoot -PathType Container)) {
    throw "Script root not found: $ScriptRoot"
}

$ScriptRoot = (Resolve-Path -LiteralPath $ScriptRoot).Path

$knownExternalRequireModules = @(
    "io2"
)

function Get-Tes3mpApiNames {
    param([string]$Root)

    $apiNames = [ordered]@{}
    $headerRoots = @(
        (Join-Path $Root "apps\openmw-mp\Script\ScriptFunctions.hpp"),
        (Join-Path $Root "apps\openmw-mp\Script\Functions")
    )

    foreach ($headerRoot in $headerRoots) {
        if (Test-Path -LiteralPath $headerRoot -PathType Leaf) {
            $headers = @(Get-Item -LiteralPath $headerRoot)
        } elseif (Test-Path -LiteralPath $headerRoot -PathType Container) {
            $headers = @(Get-ChildItem -LiteralPath $headerRoot -Filter *.hpp)
        } else {
            $headers = @()
        }

        foreach ($header in $headers) {
            $content = Get-Content -LiteralPath $header.FullName -Raw
            foreach ($match in [regex]::Matches($content, '\{\s*"([^"]+)"\s*,')) {
                $apiNames[$match.Groups[1].Value] = $true
            }
        }
    }

    return $apiNames
}

function Resolve-LegacyRequire {
    param(
        [string]$ModuleName,
        [string]$Root,
        [string]$ScannedScriptRoot
    )

    if ($knownExternalRequireModules -contains $ModuleName) {
        return $true
    }

    if ($ModuleName.EndsWith(".") -or $ModuleName.EndsWith("/") -or $ModuleName.EndsWith("\")) {
        return $true
    }

    $modulePath = $ModuleName -replace '[./\\]', [IO.Path]::DirectorySeparatorChar
    $moduleLeaf = @($ModuleName -split '[./\\]')[-1]
    $serverRoot = Join-Path $Root "files\tes3mp\server"
    $candidates = @(
        (Join-Path $ScannedScriptRoot "$modulePath.lua"),
        (Join-Path $ScannedScriptRoot "$modulePath\init.lua"),
        (Join-Path $ScannedScriptRoot "$moduleLeaf.lua"),
        (Join-Path $serverRoot "scripts\$modulePath.lua"),
        (Join-Path $serverRoot "scripts\$modulePath\init.lua"),
        (Join-Path $serverRoot "lib\lua\$modulePath.lua"),
        (Join-Path $serverRoot "lib\lua\$modulePath\init.lua")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $true
        }
    }

    return $false
}

$luaFiles = @(Get-ChildItem -LiteralPath $ScriptRoot -Recurse -Filter *.lua -File | Sort-Object FullName)
if ($luaFiles.Count -eq 0) {
    throw "No Lua files found under $ScriptRoot"
}

$apiNames = Get-Tes3mpApiNames -Root $SourceRoot
$tes3mpCallCounts = @{}
$unknownTes3mpCalls = [ordered]@{}
$requireCounts = @{}
$unknownRequires = [ordered]@{}
$syntaxErrors = @()

$hotspotPatterns = [ordered]@{
    "customEventHooks.registerHandler" = '\bcustomEventHooks\s*\.\s*registerHandler\b'
    "customEventHooks.registerValidator" = '\bcustomEventHooks\s*\.\s*registerValidator\b'
    "customCommandHooks.registerCommand" = '\bcustomCommandHooks\s*\.\s*registerCommand\b'
    "tes3mp direct calls" = '\btes3mp\s*[:.]\s*[A-Za-z_][A-Za-z0-9_]*'
    "Players table access" = '\bPlayers\s*\['
    "LoadedCells table access" = '\bLoadedCells\s*\['
    "WorldInstance access" = '\bWorldInstance\b'
    "RecordStores access" = '\bRecordStores\s*\['
    "logicHandler access" = '\blogicHandler\s*\.'
    "packetReader access" = '\bpacketReader\s*\.'
    "packetBuilder access" = '\bpacketBuilder\s*\.'
    "legacy timer creation" = '\btes3mp\s*\.\s*CreateTimer(?:Ex)?\b'
    "GUI dialog calls" = '\btes3mp\s*\.\s*(?:MessageBox|CustomMessageBox|InputDialog|ListBox)\b'
}
$hotspotCounts = [ordered]@{}
foreach ($key in $hotspotPatterns.Keys) {
    $hotspotCounts[$key] = 0
}

foreach ($file in $luaFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw

    foreach ($key in $hotspotPatterns.Keys) {
        $hotspotCounts[$key] += [regex]::Matches($content, $hotspotPatterns[$key]).Count
    }

    foreach ($match in [regex]::Matches($content, '\btes3mp\s*[:.]\s*([A-Za-z_][A-Za-z0-9_]*)')) {
        $name = $match.Groups[1].Value
        if (-not $tes3mpCallCounts.ContainsKey($name)) {
            $tes3mpCallCounts[$name] = 0
        }
        $tes3mpCallCounts[$name] += 1

        if (-not $apiNames.Contains($name)) {
            if (-not $unknownTes3mpCalls.Contains($name)) {
                $unknownTes3mpCalls[$name] = [ordered]@{
                    Count = 0
                    Files = [ordered]@{}
                }
            }
            $unknownTes3mpCalls[$name].Count += 1
            $unknownTes3mpCalls[$name].Files[$file.FullName] = $true
        }
    }

    foreach ($match in [regex]::Matches($content, 'require\s*\(?\s*["'']([^"'']+)["'']\s*\)?')) {
        $name = $match.Groups[1].Value
        if (-not $requireCounts.ContainsKey($name)) {
            $requireCounts[$name] = 0
        }
        $requireCounts[$name] += 1

        if (-not (Resolve-LegacyRequire -ModuleName $name -Root $SourceRoot -ScannedScriptRoot $ScriptRoot)) {
            if (-not $unknownRequires.Contains($name)) {
                $unknownRequires[$name] = [ordered]@{
                    Count = 0
                    Files = [ordered]@{}
                }
            }
            $unknownRequires[$name].Count += 1
            $unknownRequires[$name].Files[$file.FullName] = $true
        }
    }

    if ($LuaExe -ne "") {
        $compileOutput = & $LuaExe -e "assert(loadfile(arg[1]))" $file.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            $syntaxErrors += [ordered]@{
                File = $file.FullName
                Error = ($compileOutput -join "`n")
            }
        }
    }
}

$topTes3mpCalls = @(
    $tes3mpCallCounts.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true }, @{ Expression = "Key"; Descending = $false } |
        Select-Object -First 25
)

$topRequires = @(
    $requireCounts.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true }, @{ Expression = "Key"; Descending = $false } |
        Select-Object -First 25
)

Write-Host "TES3MP Lua compatibility analyzer"
Write-Host "Source root: $SourceRoot"
Write-Host "Script root: $ScriptRoot"
Write-Host "Lua files scanned: $($luaFiles.Count)"
Write-Host "Unique direct tes3mp calls: $($tes3mpCallCounts.Count)"
Write-Host "Unknown direct tes3mp calls: $($unknownTes3mpCalls.Count)"
Write-Host "Unknown require modules: $($unknownRequires.Count)"
if ($LuaExe -ne "") {
    Write-Host "Lua syntax errors: $($syntaxErrors.Count)"
}

Write-Host ""
Write-Host "Hotspot counts:"
foreach ($key in $hotspotCounts.Keys) {
    Write-Host ("  {0}: {1}" -f $key, $hotspotCounts[$key])
}

Write-Host ""
Write-Host "Top direct tes3mp calls:"
foreach ($entry in $topTes3mpCalls) {
    Write-Host ("  {0}: {1}" -f $entry.Key, $entry.Value)
}

Write-Host ""
Write-Host "Top require modules:"
foreach ($entry in $topRequires) {
    Write-Host ("  {0}: {1}" -f $entry.Key, $entry.Value)
}

if ($unknownTes3mpCalls.Count -gt 0) {
    Write-Host ""
    Write-Host "Unknown direct tes3mp calls:"
    foreach ($entry in $unknownTes3mpCalls.GetEnumerator() | Sort-Object Name) {
        $sampleFiles = @($entry.Value.Files.Keys | Sort-Object | Select-Object -First 5)
        Write-Host ("  {0}: {1} use(s)" -f $entry.Key, $entry.Value.Count)
        foreach ($sampleFile in $sampleFiles) {
            Write-Host "    $sampleFile"
        }
    }
}

if ($unknownRequires.Count -gt 0) {
    Write-Host ""
    Write-Host "Require modules not found in bundled TES3MP server paths:"
    foreach ($entry in $unknownRequires.GetEnumerator() | Sort-Object Name) {
        $sampleFiles = @($entry.Value.Files.Keys | Sort-Object | Select-Object -First 5)
        Write-Host ("  {0}: {1} use(s)" -f $entry.Key, $entry.Value.Count)
        foreach ($sampleFile in $sampleFiles) {
            Write-Host "    $sampleFile"
        }
    }
}

if ($syntaxErrors.Count -gt 0) {
    Write-Host ""
    Write-Host "Lua syntax errors:"
    foreach ($syntaxError in $syntaxErrors) {
        Write-Host "  $($syntaxError.File)"
        Write-Host "    $($syntaxError.Error)"
    }
}

if (($FailOnUnknownTes3mpCall -and $unknownTes3mpCalls.Count -gt 0) -or
    ($FailOnSyntaxError -and $syntaxErrors.Count -gt 0)) {
    exit 1
}
