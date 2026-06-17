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

$stateManager = Get-SourceText "apps\openmw\mwstate\statemanagerimp.cpp"
$mainMenu = Get-SourceText "apps\openmw\mwgui\mainmenu.cpp"
$menuScripts = Get-SourceText "apps\openmw\mwlua\menuscripts.cpp"
$actionManager = Get-SourceText "apps\openmw\mwinput\actionmanager.cpp"
$waitDialog = Get-SourceText "apps\openmw\mwgui\waitdialog.cpp"
$engine = Get-SourceText "apps\openmw\engine.cpp"
$actors = Get-SourceText "apps\openmw\mwmechanics\actors.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "StateManager::askLoadRecent denies TES3MP recent-load prompts" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::askLoadRecent\(\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("recent save load prompt"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "Player death animation completion bypasses single-player recent-load prompt during TES3MP sessions" -Text $actors `
    -Pattern 'if\s*\(isPlayer\)\s*\{.*#ifdef\s+BUILD_TES3MP_CLIENT.*if\s*\(mwmp::Main::isInitialized\(\)\).*continue;.*#endif.*getStateManager\(\)->askLoadRecent\(\);' `
    -Missing $missing
Test-Pattern -Name "StateManager::requestNewGame denies queued TES3MP new-game requests" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::requestNewGame\(\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("new game request"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::requestLoad denies queued TES3MP load requests" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::requestLoad\([^)]*\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("load request"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::newGame denies direct TES3MP new games while preserving bypass startup" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::newGame\(bool\s+bypass\)\s*\{\s*if\s*\(\s*!bypass\s*&&\s*denyLocalSaveLoadInMultiplayer\("new game"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::saveGame denies TES3MP local saves" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::saveGame\([^)]*\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("save"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::quickSave denies TES3MP quicksave and autosave" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::quickSave\([^)]*\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("quick save"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::loadGame(path) denies TES3MP direct path loads" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::loadGame\(const\s+std::filesystem::path&\s+filepath\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("load"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::loadGame(character,path) denies TES3MP slot loads" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::loadGame\(const\s+Character\*\s+character,\s*const\s+std::filesystem::path&\s+filepath\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("load"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::quickLoad denies TES3MP quickload" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::quickLoad\(\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("quick load"\)\s*\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "StateManager::deleteGame denies TES3MP local save deletion" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::deleteGame\([^)]*\)\s*\{\s*if\s*\(\s*denyLocalSaveLoadInMultiplayer\("save deletion"\)\s*\)\s*return;' `
    -Missing $missing

Test-Pattern -Name "Main menu hides New Game during TES3MP sessions" -Text $mainMenu `
    -Pattern 'if\s*\(\s*!multiplayerSession\s*\)\s*buttons\.emplace_back\("newgame"\);' `
    -Missing $missing
Test-Pattern -Name "Main menu hides Save Game during TES3MP sessions" -Text $mainMenu `
    -Pattern 'if\s*\(\s*!multiplayerSession\s*&&\s*state\s*==\s*MWBase::StateManager::State_Running' `
    -Missing $missing
Test-Pattern -Name "Main menu hides Load Game during TES3MP sessions" -Text $mainMenu `
    -Pattern 'if\s*\(\s*!multiplayerSession\s*&&\s*MWBase::Environment::get\(\)\.getStateManager\(\)->characterBegin\(\)' `
    -Missing $missing

Test-Pattern -Name "Menu Lua newGame routes through guarded queued new-game request" -Text $menuScripts `
    -Pattern 'api\["newGame"\]\s*=\s*\[\]\(\)\s*\{\s*MWBase::Environment::get\(\)\.getStateManager\(\)->requestNewGame\(\);\s*\};' `
    -Missing $missing
Test-Pattern -Name "Menu Lua loadGame routes through guarded queued load request" -Text $menuScripts `
    -Pattern 'MWBase::Environment::get\(\)\.getStateManager\(\)->requestLoad\(character,\s*slot->mPath\);' `
    -Missing $missing
Test-Pattern -Name "Menu Lua saveGame routes through guarded saveGame" -Text $menuScripts `
    -Pattern 'manager->saveGame\(description,\s*slot\);' `
    -Missing $missing
Test-Pattern -Name "Menu Lua deleteGame routes through guarded deleteGame" -Text $menuScripts `
    -Pattern 'MWBase::Environment::get\(\)\.getStateManager\(\)->deleteGame\(character,\s*slot\);' `
    -Missing $missing

Test-Pattern -Name "Keyboard quickload routes through guarded quickLoad" -Text $actionManager `
    -Pattern 'getStateManager\(\)->quickLoad\(\);' `
    -Missing $missing
Test-Pattern -Name "Keyboard quicksave routes through guarded quickSave" -Text $actionManager `
    -Pattern 'getStateManager\(\)->quickSave\(\);' `
    -Missing $missing
Test-Pattern -Name "Wait/rest autosave routes through guarded quickSave" -Text $waitDialog `
    -Pattern 'getStateManager\(\)->quickSave\("Autosave"\);' `
    -Missing $missing
Test-Pattern -Name "Startup --load-savegame routes through guarded loadGame after TES3MP init" -Text $engine `
    -Pattern '#ifdef BUILD_TES3MP_CLIENT.*mwmp::Main::init\(mContentFiles,\s*mFileCollections\).*mSkipMenu\s*=\s*true;.*if\s*\(!mSaveGameFile\.empty\(\)\).*mStateManager->loadGame\(mSaveGameFile\);' `
    -Missing $missing

Write-Host "TES3MP local state guard check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 23"
Write-Host "Missing guards: $($missing.Count)"

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing or changed guard patterns:"
    foreach ($item in $missing) {
        Write-Host "  $item"
    }

    if ($FailOnMissingGuard) {
        exit 1
    }
}
