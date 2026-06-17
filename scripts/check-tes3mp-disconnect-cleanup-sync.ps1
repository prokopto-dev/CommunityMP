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

$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$cellBase = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$serverFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Server.cpp"
$runtimeSmoke = Get-SourceText "scripts\smoke-tes3mp-runtime.ps1"
$serverLuaCompat = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "ServerCore forwards OnPlayerDisconnect into the legacy event handler" -Text $serverCore `
    -Pattern 'function\s+OnPlayerDisconnect\(pid\).*tes3mp\.LogMessage\(enumerations\.log\.INFO,\s*"Called \\"OnPlayerDisconnect\\" for "\s*\.\.\s*logicHandler\.GetChatName\(pid\)\).*eventHandler\.OnPlayerDisconnect\(pid\)' `
    -Missing $missing

Test-Pattern -Name "Duplicate account replacement happens only after authenticated login" -Text ($serverCore + "`n" + $logicHandler + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*function\s+OnPlayerConnect\(pid\).*if\s+not\s+logicHandler\.IsNameAllowed\(playerName\).*tes3mp\.Kick\(pid\).*else\s+tes3mp\.LogAppend\(enumerations\.log\.INFO,\s*"- New player is named "\s*\.\.\s*playerName\)\s+eventHandler\.OnPlayerConnect\(pid,\s*playerName\))(?=.*local\s+PlayerHasAuthenticatedAccount\s*=\s*function\(player\).*player\.HasAuthenticatedAccount.*player:HasAuthenticatedAccount\(\).*player\.accountAuthenticated\s*==\s*true.*player:IsLoggedIn\(\))(?=.*logicHandler\.DisconnectAuthenticatedAccountSessions\s*=\s*function\(accountName,\s*replacementPid\).*pid\s*~=\s*replacementPid.*PlayerHasAuthenticatedAccount\(player\).*eventHandler\.OnPlayerDisconnect\(pid\))(?=.*local\s+ProcessAccountPassword\s*=\s*function\(pid,\s*idGui,\s*data\).*Players\[pid\]:LoadFromDrive\(\).*Players\[pid\]\.data\.login\.passwordHash\s*~=.*return\s+false.*local\s+replacedSessionCount\s*=\s*logicHandler\.DisconnectAuthenticatedAccountSessions\(Players\[pid\]\.accountName,\s*pid\).*if\s+replacedSessionCount\s*>\s*0\s+then\s+Players\[pid\]:LoadFromDrive\(\)\s+end\s+Players\[pid\]\.accountAuthenticated\s*=\s*true)(?=.*LogicHandlerDisconnectsAuthenticatedDuplicateAccountLogin)(?=.*DisconnectAuthenticatedAccountSessions:ServerAccount:31)(?=.*loadFromDriveCount\s*==\s*2)' `
    -Missing $missing

Test-Pattern -Name "Disconnect removes pre-login unassigned clients from IP tracking" -Text $eventHandler `
    -Pattern 'eventHandler\.OnPlayerDisconnect\s*=\s*function\(pid\).*if\s+tes3mp\.GetIP\(pid\)\s*==\s*"UNASSIGNED_SYSTEM_ADDRESS"\s+then\s+for\s+ipAddress,\s+pids\s+in\s+pairs\(pidsByIpAddress\)\s+do\s+if\s+tableHelper\.containsValue\(pids,\s*pid\)\s+then\s+tableHelper\.removeValue\(pids,\s*pid\)' `
    -Missing $missing

Test-Pattern -Name "GetIP preserves the legacy unassigned address sentinel over PacketAddress" -Text $serverFunctions `
    -Pattern 'ServerFunctions::GetIP\(unsigned\s+short\s+pid\).*const\s+mwmp::PacketAddress\s+packetAddress\s*=\s*mwmp::Networking::getPtr\(\)->getPacketAddress\(player->guid\);.*if\s*\(!mwmp::isPacketAddressAssigned\(packetAddress\)\)\s+return\s+"UNASSIGNED_SYSTEM_ADDRESS";.*address\s*=\s*mwmp::packetAddressToString\(packetAddress,\s*false\);' `
    -Missing $missing

