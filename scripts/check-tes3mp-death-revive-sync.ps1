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

$character = Get-SourceText "apps\openmw\mwmechanics\character.cpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$localActor = Get-SourceText "apps\openmw\mwmp\LocalActor.cpp"
$clientCell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$actorList = Get-SourceText "apps\openmw\mwmp\ActorList.cpp"
$playerDeathProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerDeath.hpp"
$playerResurrectProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerResurrect.hpp"
$packetPlayerDeath = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerDeath.cpp"
$packetPlayerStatsDynamic = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerStatsDynamic.cpp"
$packetActorDeath = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorDeath.cpp"
$packetActorStatsDynamic = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorStatsDynamic.cpp"
$packetPlayerResurrect = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerResurrect.cpp"
$basePlayerCpp = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$serverSimulation = Get-SourceText "apps\openmw-mp\ServerSimulation.cpp"
$serverCell = Get-SourceText "apps\openmw-mp\Cell.cpp"
$dedicatedActor = Get-SourceText "apps\openmw\mwmp\DedicatedActor.cpp"
$serverPlayerDeathProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerDeath.hpp"
$serverPlayerResurrectProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerResurrect.hpp"
$serverActorStatsDynamicProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorStatsDynamic.hpp"
$scriptFunctions = Get-SourceText "apps\openmw-mp\Script\ScriptFunctions.hpp"
$mechanicsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Mechanics.cpp"
$basePacketTest = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$serverLuaCompatTest = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$cellBase = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Player and authority-owned actor deaths are emitted from the death animation path" -Text $character `
    -Pattern 'if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{.*if\s*\(mPtr\s*==\s*getPlayer\(\)\)\s*mwmp::Main::get\(\)\.getLocalPlayer\(\)->sendDeath\(static_cast<char>\(mDeathState\)\);.*else\s+if\s*\(!mPtr\.getClass\(\)\.getCreatureStats\(mPtr\)\.isDeathAnimationFinished\(\).*&&\s*mwmp::Main::get\(\)\.getCellController\(\)->isLocalActor\(mPtr\)\).*if\s*\(mwmp::LocalActor\*\s+localActor\s*=\s*mwmp::Main::get\(\)\.getCellController\(\)->getLocalActor\(mPtr\)\).*localActor->sendDeath\(static_cast<char>\(mDeathState\)\);' `
    -Missing $missing

Test-Pattern -Name "Local player death packets keep killer target metadata and clear transient target state" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendDeath\(char\s+newDeathState\)\s*\{.*if\s*\(MechanicsHelper::isEmptyTarget\(killer\)\)\s*killer\s*=\s*MechanicsHelper::getTarget\(getPlayerPtr\(\)\);.*updatePosition\(true\);.*deathState\s*=\s*newDeathState;.*getPlayerPacket\(ID_PLAYER_DEATH\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_DEATH\)->Send\(\);.*MechanicsHelper::clearTarget\(killer\);' `
    -Missing $missing

Test-Pattern -Name "PlayerDeath packet serializes movement snapshot plus death state and killer identity" -Text ($packetPlayerDeath + "`n" + $basePacketTest) `
    -Pattern 'packetID\s*=\s*ID_PLAYER_DEATH;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*player->positionSequence.*player->position.*player->direction.*player->deathState.*player->killer\.isPlayer.*if\s*\(player->killer\.isPlayer\)\s*\{.*player->killer\.guid.*\}\s*else\s*\{.*player->killer\.refId.*player->killer\.refNum.*player->killer\.mpNum.*player->killer\.name.*playerDeathRoundTripsCombatTransformAndKillerState.*playerCombatEventPacketsRejectTruncatedPayloads.*playerDeathUsesMovementDelivery' `
    -Missing $missing

