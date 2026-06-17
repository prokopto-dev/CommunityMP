[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$DocsBaseUrl = "https://docs.tes3mp.com/en/latest/api/",
    [string]$DocumentedNamesPath = "",
    [switch]$RefreshDocumentedNames,
    [switch]$FailOnMissingDocumentedFunction
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

if ($DocumentedNamesPath -eq "") {
    $DocumentedNamesPath = Join-Path $SourceRoot "scripts\data\tes3mp-lua-api-0.8.1-functions.txt"
}

$docsPages = @(
    "ActorFunctions.html",
    "BookFunctions.html",
    "CellFunctions.html",
    "CharClassFunctions.html",
    "ChatFunctions.html",
    "DialogueFunctions.html",
    "FactionFunctions.html",
    "GUIFunctions.html",
    "ItemFunctions.html",
    "MechanicsFunctions.html",
    "MiscellaneousFunctions.html",
    "ObjectFunctions.html",
    "PositionFunctions.html",
    "QuestFunctions.html",
    "RecordsDynamicFunctions.html",
    "ServerFunctions.html",
    "SettingFunctions.html",
    "ShapeshiftFunctions.html",
    "SpellFunctions.html",
    "StatsFunctions.html",
    "WorldstateFunctions.html"
)

function Add-Name {
    param(
        [System.Collections.Specialized.OrderedDictionary]$Names,
        [string]$Name
    )

    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $Names[$Name] = $true
    }
}

function Get-LocalTes3mpApiNames {
    param([string]$Root)

    $names = [ordered]@{}
    $functionHeadersRoot = Join-Path $Root "apps\openmw-mp\Script\Functions"

    foreach ($header in Get-ChildItem -LiteralPath $functionHeadersRoot -Filter *.hpp -File | Sort-Object FullName) {
        $content = Get-Content -LiteralPath $header.FullName -Raw
        foreach ($match in [regex]::Matches($content, '\{\s*"([^"]+)"\s*,')) {
            Add-Name -Names $names -Name $match.Groups[1].Value
        }
    }

    $scriptFunctionsHeader = Join-Path $Root "apps\openmw-mp\Script\ScriptFunctions.hpp"
    $scriptFunctionsContent = Get-Content -LiteralPath $scriptFunctionsHeader -Raw
    $functionsBody = ""
    $functionsArray = [regex]::Match(
        $scriptFunctionsContent,
        'static\s+constexpr\s+ScriptFunctionData\s+functions\[\]\s*\{(?<body>.*?)\n\s*\};',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)

    if ($functionsArray.Success) {
        $functionsBody = $functionsArray.Groups["body"].Value
    } else {
        $functionsMacro = [regex]::Match(
            $scriptFunctionsContent,
            '#define\s+SCRIPT_FUNCTIONS_TABLE\s+(?<body>.*?)\r?\n\r?\nclass\s+ScriptFunctions',
            [System.Text.RegularExpressions.RegexOptions]::Singleline)

        if (-not $functionsMacro.Success) {
            throw "Could not find ScriptFunctions function table in $scriptFunctionsHeader"
        }

        $functionsBody = $functionsMacro.Groups["body"].Value
    }

    foreach ($match in [regex]::Matches($functionsBody, '\{\s*"([^"]+)"\s*,')) {
        Add-Name -Names $names -Name $match.Groups[1].Value
    }

    return $names
}

function Get-DocumentedTes3mpApiNames {
    param(
        [string]$BaseUrl,
        [string[]]$Pages
    )

    if (-not $BaseUrl.EndsWith("/")) {
        $BaseUrl = "$BaseUrl/"
    }

    $names = [ordered]@{}

    foreach ($page in $Pages) {
        $uri = "$BaseUrl$page"
        $html = (Invoke-WebRequest -UseBasicParsing -Uri $uri).Content

        foreach ($match in [regex]::Matches($html, 'id="[A-Za-z0-9_]+Functions::([A-Za-z_][A-Za-z0-9_]*?)(?:__[^"]+)?"')) {
            $name = $match.Groups[1].Value
            if ($name -eq "_MessageBox") {
                $name = "MessageBox"
            }
            Add-Name -Names $names -Name $name
        }
    }

    return $names
}

function Get-DocumentedTes3mpApiNamesFromFile {
    param([string]$Path)

    $names = [ordered]@{}

    foreach ($line in Get-Content -LiteralPath $Path) {
        $name = $line.Trim()
        if ($name -eq "" -or $name.StartsWith("#")) {
            continue
        }

        Add-Name -Names $names -Name $name
    }

    return $names
}

$localNames = Get-LocalTes3mpApiNames -Root $SourceRoot
$documentedNameSource = $DocumentedNamesPath

if ((Test-Path -LiteralPath $DocumentedNamesPath -PathType Leaf) -and -not $RefreshDocumentedNames) {
    $documentedNames = Get-DocumentedTes3mpApiNamesFromFile -Path $DocumentedNamesPath
} else {
    $documentedNames = Get-DocumentedTes3mpApiNames -BaseUrl $DocsBaseUrl -Pages $docsPages
    $documentedNameSource = $DocsBaseUrl

    if ($RefreshDocumentedNames -and $DocumentedNamesPath -ne "") {
        $documentedNamesDirectory = Split-Path -Parent $DocumentedNamesPath
        if (-not (Test-Path -LiteralPath $documentedNamesDirectory -PathType Container)) {
            New-Item -ItemType Directory -Path $documentedNamesDirectory | Out-Null
        }

        $generatedDate = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
        $cacheLines = @(
            "# Generated from $DocsBaseUrl on $generatedDate."
            "# One TES3MP Lua tes3mp.* binding name per line."
            $documentedNames.Keys | Sort-Object
        )

        Set-Content -LiteralPath $DocumentedNamesPath -Value $cacheLines -Encoding utf8
        $documentedNameSource = $DocumentedNamesPath
    }
}

$missingDocumentedNames = @(
    $documentedNames.Keys |
        Where-Object { -not $localNames.Contains($_) } |
        Sort-Object
)

$localExtensions = @(
    $localNames.Keys |
        Where-Object { -not $documentedNames.Contains($_) } |
        Sort-Object
)

Write-Host "TES3MP Lua API surface check"
Write-Host "Source root: $SourceRoot"
Write-Host "Documented function source: $documentedNameSource"
Write-Host "Documented functions: $($documentedNames.Count)"
Write-Host "Local functions: $($localNames.Count)"
Write-Host "Missing documented functions: $($missingDocumentedNames.Count)"
Write-Host "Local extensions: $($localExtensions.Count)"

if ($missingDocumentedNames.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing documented functions:"
    foreach ($name in $missingDocumentedNames) {
        Write-Host "  $name"
    }
}

if ($localExtensions.Count -gt 0) {
    Write-Host ""
    Write-Host "Local functions not present in the 0.8.1 docs:"
    foreach ($name in $localExtensions | Select-Object -First 100) {
        Write-Host "  $name"
    }
    if ($localExtensions.Count -gt 100) {
        Write-Host "  ... $($localExtensions.Count - 100) more"
    }
}

if ($FailOnMissingDocumentedFunction -and $missingDocumentedNames.Count -gt 0) {
    exit 1
}