Test-Pattern -Name "Logged-in disconnect runs validators and removes IP tracking before optional default cleanup" -Text $eventHandler `
    -Pattern 'if\s+Players\[pid\]:IsLoggedIn\(\)\s+then\s+local\s+eventStatus\s*=\s*customEventHooks\.triggerValidators\("OnPlayerDisconnect",\s*\{pid\}\)\s+local\s+ipAddress\s*=\s*Players\[pid\]\.ipAddress\s+if\s+ipAddress\s*~=\s*nil\s+and\s+pidsByIpAddress\[ipAddress\]\s*~=\s*nil\s+and\s+tableHelper\.containsValue\(pidsByIpAddress\[ipAddress\],\s*pid\)\s+then\s+tableHelper\.removeValue\(pidsByIpAddress\[ipAddress\],\s*pid\)\s+end\s+if\s+eventStatus\.validDefaultHandler\s+then' `
    -Missing $missing

Test-Pattern -Name "Disconnect default cleanup persists timestamps, active spell time, summons, cell, stats, and player data" -Text $eventHandler `
    -Pattern 'if\s+eventStatus\.validDefaultHandler\s+then\s+Players\[pid\]\.data\.timestamps\.lastDisconnect\s*=\s*os\.time\(\)\s+Players\[pid\]\.data\.timestamps\.lastSessionDuration\s*=\s*os\.time\(\)\s*-\s*Players\[pid\]\.data\.timestamps\.lastLogin.*Players\[pid\]:UpdateActiveSpellTimes\(\).*Players\[pid\]:DeleteSummons\(\).*local\s+pendingServerLocationChange\s*=\s*nil.*if\s+type\(Players\[pid\]\.GetPendingServerLocationChange\)\s*==\s*"function"\s+then\s+pendingServerLocationChange\s*=\s*Players\[pid\]:GetPendingServerLocationChange\(\)\s+end\s+if\s+pendingServerLocationChange\s*~=\s*nil\s+then.*keeping server-authored location.*else\s+Players\[pid\]:SaveCell\(packetReader\.GetPlayerPacketTables\(pid,\s*"PlayerCellChange"\)\)\s+end.*Players\[pid\]:SaveStatsDynamic\(packetReader\.GetPlayerPacketTables\(pid,\s*"PlayerStatsDynamic"\)\).*Players\[pid\]:SaveToDrive\(\)' `
    -Missing $missing

Test-Pattern -Name "Disconnect stale confiscation cleanup tolerates missing targets before clearing state" -Text $eventHandler `
    -Pattern 'if\s+Players\[pid\]\.confiscationTargetName\s*~=\s*nil\s+then\s+local\s+targetName\s*=\s*Players\[pid\]\.confiscationTargetName\s+local\s+targetPlayer\s*=\s*logicHandler\.GetLoggedInPlayerByName\(targetName\)\s+if\s+targetPlayer\s*~=\s*nil\s+then\s+targetPlayer:SetConfiscationState\(false\)\s+end\s+Players\[pid\]\.confiscationTargetName\s*=\s*nil' `
    -Missing $missing

Test-Pattern -Name "Disconnect unloads a snapshot of loaded cells so mutation during unload cannot skip cells" -Text $eventHandler `
    -Pattern 'local\s+cellsToUnload\s*=\s*\{\}\s+for\s+_,\s*loadedCellDescription\s+in\s+pairs\(Players\[pid\]\.cellsLoaded\)\s+do\s+table\.insert\(cellsToUnload,\s*loadedCellDescription\)\s+end\s+for\s+_,\s*loadedCellDescription\s+in\s+pairs\(cellsToUnload\)\s+do\s+local\s+eventStatus\s*=\s*customEventHooks\.triggerValidators\("OnCellUnload",\s*\{pid,\s*loadedCellDescription\}\).*logicHandler\.UnloadCellForPlayer\(pid,\s*loadedCellDescription\).*customEventHooks\.triggerHandlers\("OnCellUnload",\s*eventStatus,\s*\{pid,\s*loadedCellDescription\}\)' `
    -Missing $missing

Test-Pattern -Name "Disconnect unloads the player's current region after cell unloads" -Text $eventHandler `
    -Pattern 'if\s+Players\[pid\]\.data\.location\.regionName\s*~=\s*nil\s+then\s+logicHandler\.UnloadRegionForPlayer\(pid,\s*Players\[pid\]\.data\.location\.regionName\)\s+end\s+end\s+customEventHooks\.triggerHandlers\("OnPlayerDisconnect",\s*eventStatus,\s*\{pid\}\)' `
    -Missing $missing