Test-Pattern -Name "Player dynamic stats persist death state for late observers" -Text ($packetPlayerStatsDynamic + "`n" + $serverPlayerDeathProcessor + "`n" + $basePacketTest) `
    -Pattern 'packetID\s*=\s*ID_PLAYER_STATS_DYNAMIC;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*RW\(player->creatureStats\.mDead,\s*send\);.*BPP_INIT\(ID_PLAYER_DEATH\).*player\.creatureStats\.mDead\s*=\s*true;.*player\.creatureStats\.mDynamic\[0\]\.mCurrent\s*=\s*0;.*statsDynamicIndexChanges\.push_back\(0\);.*playerStatsDynamicRoundTripsDeathState.*playerCellAndStatsPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Server accepts PlayerDeath only after accepted dead stats" -Text ($basePlayerCpp + "`n" + $serverPlayerDeathProcessor + "`n" + $basePacketTest) `
    -Pattern 'bool\s+hasFiniteAcceptedStatsDynamic\(\)\s+const.*hasAcceptedStatsDynamicPacket.*std::isfinite\(acceptedStatsDynamic\[i\]\.mBase\).*std::isfinite\(acceptedStatsDynamic\[i\]\.mCurrent\).*std::isfinite\(acceptedStatsDynamic\[i\]\.mMod\).*bool\s+hasServerAcceptedDeadStatsDynamic\(\)\s+const.*hasFiniteAcceptedStatsDynamic\(\).*acceptedStatsDynamicDead\s*\|\|\s*acceptedStatsDynamic\[0\]\.mCurrent\s*<=\s*healthDeadEpsilon.*bool\s+isClientDeathPacketAllowed\(\)\s+const.*deathState\s*!=\s*0\s*&&\s*hasServerAcceptedDeadStatsDynamic\(\).*class\s+ProcessorPlayerDeath.*if\s*\(!player\.isClientDeathPacketAllowed\(\)\)\s*\{.*return;.*\}.*acceptSequencedPlayerCombatEvent\(player\).*player\.creatureStats\.mDead\s*=\s*true.*player\.acceptCurrentStatsDynamicPacket\(\).*clientPlayerDeathAuthorityRequiresAcceptedDeadStats' `
    -Missing $missing

Test-Pattern -Name "Server-owned attack deaths run death lifecycle without synthesizing PlayerDeath echo packets" -Text ($serverSimulation + "`n" + $eventHandler + "`n" + $playerBase) `
    -Pattern '^(?!.*GetPacket\(\s*ID_PLAYER_DEATH\s*\))(?=.*applyHealthDamageToPlayer\(Player&\s+target,\s*float\s+damage,\s*bool&\s+becameDead\).*target\.creatureStats\.mDead\s*=\s*health\s*<=\s*healthDeadEpsilon;.*becameDead\s*=\s*target\.creatureStats\.mDead;.*target\.acceptCurrentStatsDynamicPacket\(\);)(?=.*broadcastPlayerStats\(Player&\s+target\).*ID_PLAYER_STATS_DYNAMIC.*statsPacket->Send\(target\.guid\);.*target\.sendToLoaded\(statsPacket\);)(?=.*notifyPlayerStatsDynamic\(Player&\s+target\).*Script::Call<Script::CallbackIdentity\("OnPlayerStatsDynamic"\)>\(target\.getId\(\)\);)(?=.*broadcastPlayerStats\(\*target\);.*notifyPlayerStatsDynamic\(\*target\);.*if\s*\(becameDead\)\s*notifyPlayerDeath\(\*target\);)(?=.*notifyPlayerDeath\(Player&\s+target\).*Script::Call<Script::CallbackIdentity\("OnPlayerDeath"\)>\(target\.getId\(\)\);)(?=.*eventHandler\.OnPlayerStatsDynamic\s*=\s*function\(pid\).*eventHandler\.OnGenericPlayerEvent\(pid,\s*"PlayerStatsDynamic"\))(?=.*function\s+BasePlayer:SaveDataByPacketType\(packetType,\s*playerPacket\).*packetType\s*==\s*"PlayerStatsDynamic".*self:SaveStatsDynamic\(playerPacket\))(?=.*eventHandler\.OnPlayerDeath\s*=\s*function\(pid\).*Players\[pid\]:ProcessDeath\(\))(?=.*function\s+BasePlayer:ProcessDeath\(\).*self\.data\.death\.isDead\s*=\s*true.*self\.data\.stats\.healthCurrent\s*=\s*0.*self:SaveToDrive\(\))' `
    -Missing $missing

Test-Pattern -Name "Incoming PlayerDeath packets kill local players and route dedicated-player movement through the guarded receiver" -Text $playerDeathProcessor `
    -Pattern 'BPP_INIT\(ID_PLAYER_DEATH\).*if\s*\(isLocal\(\)\)\s*\{.*static_cast<LocalPlayer\*>\(player\)->die\(\);.*\}\s*else\s+if\s*\(player\s*!=\s*0\)\s*\{.*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*dedicatedPlayer\.die\(\);' `
    -Missing $missing

Test-Pattern -Name "Local death application sets zero health and echoes PlayerDeath to the server" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::die\(\)\s*\{.*creatureStats\.mDead\s*=\s*true;.*health\.setCurrent\(0\);.*setHealth\(health\);.*updatePosition\(true\);.*getPlayerPacket\(ID_PLAYER_DEATH\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_DEATH\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "ActorDeath packets apply optional movement snapshots before death state" -Text ($packetActorDeath + "`n" + $localActor + "`n" + $clientCell + "`n" + $basePacketTest) `
    -Pattern 'PacketActorDeath::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*actor\.refId.*actor\.hasPositionData.*if\s*\(actor\.hasPositionData\).*actor\.positionSequence.*actor\.position.*actor\.direction.*actor\.deathState.*void\s+LocalActor::sendDeath\(char\s+newDeathState\).*updatePosition\(true\);.*void\s+Cell::readDeath\(ActorList&\s+actorList\).*applySequencedPosition\(\*actor,\s*baseActor\);.*actor->creatureStats\.mDead\s*=\s*true;.*setQueuedDeathState\(actor->getPtr\(\),\s*baseActor\.deathState\);.*actorDeathRoundTripsCombatTransformAndKillerState.*actorCombatEventPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Death and revive mutations pin sequenced dynamic stats before stale packets can replay" -Text ($localPlayer + "`n" + $localActor + "`n" + $serverPlayerDeathProcessor + "`n" + $mechanicsFunctions + "`n" + $playerDeathProcessor + "`n" + $playerResurrectProcessor) `
    -Pattern '(?=.*void\s+LocalPlayer::sendDeath\(char\s+newDeathState\).*deathState\s*=\s*newDeathState;.*updateStatsDynamic\(true\);.*getPlayerPacket\(ID_PLAYER_DEATH\)->setPlayer\(this\);)(?=.*void\s+LocalActor::sendDeath\(char\s+newDeathState\).*deathState\s*=\s*newDeathState;.*sendStatsDynamic\(\);.*getActorPacket\(ID_ACTOR_DEATH\)->setActorList\(&actorList\);)(?=.*void\s+LocalPlayer::die\(\).*health\.writeState\(creatureStats\.mDynamic\[0\]\);.*acceptCurrentStatsDynamicPacket\(\);)(?=.*class\s+ProcessorPlayerDeath.*player\.creatureStats\.mDead\s*=\s*true;.*player\.acceptCurrentStatsDynamicPacket\(\);)(?=.*void\s+MechanicsFunctions::Resurrect\(unsigned\s+short\s+pid,\s*unsigned\s+int\s+type\).*player->creatureStats\.mDead\s*=\s*false;.*\+\+player->statsDynamicSequence;.*player->acceptCurrentStatsDynamicPacket\(\);)(?=.*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*dedicatedPlayer\.die\(\);.*player->acceptCurrentStatsDynamicPacket\(\);)(?=.*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*if\s*\(!dedicatedPlayer\.hasReference\(\)\)\s*return;.*dedicatedPlayer\.resurrect\(\);.*player->acceptCurrentStatsDynamicPacket\(\);)' `
    -Missing $missing

Test-Pattern -Name "Actor dynamic stats persist death flags for late observers" -Text ($packetActorStatsDynamic + "`n" + $serverCell + "`n" + $clientCell + "`n" + $dedicatedActor + "`n" + $basePacketTest) `
    -Pattern 'PacketActorStatsDynamic::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.creatureStats\.mDead,\s*send\);.*RW\(actor\.creatureStats\.mDeathAnimationFinished,\s*send\);.*RW\(actor\.creatureStats\.mDynamic,\s*send\);.*actor\.hasStatsDynamicData\s*=\s*true;.*case\s+ID_ACTOR_STATS_DYNAMIC:.*cellActor->hasStatsDynamicData\s*=\s*true;.*cellActor->creatureStats\.mDead\s*=\s*newActor\.creatureStats\.mDead;.*cellActor->creatureStats\.mDeathAnimationFinished\s*=\s*newActor\.creatureStats\.mDeathAnimationFinished;.*cellActor->creatureStats\.mDynamic\[0\]\s*=\s*newActor\.creatureStats\.mDynamic\[0\];.*void\s+Cell::readStatsDynamic\(ActorList&\s+actorList\).*actor->creatureStats\s*=\s*baseActor\.creatureStats;.*actor->setStatsDynamic\(\);.*void\s+DedicatedActor::setStatsDynamic\(\).*if\s*\(!creatureStats\.mDead\s*&&\s*creatureStats\.mDynamic\[0\]\.mCurrent\s*>\s*0\).*resurrect\(ptr\);.*else\s+if\s*\(creatureStats\.mDead\).*creatureStats\.mDynamic\[0\]\.mCurrent\s*=\s*0;.*ptrCreatureStats->setDeathAnimationFinished\(creatureStats\.mDeathAnimationFinished\);.*actorStatsDynamicRoundTripsDeathState.*actorCellAiStatsPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "ActorStatsDynamic packets bridge accepted NPC death and health state into Lua persistence" -Text ($serverActorStatsDynamicProcessor + "`n" + $scriptFunctions + "`n" + $serverCore + "`n" + $eventHandler + "`n" + $serverLuaCompatTest) `
    -Pattern 'class\s+ProcessorActorStatsDynamic.*serverCell->readActorList\(packetID,\s*&actorList\);.*Script::Call<Script::CallbackIdentity\("OnActorStatsDynamic"\)>\(player\.getId\(\),\s*actorList\.cell\.getDescription\(\)\.c_str\(\)\);.*serverCell->sendToLoaded\(&packet,\s*&actorList\);.*"OnActorStatsDynamic".*Callback<unsigned\s+short,\s*const\s+char\*>\(\).*function\s+OnActorStatsDynamic\(pid,\s*cellDescription\).*eventHandler\.OnActorStatsDynamic\(pid,\s*cellDescription\).*eventHandler\.OnActorStatsDynamic\s*=\s*function\(pid,\s*cellDescription\).*customEventHooks\.triggerValidators\("OnActorStatsDynamic",\s*\{pid,\s*cellDescription\}\).*LoadedCells\[cellDescription\]:SaveActorStatsDynamic\(\).*LoadedCells\[cellDescription\]:QuicksaveToDrive\(\).*customEventHooks\.triggerHandlers\("OnActorStatsDynamic",\s*eventStatus,\s*\{pid,\s*cellDescription\}\).*EventHandlerOnActorStatsDynamicPersistsLoadedCellStats' `
    -Missing $missing

Test-Pattern -Name "Actor death flush sends queued ActorDeath packets" -Text $actorList `
    -Pattern 'void\s+ActorList::sendDeathActors\(\)\s*\{.*if\s*\(deathActors\.size\(\)\s*>\s*0\)\s*\{.*baseActors\s*=\s*deathActors;.*getActorPacket\(ID_ACTOR_DEATH\)->setActorList\(this\);.*getActorPacket\(ID_ACTOR_DEATH\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Server death handler runs validators before ProcessDeath and always triggers handlers with status" -Text $eventHandler `
    -Pattern 'eventHandler\.OnPlayerDeath\s*=\s*function\(pid\).*local\s+eventStatus\s*=\s*customEventHooks\.triggerValidators\("OnPlayerDeath",\s*\{pid\}\).*if\s+eventStatus\.validDefaultHandler\s+then\s+Players\[pid\]:ProcessDeath\(\).*customEventHooks\.triggerHandlers\("OnPlayerDeath",\s*eventStatus,\s*\{pid\}\)' `
    -Missing $missing

Test-Pattern -Name "ProcessDeath is idempotent, persists death state, clears active effects, and starts an account-scoped revive timer" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:ProcessDeath\(\).*self:NormalizeDeathState\(\).*if\s+self\.data\.death\.isDead\s*==\s*true\s+then\s+return\s+end.*self\.data\.death\.isDead\s*=\s*true.*self\.data\.death\.timestamp\s*=\s*os\.time\(\).*self\.data\.stats\.healthCurrent\s*=\s*0.*self\.data\.spellsActive\s*=\s*\{\}.*tes3mp\.CreateTimerEx\("OnDeathTimeExpiration",\s*time\.seconds\(config\.deathTime\),\s*"is",\s*self\.pid,\s*self\.accountName\).*tes3mp\.StartTimer\(self\.resurrectTimerId\).*self:SaveToDrive\(\)' `
    -Missing $missing

