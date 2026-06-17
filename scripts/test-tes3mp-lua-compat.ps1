[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [string]$BuildDir = "C:\tes3mp_refresh\openmw\MSVC2022_64_Ninja",
    [string]$Configuration = "RelWithDebInfo",
    [string]$CommunityScriptRoot = "",
    [switch]$SkipBuild,
    [switch]$SkipCommunitySample,
    [switch]$SkipDocsApiSurface,
    [switch]$StaticOnly,
    [switch]$CheckDocsApiSurface,
    [switch]$RunRuntimeSmoke,
    [switch]$RuntimeSmokeWithLocalMaster,
    [int]$RuntimeSmokeTimeoutSeconds = 12,
    [Alias("RuntimeSmokeBrowserProbeIterations")]
    [int]$RuntimeSmokeHubProbeIterations = 3
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

$communityParseTest = "Tes3mpServerLuaCompatibilityTest.CommunityScriptSampleParsesWhenRootIsProvided"
$communityHookLoadTest = "Tes3mpServerLuaCompatibilityTest.CommunityScriptSampleRegistersHooksWhenRootIsProvided"
$communityCallbackTest = "Tes3mpServerLuaCompatibilityTest.CommunityScriptSampleExecutesRepresentativeCallbacksWhenRootIsProvided"
$communityTestFilter = "$communityParseTest`:$communityHookLoadTest`:$communityCallbackTest"
$coreFilter = "Tes3mpServerLuaCompatibilityTest.*:TimerApiTest.*:ClientSettingsTest.*:Tes3mpEndpointTest.*:MpBasePacketTest.*:MpPacketStreamTest.*:GnsTransportTest.*-$communityTestFilter"

if ($StaticOnly -and $RunRuntimeSmoke) {
    throw "-RunRuntimeSmoke cannot be combined with -StaticOnly."
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-local-state-guards.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP local state guard check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-chat-buffering.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP chat buffering check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-lua-object-mutation-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP Lua object mutation sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-openmw-lua-world-cell-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP OpenMW Lua world/cell sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-gui-inventory-drop-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP GUI inventory drop sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-container-looting-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP container looting sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-gns-preinit-rejection-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP GNS pre-init rejection sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-login-world-entry-ordering.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP login/world-entry ordering check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-account-login-ux-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP account/login UX sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-character-persistence-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP character persistence sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-disconnect-cleanup-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP disconnect cleanup sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-ai-activation-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP AI activation sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-actor-movement-ai-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP actor movement/AI sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-player-movement-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP player movement sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-combat-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP combat sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-journal-topic-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP journal/topic sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-death-revive-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP death/revive sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-master-browser-sync.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingGuard
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP master/browser sync check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-recordstore-runtime-coverage.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingCoverage
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP record-store runtime coverage check failed with exit code $LASTEXITCODE"
}

function Resolve-CommunityScriptRoot {
    param([string]$RequestedRoot)

    if ($RequestedRoot -ne "") {
        return (Resolve-Path -LiteralPath $RequestedRoot).Path
    }

    $environmentCommunityRoot = [Environment]::GetEnvironmentVariable("TES3MP_LUA_COMMUNITY_SCRIPT_ROOT", "Process")
    if (-not [string]::IsNullOrWhiteSpace($environmentCommunityRoot)) {
        return (Resolve-Path -LiteralPath $environmentCommunityRoot).Path
    }

    $candidate = Join-Path ([Environment]::GetFolderPath("UserProfile")) "Downloads\random tes3mp scripts"
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }

    return ""
}

function Invoke-RuntimeSmoke {
    param(
        [string]$SourceRoot,
        [string]$BuildDir,
        [string]$Configuration,
        [int]$TimeoutSeconds,
        [int]$HubProbeIterations,
        [bool]$WithLocalMaster
    )

    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $SourceRoot "scripts\smoke-tes3mp-runtime.ps1"),
        "-BuildDir", $BuildDir,
        "-Configuration", $Configuration,
        "-TimeoutSeconds", $TimeoutSeconds,
        "-HubProbeIterations", $HubProbeIterations
    )

    if ($WithLocalMaster) {
        $arguments += "-WithLocalMaster"
    }

    powershell @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "TES3MP runtime smoke failed with exit code $LASTEXITCODE"
    }
}

if ($CheckDocsApiSurface -or -not $SkipDocsApiSurface) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-lua-api-surface.ps1") `
        -SourceRoot $SourceRoot -FailOnMissingDocumentedFunction
    if ($LASTEXITCODE -ne 0) {
        throw "TES3MP Lua API surface check failed with exit code $LASTEXITCODE"
    }
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-lua-callback-surface.ps1") `
    -SourceRoot $SourceRoot -FailOnMissingCallbackBridge