Test-Pattern -Name "Disconnect removes non-logged-in players from IP tracking and always destroys the player object" -Text $eventHandler `
    -Pattern 'else\s+local\s+ipAddress\s*=\s*Players\[pid\]\.ipAddress\s+or\s+tes3mp\.GetIP\(pid\).*if\s+ipAddress\s*~=\s*nil\s+and\s+pidsByIpAddress\[ipAddress\]\s*~=\s*nil\s+and\s+tableHelper\.containsValue\(pidsByIpAddress\[ipAddress\],\s*pid\)\s+then\s+tableHelper\.removeValue\(pidsByIpAddress\[ipAddress\],\s*pid\).*Players\[pid\]:Destroy\(\)\s+Players\[pid\]\s*=\s*nil' `
    -Missing $missing

Test-Pattern -Name "Empty server disconnect persists world data and record stores after player teardown" -Text $eventHandler `
    -Pattern 'if\s+tableHelper\.isEmpty\(Players\)\s+then\s+WorldInstance:SaveToDrive\(\)\s+for\s+_,\s*recordStore\s+in\s+pairs\(RecordStores\)\s+do\s+recordStore:DeleteUnlinkedRecords\(\)\s+recordStore:SaveToDrive\(\)' `
    -Missing $missing

Test-Pattern -Name "Cell unload removes visitors, saves actor state, quicksaves the cell, and reassigns authority when needed" -Text $logicHandler `
    -Pattern 'logicHandler\.UnloadCellForPlayer\s*=\s*function\(pid,\s*cellDescription\).*local\s+cell\s*=\s*LoadedCells\[cellDescription\].*local\s+wasAuthority\s*=\s*cell:GetAuthority\(\)\s*==\s*pid.*cell:RemoveVisitor\(pid\).*cell:SaveActorPositions\(\).*cell:SaveActorStatsDynamic\(\).*cell:SaveActorAI\(\).*cell:QuicksaveToDrive\(\).*if\s+cell\.isResetting\s*==\s*false\s+then.*if\s+wasAuthority\s+then\s+assignCellAuthority\(cell,\s*"current authority left",\s*nil,\s*pid\).*elseif\s+cell\.authority\s*~=\s*nil\s+and\s+not\s+isCellAuthorityCandidate\(cell,\s*cell\.authority\)\s+then\s+assignCellAuthority\(cell,\s*"cell authority was stale after visitor left"\).*requestMissingCellSnapshots\(cell\)' `
    -Missing $missing

Test-Pattern -Name "Cell visitor removal updates the player's loaded-cell list and records last visit" -Text $cellBase `
    -Pattern 'local\s+function\s+getPlayerCellVisitKey\(pid\).*Players\[pid\]:GetCellVisitKey\(\).*function\s+BaseCell:RemoveVisitor\(pid\).*if\s+tableHelper\.containsValue\(self\.visitors,\s*pid\)\s+then\s+tableHelper\.removeValue\(self\.visitors,\s*pid\).*Players\[pid\]:RemoveCellLoaded\(self\.description\).*self:SaveLastVisit\(getPlayerCellVisitKey\(pid\)\)' `
    -Missing $missing

Test-Pattern -Name "Region unload removes visitors and clears or transfers region authority" -Text $logicHandler `
    -Pattern 'logicHandler\.UnloadRegionForPlayer\s*=\s*function\(pid,\s*regionName\).*WorldInstance:RemoveRegionVisitor\(pid,\s*regionName\).*if\s+WorldInstance:GetRegionAuthority\(regionName\)\s*==\s*pid\s+then.*local\s+visitors\s*=\s*WorldInstance\.storedRegions\[regionName\]\.visitors.*if\s+tableHelper\.getCount\(visitors\)\s*>\s*0\s+then\s+local\s+newAuthorityPid\s*=\s*logicHandler\.GetLowestPingPid\(visitors\).*WorldInstance:SetRegionAuthority\(newAuthorityPid,\s*regionName\).*else\s+WorldInstance\.storedRegions\[regionName\]\.authority\s*=\s*nil' `
    -Missing $missing