Test-Pattern -Name "Dead players cannot overwrite mutable player, active-spell, or cell state before server resurrection" -Text $eventHandler `
    -Pattern 'eventHandler\.OnGenericPlayerEvent\s*=\s*function\(pid,\s*packetType\).*if\s+Players\[pid\]:IsDead\(\)\s+then.*Players\[pid\]:LoadStatsDynamic\(\).*return.*eventHandler\.OnPlayerSpellsActive\s*=\s*function\(pid\).*if\s+Players\[pid\]:IsDead\(\)\s+then.*Players\[pid\]:LoadStatsDynamic\(\).*return.*eventHandler\.OnPlayerCellChange\s*=\s*function\(pid\).*if\s+Players\[pid\]:IsDead\(\)\s+then.*Players\[pid\]:LoadStatsDynamic\(\).*Players\[pid\]:LoadCell\(\).*return' `
    -Missing $missing

Test-Pattern -Name "SaveStatsDynamic keeps server-authoritative zero health while pending resurrection" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:SaveStatsDynamic\(playerPacket\).*self:NormalizeDeathState\(\).*if\s+self\.data\.death\.isDead\s*==\s*true\s+then\s+self\.data\.stats\.healthCurrent\s*=\s*0\s+return\s+end.*self\.data\.stats\.healthCurrent\s*=\s*playerPacket\.stats\.healthCurrent' `
    -Missing $missing

