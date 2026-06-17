[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingCallbackBridge
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Add-Name {
    param(
        [System.Collections.Specialized.OrderedDictionary]$Names,
        [string]$Name
    )

    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $Names[$Name] = $true
    }
}

function Get-CppCallbackNames {
    param([string]$Root)

    $names = [ordered]@{}
    $header = Join-Path $Root "apps\openmw-mp\Script\ScriptFunctions.hpp"
    $content = Get-Content -LiteralPath $header -Raw
    $callbacksArray = [regex]::Match(
        $content,
        'static\s+constexpr\s+ScriptCallbackData\s+callbacks\[\]\s*\{(?<body>.*?)\n\s*\};',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)

    if (-not $callbacksArray.Success) {
        throw "Could not find ScriptFunctions::callbacks in $header"
    }

    foreach ($match in [regex]::Matches($callbacksArray.Groups["body"].Value, '\{\s*"([^"]+)"\s*,')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    return $names
}

function Get-LuaGlobalCallbackNames {
    param([string]$LuaFile)

    $names = [ordered]@{}
    $content = Get-Content -LiteralPath $LuaFile -Raw

    foreach ($match in [regex]::Matches($content, '(?m)^\s*function\s+(On[A-Za-z0-9_]+)\s*\(')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    foreach ($match in [regex]::Matches($content, '(?m)^\s*(On[A-Za-z0-9_]+)\s*=\s*function\s*\(')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    return $names
}

function Get-EventHandlerNames {
    param([string]$LuaFile)

    $names = [ordered]@{}
    $content = Get-Content -LiteralPath $LuaFile -Raw

    foreach ($match in [regex]::Matches($content, '\beventHandler\.(On[A-Za-z0-9_]+)\s*=\s*function\s*\(')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    return $names
}

function Get-ServerCoreDelegates {
    param([string]$LuaFile)

    $names = [ordered]@{}
    $content = Get-Content -LiteralPath $LuaFile -Raw

    foreach ($match in [regex]::Matches($content, '\beventHandler\.(On[A-Za-z0-9_]+)\s*\(')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    return $names
}

$serverCorePath = Join-Path $SourceRoot "files\tes3mp\server\scripts\serverCore.lua"
$eventHandlerPath = Join-Path $SourceRoot "files\tes3mp\server\scripts\eventHandler.lua"

$cppCallbacks = Get-CppCallbackNames -Root $SourceRoot
$globalCallbacks = Get-LuaGlobalCallbackNames -LuaFile $serverCorePath
$eventHandlers = Get-EventHandlerNames -LuaFile $eventHandlerPath
$serverCoreDelegates = Get-ServerCoreDelegates -LuaFile $serverCorePath

$missingGlobalCallbacks = @(
    $cppCallbacks.Keys |
        Where-Object { -not $globalCallbacks.Contains($_) } |
        Sort-Object
)

$missingEventHandlerDelegates = @(
    $serverCoreDelegates.Keys |
        Where-Object { -not $eventHandlers.Contains($_) } |
        Sort-Object
)

$luaOnlyGlobalCallbacks = @(
    $globalCallbacks.Keys |
        Where-Object { -not $cppCallbacks.Contains($_) } |
        Sort-Object
)

Write-Host "TES3MP Lua callback surface check"
Write-Host "Source root: $SourceRoot"
Write-Host "C++ callbacks: $($cppCallbacks.Count)"
Write-Host "serverCore global callbacks: $($globalCallbacks.Count)"
Write-Host "serverCore eventHandler delegates: $($serverCoreDelegates.Count)"
Write-Host "eventHandler callbacks: $($eventHandlers.Count)"
Write-Host "Missing serverCore global callbacks: $($missingGlobalCallbacks.Count)"
Write-Host "Missing eventHandler delegates: $($missingEventHandlerDelegates.Count)"

if ($missingGlobalCallbacks.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing serverCore global callbacks:"
    foreach ($name in $missingGlobalCallbacks) {
        Write-Host "  $name"
    }
}

if ($missingEventHandlerDelegates.Count -gt 0) {
    Write-Host ""
    Write-Host "serverCore delegates without eventHandler functions:"
    foreach ($name in $missingEventHandlerDelegates) {
        Write-Host "  $name"
    }
}

if ($luaOnlyGlobalCallbacks.Count -gt 0) {
    Write-Host ""
    Write-Host "Lua-only global callbacks:"
    foreach ($name in $luaOnlyGlobalCallbacks) {
        Write-Host "  $name"
    }
}

if ($FailOnMissingCallbackBridge -and
    ($missingGlobalCallbacks.Count -gt 0 -or $missingEventHandlerDelegates.Count -gt 0)) {
    exit 1
}