Test-Pattern -Name "Player SaveCell preserves existing location on empty packets and saves map exploration on valid packets" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:SaveCell\(playerPacket\).*if\s+playerPacket\s*==\s*nil\s+or\s+playerPacket\.location\s*==\s*nil\s+or\s+playerPacket\.location\.cell\s*==\s*nil\s+or\s+playerPacket\.location\.cell\s*==\s*""\s+then.*keeping saved cell.*return.*self\.data\.location\.cell\s*=\s*playerPacket\.location\.cell.*self\.data\.location\.posX\s*=\s*playerPacket\.location\.posX.*self\.data\.location\.posY\s*=\s*playerPacket\.location\.posY.*self\.data\.location\.posZ\s*=\s*playerPacket\.location\.posZ.*stateHelper:SaveMapExploration\(self\.pid,\s*self\)' `
    -Missing $missing

Test-Pattern -Name "Player SaveStatsDynamic persists current dynamic stats while respecting pending death state" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:SaveStatsDynamic\(playerPacket\).*self:NormalizeDeathState\(\).*if\s+self\.data\.death\.isDead\s*==\s*true\s+then\s+self\.data\.stats\.healthCurrent\s*=\s*0\s+return\s+end.*local\s+healthBase\s*=\s*playerPacket\.stats\.healthBase.*if\s+healthBase\s*>\s*1\s+then.*self\.data\.stats\.magickaBase\s*=\s*playerPacket\.stats\.magickaBase.*self\.data\.stats\.fatigueBase\s*=\s*playerPacket\.stats\.fatigueBase.*self\.data\.stats\.healthCurrent\s*=\s*playerPacket\.stats\.healthCurrent.*self\.data\.stats\.magickaCurrent\s*=\s*playerPacket\.stats\.magickaCurrent.*self\.data\.stats\.fatigueCurrent\s*=\s*playerPacket\.stats\.fatigueCurrent' `
    -Missing $missing

Test-Pattern -Name "Active spell disconnect cleanup subtracts elapsed time and removes expired instances" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:UpdateActiveSpellTimes\(\).*local\s+timeSinceCast\s*=\s*os\.time\(\)\s*-\s*spellInstanceValues\.startTime.*if\s+timeSinceCast\s*<=\s*0\s+then\s+self\.data\.spellsActive\[spellId\]\[spellInstanceIndex\]\.effects\[effectIndex\]\s*=\s*nil\s+else\s+hadRemainingEffect\s*=\s*true.*effectTable\.timeLeft\s*=\s*effectTable\.timeLeft\s*-\s*timeSinceCast.*if\s+hadRemainingEffect\s*==\s*false\s+then\s+self\.data\.spellsActive\[spellId\]\[spellInstanceIndex\]\s*=\s*nil.*if\s+tableHelper\.getCount\(self\.data\.spellsActive\[spellId\]\)\s*==\s*0\s+then\s+self\.data\.spellsActive\[spellId\]\s*=\s*nil' `
    -Missing $missing

Test-Pattern -Name "Runtime smoke verifies disconnect save followed by immediate same-account reconnect" -Text $runtimeSmoke `
    -Pattern 'local\s+resultPath\s*=\s*"server/data/disconnect-flow-smoke\.txt".*local\s+relogPid\s*=\s*62.*local\s+clientPasswordHash\s*=\s*"disconnect-client-password-hash".*ShowCharacterList.*reconnect character list slot count.*eventHandler\.OnPlayerDisconnect\(pid\).*eventHandler\.OnPlayerConnect\(relogPid,\s*accountName\).*local\s+reloggedPlayer\s*=\s*Players\[relogPid\].*immediate reconnect logged in before character selection.*eventHandler\.OnGUIAction\(relogPid,\s*guiHelper\.ID\.CHARACTERLIST,\s*"0"\).*immediate reconnect character selection did not finish login.*assertEqual\(reloggedPlayer\.data\.location\.cell,\s*savedCell,\s*"reconnect saved cell"\).*assertEqual\(reloggedPlayer\.data\.stats\.healthCurrent,\s*31,\s*"reconnect saved health"\).*immediate reconnect did not add the new pid to IP tracking.*immediate reconnect left the old pid in IP tracking.*reconnect character list count.*writeResult\("OK\|"\s*\.\.\s*accountName\s*\.\.\s*"\|"\s*\.\.\s*characterName\s*\.\.\s*"\|disconnect\|reconnect"\)' `
    -Missing $missing

Write-Host "TES3MP disconnect cleanup sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 17"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