Test-Pattern -Name "Pending-death relog completes resurrection after saved cell load" -Text $playerBase `
    -Pattern 'self\.loggedIn\s*=\s*true.*self:StopLoginTimer\(\).*self:LoadCell\(\).*if\s+self\.data\.death\.isDead\s*==\s*true\s+and\s+config\.playersRespawn\s+then.*logged in with a pending death; completing server resurrection.*self:Resurrect\(\)' `
    -Missing $missing

Test-Pattern -Name "Server death timer validates account name before resurrecting and forwarding handlers" -Text $eventHandler `
    -Pattern 'eventHandler\.OnDeathTimeExpiration\s*=\s*function\(pid,\s*accountName\).*Players\[pid\]\s*~=\s*nil\s+and\s+Players\[pid\]:IsLoggedIn\(\)\s+and\s+Players\[pid\]\.accountName\s*==\s*accountName.*triggerValidators\("OnDeathTimeExpiration",\s*\{pid\}\).*if\s+eventStatus\.validDefaultHandler\s+then\s+Players\[pid\]:Resurrect\(\).*triggerHandlers\("OnDeathTimeExpiration",\s*eventStatus,\s*\{pid\}\)' `
    -Missing $missing

Test-Pattern -Name "Actor death saves nested killer identity without legacy typo" -Text $cellBase `
    -Pattern 'function\s+BaseCell:SaveActorDeath\(actors\).*if\s+actor\.killer\.pid\s*~=\s*nil\s+then.*playerKey\s*=\s*actor\.killer\.playerKey.*elseif\s+actor\.killer\.name\s*~=\s*""\s+then.*refId\s*=\s*actor\.killer\.refId.*uniqueIndex\s*=\s*actor\.killer\.uniqueIndex' `
    -Missing $missing

