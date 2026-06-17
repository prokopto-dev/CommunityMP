[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$ScriptRoot = "",
    [switch]$FailOnUntriggeredHook
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

if ($ScriptRoot -ne "") {
    $ScriptRoot = (Resolve-Path -LiteralPath $ScriptRoot).Path
}

function New-NameMap {
    return [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::Ordinal)
}

function Add-Occurrence {
    param(
        [System.Collections.Generic.Dictionary[string, object]]$Map,
        [string]$Name,
        [string]$FileName
    )

    if (-not $Map.ContainsKey($Name)) {
        $Map[$Name] = [ordered]@{
            Count = 0
            Files = (New-NameMap)
        }
    }

    $Map[$Name].Count += 1
    $Map[$Name].Files[$FileName] = $true
}

function Add-Name {
    param(
        [System.Collections.Generic.Dictionary[string, object]]$Map,
        [string]$Name
    )

    if (-not $Map.ContainsKey($Name)) {
        $Map[$Name] = [ordered]@{
            Count = 0
            Files = (New-NameMap)
        }
    }
}

function Get-EventAliases {
    param([string]$Root)

    $aliases = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::Ordinal)
    $customEventHooksPath = Join-Path $Root "files\tes3mp\server\scripts\customEventHooks.lua"
    $content = Get-Content -LiteralPath $customEventHooksPath -Raw
    $aliasTable = [regex]::Match(
        $content,
        'customEventHooks\.eventAliases\s*=\s*\{(?<body>.*?)\}',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)

    if ($aliasTable.Success) {
        foreach ($match in [regex]::Matches($aliasTable.Groups["body"].Value, '([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"')) {
            $aliases[$match.Groups[1].Value] = $match.Groups[2].Value
        }
    }

    return $aliases
}

function Add-LuaHookNames {
    param(
        [System.Collections.Generic.Dictionary[string, object]]$RegisteredHooks,
        [System.Collections.Generic.Dictionary[string, object]]$TriggeredHooks,
        [string]$Root
    )

    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -Filter *.lua -File | Sort-Object FullName) {
        $content = Get-Content -LiteralPath $file.FullName -Raw

        foreach ($match in [regex]::Matches($content, 'customEventHooks\s*\.\s*register(?:Handler|Validator)\s*\(\s*["'']([^"'']+)["'']')) {
            Add-Occurrence -Map $RegisteredHooks -Name $match.Groups[1].Value -FileName $file.FullName
        }

        foreach ($match in [regex]::Matches($content, 'customEventHooks\s*\.\s*trigger(?:Handlers|Validators)\s*\(\s*["'']([^"'']+)["'']')) {
            Add-Occurrence -Map $TriggeredHooks -Name $match.Groups[1].Value -FileName $file.FullName
        }
    }
}

$coreScriptRoot = Join-Path $SourceRoot "files\tes3mp\server\scripts"
$registeredHooks = New-NameMap
$triggeredHooks = New-NameMap
$aliases = Get-EventAliases -Root $SourceRoot

Add-LuaHookNames -RegisteredHooks $registeredHooks -TriggeredHooks $triggeredHooks -Root $coreScriptRoot
if ($ScriptRoot -ne "" -and $ScriptRoot -ne $coreScriptRoot) {
    Add-LuaHookNames -RegisteredHooks $registeredHooks -TriggeredHooks $triggeredHooks -Root $ScriptRoot
}

$dynamicCoreHooks = @(
    "OnPlayerAttribute",
    "OnPlayerSkill",
    "OnPlayerLevel",
    "OnPlayerShapeshift",
    "OnPlayerEquipment",
    "OnPlayerInventory",
    "OnPlayerSpellbook",
    "OnPlayerCooldowns",
    "OnPlayerQuickKeys",
    "OnActorList",
    "OnActorEquipment",
    "OnActorSpellsActive",
    "OnObjectActivate",
    "OnObjectHit",
    "OnObjectSound",
    "OnObjectPlace",
    "OnObjectSpawn",
    "OnObjectDelete",
    "OnObjectLock",
    "OnObjectMove",
    "OnObjectRotate",
    "OnObjectDialogueChoice",
    "OnObjectMiscellaneous",
    "OnObjectRestock",
    "OnObjectTrap",
    "OnObjectScale",
    "OnObjectState",
    "OnDoorState",
    "OnClientScriptLocal"
)

foreach ($hookName in $dynamicCoreHooks) {
    Add-Name -Map $triggeredHooks -Name $hookName
}

$untriggeredHooks = @(
    $registeredHooks.Keys |
        Where-Object {
            if ($triggeredHooks.ContainsKey($_)) {
                return $false
            }
            if ($aliases.ContainsKey($_) -and $triggeredHooks.ContainsKey($aliases[$_])) {
                return $false
            }
            return $true
        } |
        Sort-Object
)

Write-Host "TES3MP Lua event hook surface check"
Write-Host "Source root: $SourceRoot"
if ($ScriptRoot -ne "") {
    Write-Host "Script root: $ScriptRoot"
}
Write-Host "Registered hook names: $($registeredHooks.Count)"
Write-Host "Triggered hook names: $($triggeredHooks.Count)"
Write-Host "Known aliases: $($aliases.Count)"
Write-Host "Untriggered registered hook names: $($untriggeredHooks.Count)"

if ($aliases.Count -gt 0) {
    Write-Host ""
    Write-Host "Known hook aliases:"
    foreach ($entry in $aliases.GetEnumerator() | Sort-Object Key) {
        Write-Host ("  {0} -> {1}" -f $entry.Key, $entry.Value)
    }
}

if ($untriggeredHooks.Count -gt 0) {
    Write-Host ""
    Write-Host "Registered hook names without a matching trigger:"
    foreach ($hookName in $untriggeredHooks) {
        Write-Host ("  {0}: {1} registration(s)" -f $hookName, $registeredHooks[$hookName].Count)
        foreach ($fileName in @($registeredHooks[$hookName].Files.Keys | Sort-Object | Select-Object -First 5)) {
            Write-Host "    $fileName"
        }
    }
}

if ($FailOnUntriggeredHook -and $untriggeredHooks.Count -gt 0) {
    exit 1
}
