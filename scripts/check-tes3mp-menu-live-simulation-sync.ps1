[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingGuard
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

function Test-Pattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

$engine = Get-SourceText "apps\openmw\engine.cpp"
$dateTimeManager = Get-SourceText "apps\openmw\mwworld\datetimemanager.cpp"
$clientMainHeader = Get-SourceText "apps\openmw\mwmp\Main.hpp"
$clientMain = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$clientLocalPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$luaManager = Get-SourceText "apps\openmw\mwlua\luamanagerimp.cpp"
$coreBindings = Get-SourceText "apps\openmw\mwlua\corebindings.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Multiplayer pause bypass is limited to initialized logged-in local players" -Text ($clientMainHeader + "`n" + $clientMain + "`n" + $clientLocalPlayer) `
    -Pattern 'static\s+bool\s+shouldRunWorldWhilePaused\(\);.*bool\s+Main::shouldRunWorldWhilePaused\(\).*isInitialized\(\).*get\(\)\.getLocalPlayer\(\)\s*!=\s*nullptr.*get\(\)\.getLocalPlayer\(\)->isLoggedIn\(\).*bool\s+LocalPlayer::isLoggedIn\(\)\s+const.*return\s+hasLoadedCharacter\(\);' `
    -Missing $missing

Test-Pattern -Name "Engine frame keeps world mechanics physics and scripts live while multiplayer menus are open" -Text $engine `
    -Pattern 'bool\s+paused\s*=\s*mWorld->getTimeManager\(\)->isPaused\(\);.*if\s*\(mwmp::Main::shouldRunWorldWhilePaused\(\)\)\s*paused\s*=\s*false;.*if\s*\(!mWindowManager->containsMode\(MWGui::GM_MainMenu\)\s*\|\|\s*!paused\).*if\s*\(!paused\).*mWorld->advanceTime\(hours,\s*true\);.*mMechanicsManager->update\(frametime,\s*paused\);.*mWorld->updatePhysics\(frametime,\s*paused,.*mWorld->update\(frametime,\s*paused\);' `
    -Missing $missing

Test-Pattern -Name "Rendering simulation timestamps advance during logged-in multiplayer menu modes" -Text $engine `
    -Pattern 'timeManager\.updateIsPaused\(\);.*bool\s+advanceSimulationTime\s*=\s*!timeManager\.isPaused\(\);.*if\s*\(mwmp::Main::shouldRunWorldWhilePaused\(\)\)\s*advanceSimulationTime\s*=\s*true;.*if\s*\(advanceSimulationTime\).*timeManager\.setSimulationTime\(timeManager\.getSimulationTime\(\)\s*\+\s*dt\);.*timeManager\.setRenderingSimulationTime\(timeManager\.getRenderingSimulationTime\(\)\s*\+\s*dt\);' `
    -Missing $missing

Test-Pattern -Name "Lua timers and frame callbacks use effective multiplayer pause state" -Text $luaManager `
    -Pattern 'bool\s+isWorldEffectivelyPaused\(const\s+MWWorld::DateTimeManager&\s+timeManager\).*mwmp::Main::shouldRunWorldWhilePaused\(\).*return\s+false;.*return\s+timeManager\.isPaused\(\);.*if\s*\(!isWorldEffectivelyPaused\(timeManager\)\).*mMenuScripts\.processTimers.*mGlobalScripts\.processTimers.*processTimers\(timeManager\.getSimulationTime\(\),\s*timeManager\.getGameTime\(\)\).*bool\s+isPaused\s*=\s*isWorldEffectivelyPaused\(timeManager\);.*asLocal\(ptr\)->update\(isPaused\s*\?\s*0\s*:\s*frameDuration\);.*mGlobalScripts\.update\(isPaused\s*\?\s*0\s*:\s*frameDuration\);.*mInputActions\.update\(frameDuration\);.*mMenuScripts\.onFrame\(frameDuration\);.*playerScripts->onFrame\(frameDuration\);' `
    -Missing $missing

Test-Pattern -Name "Lua isWorldPaused reports effective multiplayer pause state" -Text $coreBindings `
    -Pattern 'bool\s+isWorldEffectivelyPaused\(MWWorld::DateTimeManager\*\s*timeManager\).*mwmp::Main::shouldRunWorldWhilePaused\(\).*return\s+false;.*return\s+timeManager->isPaused\(\);.*api\["isWorldPaused"\]\s*=\s*\[timeManager\]\(\)\s*\{\s*return\s+isWorldEffectivelyPaused\(timeManager\);\s*\};' `
    -Missing $missing

Test-Pattern -Name "Singleplayer pause sources still include GUI pause tags console message boxes and no-game state" -Text $dateTimeManager `
    -Pattern 'void\s+DateTimeManager::updateIsPaused\(\).*mPaused\s*=\s*!mPausedTags\.empty\(\)\s*\|\|\s*wm->isConsoleMode\(\)\s*\|\|\s*wm->isPostProcessorHudVisible\(\).*wm->isInteractiveMessageBoxActive\(\).*stateManager->getState\(\)\s*==\s*MWBase::StateManager::State_NoGame;' `
    -Missing $missing

Write-Host "TES3MP menu live simulation sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 6"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