Test-Pattern -Name "Resurrect restores player state, stops revive timer, persists configured respawn, and applies jail/bounty recovery" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:Resurrect\(\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\("respawn",\s*config\.defaultRespawn\.cellDescription\).*tes3mp\.SetCell\(self\.pid,\s*config\.defaultRespawn\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\).*tes3mp\.SetPos\(self\.pid,\s*config\.defaultRespawn\.position\[1\].*tes3mp\.SetRot\(self\.pid,\s*config\.defaultRespawn\.rotation\[1\].*tes3mp\.SendCell\(self\.pid\).*tes3mp\.SendPos\(self\.pid\).*contentFixer\.UnequipDeadlyItems\(self\.pid\).*tes3mp\.Resurrect\(self\.pid,\s*currentResurrectType\).*self\.data\.death\.isDead\s*=\s*false.*self\.data\.death\.timestamp\s*=\s*0.*tes3mp\.StopTimer\(self\.resurrectTimerId\).*self\.resurrectTimerId\s*=\s*nil.*self:StoreLocation\(\{\s*cellDescription\s*=\s*config\.defaultRespawn\.cellDescription,\s*position\s*=\s*config\.defaultRespawn\.position,\s*rotation\s*=\s*config\.defaultRespawn\.rotation\s*\}\).*packetReader\.GetPlayerPacketTables\(self\.pid,\s*"PlayerCellChange"\).*self:SaveToDrive\(\).*config\.deathPenaltyJailDays\s*>\s*0\s+or\s+config\.bountyDeathPenalty.*tes3mp\.Jail\(self\.pid,\s*jailTime,\s*true,\s*true,\s*"Recovering",\s*resurrectionText\).*config\.bountyResetOnDeath.*tes3mp\.SetBounty\(self\.pid,\s*0\).*tes3mp\.SendBounty\(self\.pid\).*self:SaveBounty\(\)' `
    -Missing $missing

Test-Pattern -Name "PlayerResurrect packet and receiver apply local/dedicated resurrection by packet type" -Text ($packetPlayerResurrect + "`n" + $playerResurrectProcessor + "`n" + $localPlayer) `
    -Pattern 'packetID\s*=\s*ID_PLAYER_RESURRECT;.*RW\(player->resurrectType,\s*send\);.*BPP_INIT\(ID_PLAYER_RESURRECT\).*if\s*\(isLocal\(\)\)\s*\{.*static_cast<LocalPlayer\*>\(player\)->resurrect\(\);.*\}\s*else\s+if\s*\(player\s*!=\s*0\)\s*\{.*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*if\s*\(!dedicatedPlayer\.hasReference\(\)\)\s*return;.*dedicatedPlayer\.resurrect\(\);.*void\s+LocalPlayer::resurrect\(\).*creatureStats\.mDead\s*=\s*false;.*teleportToClosestMarker.*getMechanicsManager\(\)->resurrect\(ptrPlayer\).*resumeGame\(\).*setDrawState\(MWMechanics::DrawState::Nothing\).*getPlayerPacket\(ID_PLAYER_RESURRECT\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_RESURRECT\)->Send\(\);.*updateStatsDynamic\(true\);.*if\s*\(markerRespawn\)\s*updateCell\(true\);' `
    -Missing $missing

Test-Pattern -Name "Server death and revive packets scope remote-world state to loaded clients" -Text ($serverPlayerDeathProcessor + "`n" + $serverPlayerResurrectProcessor + "`n" + $mechanicsFunctions) `
    -Pattern '^(?!.*class\s+ProcessorPlayerResurrect.*player\.creatureStats\.mDead\s*=\s*false)(?!.*class\s+ProcessorPlayerResurrect.*player\.sendToLoaded\(&packet\))(?=.*class\s+ProcessorPlayerDeath.*player\.creatureStats\.mDead\s*=\s*true;.*player\.sendToLoaded\(&packet\);)(?=.*class\s+ProcessorPlayerResurrect.*Received\s+%s\s+from\s+%s)(?=.*void\s+MechanicsFunctions::Resurrect\(unsigned\s+short\s+pid,\s*unsigned\s+int\s+type\).*player->creatureStats\.mDead\s*=\s*false;.*\+\+player->statsDynamicSequence;.*player->acceptCurrentStatsDynamicPacket\(\);.*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*Script::Call<Script::CallbackIdentity\("OnPlayerResurrect"\)>\(player->getId\(\)\);)' `
    -Missing $missing

Write-Host "TES3MP death/revive sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 23"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
