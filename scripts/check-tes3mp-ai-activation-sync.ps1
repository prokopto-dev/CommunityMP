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

$main = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$cellController = Get-SourceText "apps\openmw\mwmp\CellController.cpp"
$cell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$actorAuthority = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorAuthority.hpp"
$actorListProcessor = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorList.hpp"
$actorAiProcessor = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorAI.hpp"
$actorList = Get-SourceText "apps\openmw\mwmp\ActorList.cpp"
$dedicatedActor = Get-SourceText "apps\openmw\mwmp\DedicatedActor.cpp"
$config = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$serverCell = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$commandHandler = Get-SourceText "files\tes3mp\server\scripts\commandHandler.lua"
$serverActorFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Actors.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Client disables AI before login and during unfinished chargen" -Text $main `
    -Pattern 'void\s+Main::frame\(float\s+dt\)\s*\{.*get\(\)\.getNetworking\(\)->update\(\);.*const\s+bool\s+loggedIn\s*=.*localPlayer->isLoggedIn\(\);.*if\s*\(!loggedIn\)\s*setAIActive\(false\);.*if\s*\(loggedIn\)\s*\{.*PlayerList::update\(dt\);.*updateDedicated\(dt\);.*get\(\)\.updateWorld\(dt\);.*\}.*if\s*\(!mLocalPlayer->processCharGen\(\)\)\s*\{.*setAIActive\(false\);.*return;.*\}' `
    -Missing $missing

Test-Pattern -Name "Client enables AI and local actor updates only from logged-in state" -Text $main `
    -Pattern 'const\s+bool\s+loggedIn\s*=\s*mLocalPlayer->isLoggedIn\(\);.*setAIActive\(loggedIn\);.*if\s*\(loggedIn\)\s*\{.*mLocalPlayer->update\(\);.*mCellController->updateLocal\(false\);' `
    -Missing $missing

Test-Pattern -Name "Local actor update flushes AI packets with authority-owned actor state" -Text $cell `
    -Pattern 'void\s+Cell::updateLocal\(bool\s+forceUpdate\)\s*\{.*actorList->sendPositionActors\(\);.*actorList->sendAnimFlagsActors\(\);.*actorList->sendAnimPlayActors\(\);.*actorList->sendSpeechActors\(\);.*actorList->sendAttackActors\(\);.*actorList->sendCastActors\(\);.*actorList->sendStatsDynamicActors\(\);.*actorList->sendDeathActors\(\);.*actorList->sendEquipmentActors\(\);.*actorList->sendAiActors\(\);.*actorList->sendCellChangeActors\(\);' `
    -Missing $missing

Test-Pattern -Name "Actor authority queues unloaded cells and applies full authority side effects when active" -Text ($cellController + "`n" + $actorAuthority) `
    -Pattern 'std::map<std::string,\s*PacketGuid>\s+CellController::queuedAuthorityGuids;.*applyQueuedActorAuthority\(mpCell->getCellStore\(\)->getCell\(\)->getEsm3\(\)\);.*if\s*\(isActiveWorldCell\(cell\)\)\s*applyQueuedActorAuthority\(cell\);.*void\s+CellController::applyActorAuthority\(const\s+ESM::Cell&\s+cell,\s*const\s+PacketGuid&\s+guid\).*mpCell->setAuthority\(guid\);.*guid\s*==\s*Main::get\(\)\.getLocalPlayer\(\)->guid.*mpCell->uninitializeDedicatedActors\(\);.*mpCell->initializeLocalActors\(\);.*mpCell->updateLocal\(true\);.*refreshNavigator\(\);.*mpCell->uninitializeLocalActors\(\);.*void\s+CellController::queueActorAuthority\(const\s+ESM::Cell&\s+cell,\s*const\s+PacketGuid&\s+guid\).*queuedAuthorityGuids\[cellDescription\(cell\)\]\s*=\s*guid;.*bool\s+CellController::applyQueuedActorAuthority\(const\s+ESM::Cell&\s+cell\).*Applying queued ID_ACTOR_AUTHORITY.*applyActorAuthority\(cell,\s*guid\);.*if\s*\(cellController->isActiveWorldCell\(actorList\.cell\)\).*cellController->initializeCell\(actorList\.cell\);.*cellController->applyActorAuthority\(actorList\.cell,\s*guid\);.*cellController->queueActorAuthority\(actorList\.cell,\s*guid\);.*Queued it because that cell isn''t loaded yet' `
    -Missing $missing

Test-Pattern -Name "Actor-list requests spawn leveled creatures and answer with the current cell actor list" -Text $actorListProcessor `
    -Pattern 'if\s*\(actorList\.action\s*==\s*mwmp::BaseActorList::REQUEST\)\s*\{.*MechanicsHelper::spawnLeveledCreatures\(ptrCellStore\);.*actorList\.sendActorsInCell\(ptrCellStore\);' `
    -Missing $missing

Test-Pattern -Name "Actor-list responses include NPC and creature refs from the requested cell" -Text $actorList `
    -Pattern 'void\s+ActorList::sendActorsInCell\(MWWorld::CellStore\*\s+cellStore\)\s*\{.*reset\(\);.*cell\s*=\s*makeActorPacketCell\(\*cellStore->getCell\(\)\);.*action\s*=\s*BaseActorList::SET;.*forEachType<ESM::NPC>\(addCellActor\);.*forEachType<ESM::Creature>\(addCellActor\);.*getActorPacket\(ID_ACTOR_LIST\)->setActorList\(this\);.*getActorPacket\(ID_ACTOR_LIST\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Incoming actor AI packets are routed into CellController AI application" -Text $actorAiProcessor `
    -Pattern 'virtual\s+void\s+Do\(ActorPacket\s+&packet,\s+ActorList\s+&actorList\)\s*\{.*Main::get\(\)\.getCellController\(\)->readAi\(actorList\);' `
    -Missing $missing

Test-Pattern -Name "CellController initializes cells before applying incoming actor AI state" -Text $cellController `
    -Pattern 'void\s+CellController::readAi\(ActorList&\s+actorList\)\s*\{.*initializeCell\(actorList\.cell\);.*if\s*\(cellsInitialized\.count\(mapIndex\)\s*>\s*0\)\s*cellsInitialized\[mapIndex\]->readAi\(actorList\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actors apply travel, wander, activate, combat, escort, follow, and cancel AI packages" -Text $dedicatedActor `
    -Pattern 'void\s+DedicatedActor::setAi\(\)\s*\{.*setAiSetting\(MWMechanics::AiSetting::Fight,\s*0\);.*BaseActorList::CANCEL.*getAiSequence\(\)\.clear\(\);.*BaseActorList::TRAVEL.*MWMechanics::AiTravel.*BaseActorList::WANDER.*MWMechanics::AiWander.*BaseActorList::ACTIVATE.*MWMechanics::AiActivate.*BaseActorList::COMBAT.*MWMechanics::AiCombat.*BaseActorList::ESCORT.*MWMechanics::AiEscort.*BaseActorList::FOLLOW.*MWMechanics::AiFollow' `
    -Missing $missing

Test-Pattern -Name "Server cell visitors request actor lists before a full actor list is known" -Text $serverCell `
    -Pattern 'function\s+BaseCell:AddVisitor\(pid,\s*options\).*Players\[pid\]:AddCellLoaded\(self\.description\).*if\s+options\.skipActorListRequest\s*~=\s*true\s+and\s+not\s+self:HasFullActorList\(\)\s+and\s+not\s+self\.isRequestingActorList\s+then.*local\s+actorListRequestPid\s*=\s*pid.*if\s+self\.authority\s*~=\s*nil\s+and\s+tableHelper\.containsValue\(self\.visitors,\s*self\.authority\)\s+then.*actorListRequestPid\s*=\s*self\.authority.*self:RequestActorList\(actorListRequestPid\)' `
    -Missing $missing

Test-Pattern -Name "Server saves requested actor lists as full actor lists" -Text $serverCell `
    -Pattern 'function\s+BaseCell:SaveActorList\(actors,\s*pid\).*if\s+self\.isRequestingActorList\s*==\s*true\s+and\s+pid\s*~=\s*nil\s+and\s+self\.actorListRequestPid\s*~=\s*pid\s+then.*return.*if\s+self\.isRequestingActorList\s*==\s*true\s+then.*self\.isRequestingActorList\s*=\s*false.*self\.actorListRequestPid\s*=\s*nil.*self\.data\.loadState\.hasFullActorList\s*=\s*true' `
    -Missing $missing

Test-Pattern -Name "Server keeps cell authority sticky and resends ActorAuthority to new visitors" -Text ($config + "`n" + $logicHandler) `
    -Pattern 'config\.allowCellAuthorityTransferForLowerPing\s*=\s*false.*local\s+function\s+isCellAuthorityCandidate\(cell,\s*pid\).*Players\s*==\s*nil\s+or\s+Players\[pid\]\s*==\s*nil.*IsLoggedIn.*local\s+function\s+getLowestPingCellAuthorityPid\(cell,\s*excludedPid\).*visitorPid\s*~=\s*excludedPid.*local\s+function\s+assignCellAuthority\(cell,\s*reason,\s*preferredPid,\s*excludedPid\).*preferredPid\s*~=\s*excludedPid.*cell:SetAuthority\(newAuthorityPid\).*logicHandler\.LoadCellForPlayer\s*=\s*function\(pid,\s*cellDescription,\s*visitorOptions\).*local\s+cell\s*=\s*LoadedCells\[cellDescription\].*local\s+wasVisitor\s*=\s*tableHelper\.containsValue\(cell\.visitors,\s*pid\).*local\s+previousAuthorityPid\s*=\s*cell:GetAuthority\(\).*local\s+previousVisitorCount\s*=\s*tableHelper\.getCount\(cell\.visitors\).*local\s+previousAuthorityWasCurrentVisitor\s*=\s*isCellAuthorityCandidate\(cell,\s*previousAuthorityPid\).*cell:AddVisitor\(pid,\s*visitorOptions\).*if\s+not\s+previousAuthorityWasCurrentVisitor\s+then.*previousVisitorCount\s*==\s*0\s+or\s+previousAuthorityPid\s*==\s*nil.*preferredPid\s*=\s*pid.*previousAuthorityPid\s*==\s*pid\s+and\s+not\s+wasVisitor\s+and\s+previousVisitorCount\s*>\s*0.*excludedPid\s*=\s*pid.*assignCellAuthority\(cell,\s*"cell authority was missing or stale",\s*preferredPid,\s*excludedPid\).*elseif\s+config\.allowCellAuthorityTransferForLowerPing\s*==\s*true.*assignCellAuthority\(cell,\s*"new visitor had lower ping",\s*pid\).*elseif\s+not\s+wasVisitor\s+then.*cell:LoadActorAuthority\(authPid\)' `
    -Missing $missing

Test-Pattern -Name "Server reassigns pending cell snapshot requests after authority handoff" -Text $logicHandler `
    -Pattern 'local\s+function\s+requestMissingCellSnapshots\(cell\).*type\(cell\.visitors\)\s*~=\s*"table".*local\s+requestPid\s*=\s*cell\.authority.*not\s+isCellAuthorityCandidate\(cell,\s*requestPid\).*requestPid\s*=\s*getLowestPingCellAuthorityPid\(cell\).*local\s+function\s+logSnapshotRerequest\(snapshotType\).*not\s+cell:HasFullContainerData\(\)\s+and\s+cell\.isRequestingContainerData\s*~=\s*true.*cell:RequestContainers\(requestPid\).*config\.serverAuthoritativeActors\s*~=\s*true\s+and\s+not\s+cell:HasFullActorList\(\)\s+and\s+cell\.isRequestingActorList\s*~=\s*true.*cell:RequestActorList\(requestPid\).*logicHandler\.UnloadCellForPlayer\s*=\s*function\(pid,\s*cellDescription\).*local\s+wasAuthority\s*=\s*cell:GetAuthority\(\)\s*==\s*pid.*assignCellAuthority\(cell,\s*"current authority left",\s*nil,\s*pid\).*requestMissingCellSnapshots\(cell\)' `
    -Missing $missing

Test-Pattern -Name "Server cell authority setter accepts only logged-in current visitors" -Text $serverCell `
    -Pattern 'function\s+BaseCell:SetAuthority\(pid\).*pid\s*==\s*nil\s+or\s+Players\s*==\s*nil\s+or\s+Players\[pid\]\s*==\s*nil.*return\s+false.*type\(Players\[pid\]\.IsLoggedIn\)\s*==\s*"function"\s+and\s+not\s+Players\[pid\]:IsLoggedIn\(\).*not logged in.*return\s+false.*not\s+tableHelper\.containsValue\(self\.visitors,\s*pid\).*not a visitor.*return\s+false.*self\.authority\s*=\s*pid.*self:LoadActorAuthority\(pid\).*return\s+true' `
    -Missing $missing

Test-Pattern -Name "Server actor AI repairs stale authority before sending packets" -Text $logicHandler `
    -Pattern 'logicHandler\.SetAIForActor\s*=\s*function\(cell,\s*actorUniqueIndex,\s*action,\s*targetPid,\s*targetUniqueIndex,.*local\s+authorityPid\s*=\s*cell\.authority.*not\s+isCellAuthorityCandidate\(cell,\s*authorityPid\).*authorityPid\s*=\s*assignCellAuthority\(cell,\s*"actor AI authority was stale"\).*authorityPid\s*==\s*nil.*Skipped sending AI for actor.*tes3mp\.SetActorListPid\(authorityPid\)' `
    -Missing $missing

Test-Pattern -Name "Server SetCellAuthority reports rejected admin authority assignments" -Text ($logicHandler + "`n" + $commandHandler) `
    -Pattern 'logicHandler\.SetCellAuthority\s*=\s*function\(pid,\s*cellDescription\).*LoadedCells\[cellDescription\]\s*==\s*nil.*return\s+false.*return\s+LoadedCells\[cellDescription\]:SetAuthority\(pid\).*if\s+not\s+logicHandler\.SetCellAuthority\(targetPid,\s*cellDescription\)\s+then.*cannot be cell authority' `
    -Missing $missing

Test-Pattern -Name "Server script actor authority rejects players without the cell loaded" -Text $serverActorFunctions `
    -Pattern 'bool\s+playerHasLoadedCell\(Player\*\s+player,\s*Cell\*\s+serverCell\).*player->getCells\(\).*std::find\(loadedCells->begin\(\),\s*loadedCells->end\(\),\s*serverCell\).*bool\s+canAssignActorAuthority\(Cell\*\s+serverCell,\s*PacketGuid\s+guid\).*Players::getPlayer\(guid\).*void\s+ActorFunctions::SendActorAuthority\(\).*if\s*\(!canAssignActorAuthority\(serverCell,\s*writeActorList\.guid\)\).*Refused actor authority.*return;.*serverCell->setAuthority\(writeActorList\.guid\)' `
    -Missing $missing

Write-Host "TES3MP AI activation sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 17"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