if ($LASTEXITCODE -ne 0) {
    throw "TES3MP Lua callback surface check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-lua-event-hook-surface.ps1") `
    -SourceRoot $SourceRoot -FailOnUntriggeredHook
if ($LASTEXITCODE -ne 0) {
    throw "Bundled Lua event hook surface check failed with exit code $LASTEXITCODE"
}

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\analyze-tes3mp-lua-compat.ps1") `
    -SourceRoot $SourceRoot -FailOnUnknownTes3mpCall
if ($LASTEXITCODE -ne 0) {
    throw "Bundled Lua compatibility analyzer failed with exit code $LASTEXITCODE"
}

$resolvedCommunityScriptRoot = ""
if (-not $SkipCommunitySample) {
    $resolvedCommunityScriptRoot = Resolve-CommunityScriptRoot -RequestedRoot $CommunityScriptRoot

    if ($resolvedCommunityScriptRoot -ne "") {
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\check-tes3mp-lua-event-hook-surface.ps1") `
            -SourceRoot $SourceRoot -ScriptRoot $resolvedCommunityScriptRoot -FailOnUntriggeredHook
        if ($LASTEXITCODE -ne 0) {
            throw "Community Lua event hook surface check failed with exit code $LASTEXITCODE"
        }

        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceRoot "scripts\analyze-tes3mp-lua-compat.ps1") `
            -SourceRoot $SourceRoot -ScriptRoot $resolvedCommunityScriptRoot -FailOnUnknownTes3mpCall
        if ($LASTEXITCODE -ne 0) {
            throw "Community Lua compatibility analyzer failed with exit code $LASTEXITCODE"
        }
    }
}

if ($StaticOnly) {
    if ($SkipCommunitySample) {
        Write-Host "Skipped community Lua sample validation."
    } elseif ($resolvedCommunityScriptRoot -eq "") {
        Write-Host "No community Lua sample root provided. Set -CommunityScriptRoot or TES3MP_LUA_COMMUNITY_SCRIPT_ROOT to validate one."
    }
    Write-Host "TES3MP Lua static compatibility validation passed."
    exit 0
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$componentsTests = Join-Path $BuildDir "$Configuration\components-tests.exe"

if (-not $SkipBuild) {
    $vsDevCmd = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
        throw "Visual Studio developer command prompt not found: $vsDevCmd"
    }

    $buildCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && cmake --build `"$BuildDir`" --config $Configuration --parallel --target components-tests.exe"
    cmd.exe /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "components-tests.exe build failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $componentsTests -PathType Leaf)) {
    throw "components-tests.exe not found: $componentsTests"
}

& $componentsTests "--gtest_filter=$coreFilter"
if ($LASTEXITCODE -ne 0) {
    throw "Core TES3MP Lua compatibility gate failed with exit code $LASTEXITCODE"
}

if ($SkipCommunitySample) {
    Write-Host "Skipped community Lua sample validation."
    if ($RunRuntimeSmoke) {
        Invoke-RuntimeSmoke -SourceRoot $SourceRoot -BuildDir $BuildDir -Configuration $Configuration `
            -TimeoutSeconds $RuntimeSmokeTimeoutSeconds -HubProbeIterations $RuntimeSmokeHubProbeIterations `
            -WithLocalMaster $RuntimeSmokeWithLocalMaster
    }
    exit 0
}

if ($resolvedCommunityScriptRoot -eq "") {
    Write-Host "No community Lua sample root provided. Set -CommunityScriptRoot or TES3MP_LUA_COMMUNITY_SCRIPT_ROOT to validate one."
    if ($RunRuntimeSmoke) {
        Invoke-RuntimeSmoke -SourceRoot $SourceRoot -BuildDir $BuildDir -Configuration $Configuration `
            -TimeoutSeconds $RuntimeSmokeTimeoutSeconds -HubProbeIterations $RuntimeSmokeHubProbeIterations `
            -WithLocalMaster $RuntimeSmokeWithLocalMaster
    }
    exit 0
}

$previousCommunityRoot = [Environment]::GetEnvironmentVariable("TES3MP_LUA_COMMUNITY_SCRIPT_ROOT", "Process")
try {
    [Environment]::SetEnvironmentVariable("TES3MP_LUA_COMMUNITY_SCRIPT_ROOT", $resolvedCommunityScriptRoot, "Process")
    & $componentsTests "--gtest_filter=$communityTestFilter"
    if ($LASTEXITCODE -ne 0) {
        throw "Community Lua smoke failed with exit code $LASTEXITCODE"
    }
}
finally {
    [Environment]::SetEnvironmentVariable("TES3MP_LUA_COMMUNITY_SCRIPT_ROOT", $previousCommunityRoot, "Process")
}

if ($RunRuntimeSmoke) {
    Invoke-RuntimeSmoke -SourceRoot $SourceRoot -BuildDir $BuildDir -Configuration $Configuration `
        -TimeoutSeconds $RuntimeSmokeTimeoutSeconds -HubProbeIterations $RuntimeSmokeHubProbeIterations `
        -WithLocalMaster $RuntimeSmokeWithLocalMaster
}

Write-Host "TES3MP Lua compatibility validation passed."
