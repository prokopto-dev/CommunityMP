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

$clientMain = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$clientEngine = Get-SourceText "apps\openmw\engine.cpp"
$clientLocalPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$clientPlayerProcessor = Get-SourceText "apps\openmw\mwmp\processors\PlayerProcessor.cpp"
$clientCharGenProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerCharGen.hpp"
$clientSystemHandshake = Get-SourceText "apps\openmw\mwmp\processors\system\ProcessorSystemHandshake.hpp"
$serverNetworking = Get-SourceText "apps\openmw-mp\Networking.cpp"
$systemPacket = Get-SourceText "components\openmw-mp\Packets\System\SystemPacket.cpp"
$playerPacket = Get-SourceText "components\openmw-mp\Packets\Player\PlayerPacket.cpp"
$cellChangePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCellChange.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Server accepts pre-init before creating player and requesting system handshake" -Text $serverNetworking `
    -Pattern 'PacketPreInit::PluginContainer\s+tmp;.*packetPreInit\.Send\(packet->address\(\)\);.*Players::newPlayer\(packet->guid\(\)\);.*GetPacket\(ID_SYSTEM_HANDSHAKE\)->RequestData\(packet->guid\(\)\);.*return\s+true;' `
    -Missing $missing

Test-Pattern -Name "Server closes stale clients that send non-pre-init packets before player creation" -Text $serverNetworking `
    -Pattern 'bool\s+Networking::preInit\(ReceivedPacket\*\s+packet,\s+PacketStream\s+&bsIn\).*if\s*\(packet->id\(\)\s*!=\s*ID_GAME_PREINIT\)\s*\{.*sent\s+wrong\s+first\s+packet.*transport->closeConnection\(packet->destination\(\),\s*true\);.*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "Server validates system handshake before marking player handshaked" -Text $serverNetworking `
    -Pattern 'if\s*\(packet->id\(\)\s*==\s*ID_SYSTEM_HANDSHAKE\).*myPacket->Read\(\);.*!myPacket->isPacketValid\(\).*kickPlayer\(player->guid\);.*player->isHandshaked\(\).*kickPlayer\(player->guid\);.*validateServerPassword\(serverPassword,\s*baseSystem\.serverPassword\).*isBlank\(baseSystem\.playerName\).*kickPlayer\(player->guid\);.*player->setLoginName\(baseSystem\.playerName\.substr\(0,\s*35\)\);.*player->setLoginPasswordHash\(baseSystem\.accountPasswordHash\);.*player->setHandshake\(\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Server defers early ID_LOADED until system handshake completes" -Text $serverNetworking `
    -Pattern 'if\s*\(!player->isHandshaked\(\)\)\s*\{.*if\s*\(packet->id\(\)\s*==\s*ID_LOADED\)\s*\{.*Deferring ID_LOADED until system handshake completes.*player->setPendingLoaded\(true\);.*return;.*player->incrementHandshakeAttempts\(\);.*getHandshakeAttempts\(\)\s*>\s*20.*kickPlayer\(player->guid,\s*false\).*getHandshakeAttempts\(\)\s*>\s*5.*kickPlayer\(player->guid,\s*true\);.*return;\s*\}' `
    -Missing $missing

Test-Pattern -Name "Server processes ID_LOADED as the OnPlayerConnect boundary before POSTLOADED/newPlayer" -Text $serverNetworking `
    -Pattern 'void\s+Networking::processLoadedPlayer\(Player\*\s+player\).*player->setLoadState\(Player::LOADED\);.*Script::Call<Script::CallbackIdentity\("OnPlayerConnect"\)>\(pid\);.*ID_USER_DISCONNECTED.*Players::deletePlayer\(player->guid\);.*player->setLoadState\(Player::POSTLOADED\);.*newPlayer\(player->guid\);.*if\s*\(packet->id\(\)\s*==\s*ID_LOADED\)\s*\{.*processLoadedPlayer\(player\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Server replays deferred ID_LOADED immediately after accepting handshake" -Text $serverNetworking `
    -Pattern 'player->setLoginName\(baseSystem\.playerName\.substr\(0,\s*35\)\);.*player->setLoginPasswordHash\(baseSystem\.accountPasswordHash\);.*player->setHandshake\(\);.*if\s*\(player->hasPendingLoaded\(\)\)\s*\{.*Processing deferred ID_LOADED.*player->setPendingLoaded\(false\);.*processLoadedPlayer\(player\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Client disables controls until TES3MP has loaded the selected character" -Text $clientEngine `
    -Pattern 'bool\s+disableControls\s*=\s*false;.*#ifdef\s+BUILD_TES3MP_CLIENT.*disableControls\s*=\s*mwmp::Main::isInitialized\(\)\s*&&\s*!mwmp::Main::get\(\)\.getLocalPlayer\(\)->isLoggedIn\(\);.*#endif.*mInputManager->update\(frametime,\s*disableControls\);' `
    -Missing $missing

Test-Pattern -Name "Client replies to server system-handshake request with local system credentials" -Text $clientSystemHandshake `
    -Pattern 'virtual\s+void\s+Do\(SystemPacket\s+&packet,\s+BaseSystem\s+\*system\)\s*\{.*packet\.setSystem\(Main::get\(\)\.getLocalSystem\(\)\);.*packet\.Send\(serverAddr\);' `
    -Missing $missing

Test-Pattern -Name "Client ignores local gameplay snapshot requests until selected-character login completes" -Text $clientPlayerProcessor `
    -Pattern 'if\s*\(request\s*&&\s*guid\s*==\s*myGuid\)\s*\{.*LocalPlayer\*\s+localPlayer\s*=\s*Main::get\(\)\.getLocalPlayer\(\);.*localPlayer\s*!=\s*nullptr\s*&&\s*!localPlayer->isLoggedIn\(\).*Ignoring %s request for LocalPlayer until character login completes.*return\s+true;' `
    -Missing $missing

Test-Pattern -Name "Client sends Loaded as the login bootstrap before selected-character gameplay state" -Text $clientMain `
    -Pattern 'if\s*\(!mLocalPlayer->processCharGen\(\)\)\s*\{.*setAIActive\(false\);.*return;.*const\s+bool\s+loggedIn\s*=\s*mLocalPlayer->isLoggedIn\(\);.*setAIActive\(loggedIn\);.*if\s*\(!mInitialLoadedSent\)\s*\{.*mInitialLoadedSent\s*=\s*true;.*getPlayerPacket\(ID_LOADED\)->setPlayer\(getLocalPlayer\(\)\);.*getPlayerPacket\(ID_LOADED\)->Send\(\);.*return;.*if\s*\(!loggedIn\)\s*return;.*if\s*\(!mInitialPlayerPacketsSent\)\s*\{.*mInitialPlayerPacketsSent\s*=\s*true;.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(getLocalPlayer\(\)\);.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->Send\(\);.*mLocalPlayer->updateStatsDynamic\(true\);.*setChatVisible\(true\);' `
    -Missing $missing

Test-Pattern -Name "Client maps server chargen stages to OpenMW chargen menus including the name prompt" -Text $clientLocalPlayer `
    -Pattern 'MWGui::GuiMode\s+charGenModeForStage\(int\s+stage\).*case\s+0:\s*return\s+MWGui::GM_Name;.*case\s+1:\s*return\s+MWGui::GM_Race;.*case\s+2:\s*return\s+MWGui::GM_Class;.*case\s+3:\s*return\s+MWGui::GM_Birth;.*default:\s*return\s+MWGui::GM_Review;.*int\s+charGenStageForMode\(MWGui::GuiMode\s+mode\).*case\s+MWGui::GM_Name:\s*return\s+0;.*case\s+MWGui::GM_Race:\s*return\s+1;.*case\s+MWGui::GM_Class:.*case\s+MWGui::GM_ClassPick:.*case\s+MWGui::GM_ClassCreate:.*case\s+MWGui::GM_ClassGenerate:.*return\s+2;.*case\s+MWGui::GM_Birth:\s*return\s+3;.*case\s+MWGui::GM_Review:\s*return\s+4;.*int\s+activeCharGenStage\(const\s+MWBase::WindowManager&\s+windowManager\).*const\s+int\s+topStage\s*=\s*charGenStageForMode\(windowManager\.getMode\(\)\);.*windowManager\.containsMode\(MWGui::GM_Name\).*return\s+0;.*windowManager\.containsMode\(MWGui::GM_Race\).*return\s+1;.*windowManager\.containsMode\(MWGui::GM_Class\).*windowManager\.containsMode\(MWGui::GM_ClassPick\).*windowManager\.containsMode\(MWGui::GM_ClassCreate\).*windowManager\.containsMode\(MWGui::GM_ClassGenerate\).*return\s+2;.*windowManager\.containsMode\(MWGui::GM_Birth\).*return\s+3;.*windowManager\.containsMode\(MWGui::GM_Review\).*return\s+4;' `
    -Missing $missing

Test-Pattern -Name "Client decodes unmatched server chargen packets into the local player" -Text $clientCharGenProcessor `
    -Pattern 'if\s*\(!isLocal\(\)\s*&&\s*player\s*==\s*nullptr\s*&&\s*!isRequest\(\)\).*ID_PLAYER_CHARGEN.*getLocalPlayer\(\);.*packet\.setPlayer\(player\);.*packet\.Read\(\);' `
    -Missing $missing

Test-Pattern -Name "Client syncs active chargen menus while preserving the pending dialog until it closes" -Text $clientLocalPlayer `
    -Pattern 'const\s+auto\s+sendCharGenState\s*=\s*\[&\]\s*\(\)\s*\{.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->Send\(\);.*const\s+auto\s+closePendingCharGenStage\s*=\s*\[&\]\s*\(\)\s*\{.*if\s*\(mPendingCharGenStage\s*==\s*-1\).*return;.*if\s*\(mPendingCharGenStage\s*>=\s*charGenState\.endStage\).*charGenState\.currentStage\s*=\s*charGenState\.endStage;.*else\s+if\s*\(charGenState\.currentStage\s*==\s*mPendingCharGenStage\).*std::min\(mPendingCharGenStage\s*\+\s*1,\s*charGenState\.endStage\).*mPendingCharGenStage\s*=\s*-1;.*if\s*\(!charGenState\.isFinished\)\s*\{.*const\s+int\s+activeStage\s*=\s*activeCharGenStage\(\*windowManager\);.*if\s*\(activeStage\s*!=\s*-1\).*if\s*\(mPendingCharGenStage\s*!=\s*-1\s*&&\s*activeStage\s*!=\s*mPendingCharGenStage\).*closePendingCharGenStage\(\);.*if\s*\(activeStage\s*!=\s*mPendingCharGenStage\).*if\s*\(activeStage\s*>=\s*charGenState\.endStage\).*charGenState\.currentStage\s*=\s*charGenState\.endStage;.*else\s+if\s*\(charGenState\.currentStage\s*!=\s*activeStage\).*charGenState\.currentStage\s*=\s*activeStage;.*sendCharGenState\(\);.*mPendingCharGenStage\s*=\s*activeStage;.*return\s+false;.*if\s*\(!charGenState\.isFinished\)\s*closePendingCharGenStage\(\);.*if\s*\(charGenState\.currentStage\s*<\s*charGenState\.endStage\)\s*\{.*windowManager->pushGuiMode\(charGenModeForStage\(charGenState\.currentStage\)\);.*sendCharGenState\(\);.*mPendingCharGenStage\s*=\s*currentStage;.*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "Client keeps OpenMW chargen global active during TES3MP chargen and releases it afterward" -Text $clientLocalPlayer `
    -Pattern 'void\s+setOpenMwCharGenFinished\(bool\s+finished\).*const\s+int\s+state\s*=\s*finished\s*\?\s*-1\s*:\s*1;.*world->setGlobalInt\(MWWorld::Globals::sCharGenState,\s*state\);.*if\s*\(!charGenState\.isFinished\)\s*\{.*setOpenMwCharGenFinished\(false\);.*charGenState\.isFinished\s*=\s*true;.*setOpenMwCharGenFinished\(true\);.*void\s+LocalPlayer::setCharacter\(\).*charGenState\.isFinished\s*=\s*true;.*setOpenMwCharGenFinished\(true\);' `
    -Missing $missing

Test-Pattern -Name "Client sends final chargen base-info before stats and EndCharGen packet" -Text $clientLocalPlayer `
    -Pattern 'if\s*\(!charGenState\.isFinished\)\s*\{.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->Send\(\);.*if\s*\(charGenState\.endStage\s*!=\s*1\)\s*\{.*updateStatsDynamic\(true\);.*updateAttributes\(true\);.*updateSkills\(true\);.*updateLevel\(true\);.*sendClass\(\);.*sendSpellbook\(\);.*charGenState\.currentStage\s*=\s*charGenState\.endStage;.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->Send\(\);.*charGenState\.isFinished\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "Client login readiness requires a received cell plus valid race and character or completed chargen" -Text $clientLocalPlayer `
    -Pattern 'bool\s+LocalPlayer::hasLoadedCharacter\(\)\s+const\s*\{.*if\s*\(!receivedCell\s*\|\|\s*!hasValidLocalPlayerRace\(\)\)\s*return\s+false;.*if\s*\(receivedCharacter\)\s*return\s+true;.*return\s+charGenState\.isFinished\s*&&\s*charGenState\.endStage\s*>\s*1;.*bool\s+LocalPlayer::isLoggedIn\(\)\s+const\s*\{.*return\s+hasLoadedCharacter\(\);' `
    -Missing $missing

Test-Pattern -Name "Client sets receivedCharacter only after a valid server character update" -Text $clientLocalPlayer `
    -Pattern 'void\s+LocalPlayer::setCharacter\(\)\s*\{.*receivedCharacter\s*=\s*false;.*if\s*\(world->getStore\(\)\.get<ESM::Race>\(\)\.search\(npc\.mRace\)\s*!=\s*0\)\s*\{.*receivedCharacter\s*=\s*true;.*charGenState\.isFinished\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "Client applies server cell before marking receivedCell" -Text $clientLocalPlayer `
    -Pattern 'void\s+LocalPlayer::setCell\(\)\s*\{.*bool\s+cellApplied\s*=\s*true;.*world->changeTo.*updateCell\(true,\s*false\);.*if\s*\(cellApplied\)\s*updatePosition\(true,\s*false,\s*false\);.*receivedCell\s*=\s*cellApplied;' `
    -Missing $missing

Test-Pattern -Name "System packets stay on the system ordering channel" -Text $systemPacket `
    -Pattern 'SystemPacket::SystemPacket\(\)\s*:\s*BasePacket\(\)\s*\{.*priority\s*=\s*PacketPriority::High;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_SYSTEM;' `
    -Missing $missing

Test-Pattern -Name "Player packets stay on the player ordering channel" -Text $playerPacket `
    -Pattern 'PlayerPacket::PlayerPacket\(\)\s*:\s*BasePacket\(\)\s*\{.*priority\s*=\s*PacketPriority::High;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_PLAYER;' `
    -Missing $missing

Test-Pattern -Name "Cell change packets are immediate priority and preserve movement-lane ordering" -Text $cellChangePacket `
    -Pattern 'PacketPlayerCellChange::PacketPlayerCellChange\(\)\s*:\s*PlayerPacket\(\)\s*\{.*packetID\s*=\s*ID_PLAYER_CELL_CHANGE;.*priority\s*=\s*PacketPriority::Immediate;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;' `
    -Missing $missing

Write-Host "TES3MP login/world-entry ordering check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 20"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
