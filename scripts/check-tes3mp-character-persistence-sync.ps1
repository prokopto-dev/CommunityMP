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

    if ($Name -eq "Instanced spawn cell records use the finalized character-name cell before teleport") {
        $requiredPatterns = @(
            'function\s+BasePlayer:SendInstancedSpawnCellRecord\(location\).*local\s+originalCellDescription\s*=\s*config\.instancedSpawn\.cellDescription.*local\s+instancedCellPrefix\s*=\s*originalCellDescription\s*\.\.\s*" - Instance for ".*string\.sub\(location\.cellDescription,\s*1,\s*string\.len\(instancedCellPrefix\)\)',
            'function\s+BasePlayer:SendInstancedSpawnCellRecord\(location\).*tes3mp\.ClearRecords\(\).*tes3mp\.SetRecordType\(enumerations\.recordType\["CELL"\]\).*packetBuilder\.AddCellRecord\(location\.cellDescription,\s*\{baseId\s*=\s*originalCellDescription\}\).*tes3mp\.SendRecordDynamic\(self\.pid,\s*false,\s*false\)',
            'function\s+BasePlayer:SendLocation\(location,\s*options\).*options\s*=\s*options\s*or\s*\{\}.*self:SendInstancedSpawnCellRecord\(location\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\(options\.reason\s+or\s+"sendLocation",\s*location\.cellDescription,\s*options\)',
            'function\s+BasePlayer:SendLocation\(location,\s*options\).*tes3mp\.SetCell\(self\.pid,\s*location\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\).*tes3mp\.SetPos\(self\.pid,\s*location\.position\[1\],\s*location\.position\[2\],\s*location\.position\[3\]\).*tes3mp\.SetRot\(self\.pid,\s*location\.rotation\[1\],\s*location\.rotation\[2\]\).*tes3mp\.SendCell\(self\.pid\).*tes3mp\.SendPos\(self\.pid\)',
            'Players\[pid\]:SendInstancedSpawnCellRecord\(Players\[pid\]:GetInitialSpawn\(\)\)',
            'PlayerBaseEndCharGenSendsInstancedSpawnRecordForCharacterName',
            'SetCellChangeReason:8:.*enumerations\.cellChangeReason\.SERVER',
            'Instance for DisplayName',
            'Instance for ServerAccount'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "New characters receive server-owned release papers journal and dialogue topics") {
        $requiredPatterns = @(
            'startingOfficeReleaseJournal\s*=\s*\{.*quest\s*=\s*"a1_1_findspymaster".*index\s*=\s*1',
            'startingOfficeReleaseItems\s*=\s*\{.*bk_A1_1_DirectionsCaiusCosades.*bk_a1_1_caiuspackage.*Gold_001.*count\s*=\s*87',
            'startingOfficeReleaseTopics\s*=\s*\{.*"duties".*"Caius Cosades"',
            'function\s+BasePlayer:EnsureStartingOfficeReleaseState\(spawnUsed\).*self\.creatingNewCharacter\s*~=\s*true.*inventoryHelper\.addItem\(self\.data\.inventory,\s*item\.refId,\s*item\.count,\s*item\.charge,\s*item\.enchantmentCharge,\s*item\.soul\).*mergeJournalItem\(self\.data\.journal,\s*journalItem\).*recordJournalChanges\(self,\s*\{\s*journalItem\s*\}\).*local\s+acceptedTopics\s*=\s*\{\}.*tableHelper\.containsCaseInsensitiveString\(self\.data\.topics,\s*topicId,\s*false\).*recordTopicChanges\(self,\s*acceptedTopics\)',
            'function\s+BasePlayer:EnsureSharedStartingOfficeReleaseState\(\).*config\.shareJournal\s*==\s*true.*mergeJournalItem\(WorldInstance\.data\.journal,\s*journalItem\).*recordJournalChanges\(WorldInstance,\s*\{\s*journalItem\s*\}\).*config\.shareTopics\s*==\s*true.*tableHelper\.containsCaseInsensitiveString\(WorldInstance\.data\.topics,\s*topicId,\s*false\).*recordTopicChanges\(WorldInstance,\s*acceptedTopics\)',
            'function\s+BasePlayer:QueueStartingOfficeReleaseStateChanges\(releaseStateChanges,\s*sharedReleaseStateChanges\).*pendingStartingOfficeReleaseStateChanges\s*=\s*\{.*items\s*=\s*tableHelper\.deepCopy\(releaseStateChanges\.items\s*or\s*\{\}\).*journal\s*=.*topics\s*=.*sharedJournal\s*=.*sharedTopics\s*=',
            'function\s+BasePlayer:ApplyStartingOfficeReleaseStateChanges\(\).*local\s+releaseStateChanges\s*=\s*self\.pendingStartingOfficeReleaseStateChanges.*self\.pendingStartingOfficeReleaseStateChanges\s*=\s*nil.*WorldInstance:LoadJournal\(self\.pid\).*WorldInstance:LoadTopics\(self\.pid\).*self:LoadItemChanges\(releaseStateChanges\.items,\s*enumerations\.inventory\.ADD\).*self:LoadJournal\(\).*self:LoadTopics\(\)',
            'function\s+BasePlayer:EndCharGen\(\).*local\s+releaseStateChanges\s*=\s*self:EnsureStartingOfficeReleaseState\(spawnUsed\).*local\s+sharedReleaseStateChanges\s*=\s*self:EnsureSharedStartingOfficeReleaseState\(\).*local\s+hasQueuedReleaseStateChanges\s*=\s*self:QueueStartingOfficeReleaseStateChanges\(.*self:SendLocation\(spawnUsed,\s*\{\s*reason\s*=\s*"chargenSpawn"\s*\}\).*elseif\s+hasQueuedReleaseStateChanges\s+then\s+self:ApplyStartingOfficeReleaseStateChanges\(\)',
            'function\s+Player:QuicksaveToDrive\(\).*writeAccountSnapshot\(self,\s*\{\s*preserveCreatingNewCharacter\s*=\s*self\.creatingNewCharacter\s*==\s*true\s*\}\)',
            'eventHandler\.OnPlayerCellChange\s*=\s*function\(pid\).*pendingServerLocationChange\.reason\s*==\s*"chargenSpawn".*Players\[pid\]:ApplyStartingOfficeReleaseStateChanges\(\)',
            'eventHandler\.OnCellLoad\s*=\s*function\(pid,\s*cellDescription\).*logicHandler\.LoadCellForPlayer\(pid,\s*cellDescription\).*ApplyStartingOfficeReleaseStateChanges',
            'PlayerBaseRegisterAndEndCharGenKeepAccountAndCharacterNamesSeparate.*player\.GetInitialSpawn.*return\s+nil.*player:SaveActiveCharacterSlot\(true\).*player\.creatingNewCharacter\s*==\s*true.*LoadItemChanges:3:.*LoadJournal.*LoadTopics',
            'PlayerBaseEndCharGenSendsInstancedSpawnRecordForCharacterName.*pendingStartingOfficeReleaseStateChanges\s*~=\s*nil.*callsBeforeReleaseAck.*player:ApplyStartingOfficeReleaseStateChanges\(\).*pendingStartingOfficeReleaseStateChanges\s*==\s*nil',
            'CommunityMpPlayerAccountStoreSplitsAccountsAndCharacters.*player:QuicksaveToDrive\(\).*player\.creatingNewCharacter\s*==\s*true.*player:CreateAccount\(\).*player\.creatingNewCharacter\s*==\s*false',
            'EventHandlerChargenSpawnAckAppliesQueuedReleaseState.*reason\s*=\s*"chargenSpawn".*ApplyStartingOfficeReleaseStateChanges',
            'EventHandlerCellLoadAppliesQueuedReleaseState.*LoadCellForPlayer.*ApplyStartingOfficeReleaseStateChanges'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Account save store keeps character slots while exposing the selected character through legacy player data") {
        $requiredPatterns = @(
            'function\s+BasePlayer:StartNewCharacter\(\).*local\s+entries\s*=\s*self:EnsureCharacterSlots\(true\).*self\.activeCharacterIndex\s*=\s*#entries\s*\+\s*1.*self\.creatingNewCharacter\s*=\s*true',
            'function\s+BasePlayer:SaveActiveCharacterSlot\(preserveCreatingNewCharacter\).*local\s+wasCreatingNewCharacter\s*=\s*self\.creatingNewCharacter\s*==\s*true.*if\s+preserveCreatingNewCharacter\s*~=\s*true\s+then\s+self\.creatingNewCharacter\s*=\s*false\s+else\s+self\.creatingNewCharacter\s*=\s*wasCreatingNewCharacter\s+end.*entries\[targetIndex\]\s*=\s*self:CreateCharacterSnapshot\(\)',
            'function\s+Player:QuicksaveToDrive\(\).*writeAccountSnapshot\(self,\s*\{\s*preserveCreatingNewCharacter\s*=\s*self\.creatingNewCharacter\s*==\s*true\s*\}\)',
            'PlayerBaseCharacterSlotsPreserveAccountAndLegacyData.*legacy_quest.*new_quest.*legacy topic.*new topic.*customVariables\.questFlag',
            'PlayerBaseCharacterSlotsPreserveAccountAndLegacyData.*player:SaveActiveCharacterSlot\(true\).*player\.creatingNewCharacter\s*==\s*true.*player:SaveActiveCharacterSlot\(\).*player\.creatingNewCharacter\s*==\s*false',
            'CommunityMpPlayerAccountStoreSplitsAccountsAndCharacters.*player:QuicksaveToDrive\(\).*player\.creatingNewCharacter\s*==\s*true.*player:CreateAccount\(\).*player\.creatingNewCharacter\s*==\s*false'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

$clientMain = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$clientLocalPlayerHeader = Get-SourceText "apps\openmw\mwmp\LocalPlayer.hpp"
$clientLocalPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$clientDedicatedPlayer = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.cpp"
$clientWorldstate = Get-SourceText "apps\openmw\mwmp\Worldstate.cpp"
$windowManager = Get-SourceText "apps\openmw\mwgui\windowmanagerimp.cpp"
$engine = Get-SourceText "apps\openmw\engine.cpp"
$world = Get-SourceText "apps\openmw\mwworld\worldimp.cpp"
$stateManager = Get-SourceText "apps\openmw\mwstate\statemanagerimp.cpp"
$clientSystemHandshake = Get-SourceText "apps\openmw\mwmp\processors\system\ProcessorSystemHandshake.hpp"
$systemHandshakePacket = Get-SourceText "components\openmw-mp\Packets\System\PacketSystemHandshake.cpp"
$serverNetworking = Get-SourceText "apps\openmw-mp\Networking.cpp"
$baseInfoProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerBaseInfo.hpp"
$inventoryProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerInventory.hpp"
$cooldownsProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerCooldowns.hpp"
$factionProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerFaction.hpp"
$serverInventoryProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerInventory.hpp"
$equipmentProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerEquipment.hpp"
$serverEquipmentProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerEquipment.hpp"
$spellbookProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerSpellbook.hpp"
$playerSpellsActiveProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerSpellsActive.hpp"
$playerInventoryPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerInventory.cpp"
$playerCooldownsPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCooldowns.cpp"
$playerFactionPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerFaction.cpp"
$playerEquipmentPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerEquipment.cpp"
$playerJournalPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerJournal.cpp"
$playerBookPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerBook.cpp"
$playerSpellbookPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerSpellbook.cpp"
$playerSpellsActivePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerSpellsActive.cpp"
$clientScriptGlobalPacket = Get-SourceText "components\openmw-mp\Packets\Worldstate\PacketClientScriptGlobal.cpp"
$worldKillCountPacket = Get-SourceText "components\openmw-mp\Packets\Worldstate\PacketWorldKillCount.cpp"
$worldMapPacket = Get-SourceText "components\openmw-mp\Packets\Worldstate\PacketWorldMap.cpp"
$basePlayerHeader = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$baseStructs = Get-SourceText "components\openmw-mp\Base\BaseStructs.hpp"
$itemFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Items.cpp"
$questFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Quests.cpp"
$bookFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Books.cpp"
$basePacketTest = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$journalInterface = Get-SourceText "apps\openmw\mwbase\journal.hpp"
$journalImpl = Get-SourceText "apps\openmw\mwdialogue\journalimp.cpp"
$npcStats = Get-SourceText "apps\openmw\mwmechanics\npcstats.cpp"
$cmakeLists = Get-SourceText "CMakeLists.txt"
$installVerifier = Get-SourceText "scripts\verify-tes3mp-install.ps1"
$deployScript = Get-SourceText "scripts\deploy-tes3mp.ps1"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$serverConfig = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$stateHelper = Get-SourceText "files\tes3mp\server\scripts\stateHelper.lua"
$worldBase = Get-SourceText "files\tes3mp\server\scripts\world\base.lua"
$packetBuilder = Get-SourceText "files\tes3mp\server\scripts\packetBuilder.lua"
$defaultCommands = Get-SourceText "files\tes3mp\server\scripts\defaultCommands.lua"
$dataTableBuilder = Get-SourceText "files\tes3mp\server\scripts\dataTableBuilder.lua"
$cellBase = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$cellStateStore = Get-SourceText "files\tes3mp\server\scripts\communitymp\saves\cellStateStore.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$playerAccountStore = Get-SourceText "files\tes3mp\server\scripts\communitymp\saves\playerAccountStore.lua"
$saveCodec = Get-SourceText "files\tes3mp\server\lib\lua\communitympSaveCodec.lua"
$serverStateRegistry = Get-SourceText "files\tes3mp\server\lib\lua\communitympServerStateRegistry.lua"
$worldSaveRegistry = Get-SourceText "files\tes3mp\server\lib\lua\communitympWorldSaveRegistry.lua"
$recordStoreBase = Get-SourceText "files\tes3mp\server\scripts\recordstore\base.lua"
$recordStateStore = Get-SourceText "files\tes3mp\server\scripts\communitymp\saves\recordStateStore.lua"
$worldStateStore = Get-SourceText "files\tes3mp\server\scripts\communitymp\saves\worldStateStore.lua"
$serverLuaCompat = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

$legacyStorageFiles = @(
    "files\tes3mp\server\scripts\database.lua",
    "files\tes3mp\server\scripts\player\json.lua",
    "files\tes3mp\server\scripts\player\sql.lua",
    "files\tes3mp\server\scripts\cell\json.lua",
    "files\tes3mp\server\scripts\cell\sql.lua",
    "files\tes3mp\server\scripts\world\json.lua",
    "files\tes3mp\server\scripts\world\sql.lua",
    "files\tes3mp\server\scripts\recordstore\json.lua",
    "files\tes3mp\server\scripts\recordstore\sql.lua"
)

$remainingLegacyStorageFiles = @($legacyStorageFiles | Where-Object {
    Test-Path -LiteralPath (Join-Path $SourceRoot $_) -PathType Leaf
})

if ($remainingLegacyStorageFiles.Count -gt 0) {
    $missing.Add("Legacy JSON and SQL storage modules are deleted")
}

Test-Pattern -Name "Client requires account credentials before connecting and hashes the account password into the system handshake" -Text $clientMain `
    -Pattern 'Settings::Manager::getBool\("rememberAccount",\s*"General"\).*Settings::Manager::getString\("accountPasswordHash",\s*"General"\).*promptLoginCredentials\(.*serverEndpoint,\s*playerName,\s*initialServerPassword,\s*rememberCredentials,\s*hasRememberedAccountPasswordHash\).*playerName\s*=\s*trimAccountName\(credentials->accountName\);.*serverPassword\s*=\s*normalizeServerPassword\(credentials->serverPassword\);.*accountPasswordHash\s*=\s*credentials->useRememberedAccountPasswordHash\s*\?\s*rememberedAccountPasswordHash\s*:\s*getAccountPasswordHash\(credentials->accountPassword\);.*pendingAccountPassword\s*=\s*credentials->useRememberedAccountPasswordHash\s*\?\s*std::string\(\)\s*:\s*credentials->accountPassword;.*if\s*\(playerName\.empty\(\)\s*\|\|\s*accountPasswordHash\.empty\(\)\).*saveLoginCredentialSettings\(playerName,\s*credentials->rememberCredentials,\s*accountPasswordHash\);.*mLocalSystem->playerName\s*=\s*playerName;.*mLocalSystem->serverPassword\s*=\s*serverPassword;.*mLocalSystem->accountPasswordHash\s*=\s*accountPasswordHash;.*mNetworking->connect' `
    -Missing $missing

Test-Pattern -Name "System handshake packet carries account name, server password, and client account-password hash on the system lane" -Text $systemHandshakePacket `
    -Pattern 'packetID\s*=\s*ID_SYSTEM_HANDSHAKE;.*orderChannel\s*=\s*CHANNEL_SYSTEM;.*RW\(system->playerName,\s*send,\s*true,\s*maxNameLength\).*RW\(system->serverPassword,\s*send,\s*true,\s*maxPasswordLength\).*RW\(system->accountPasswordHash,\s*send,\s*true,\s*maxAccountPasswordHashLength\).*packetValid\s*=\s*false' `
    -Missing $missing

Test-Pattern -Name "Client answers server system-handshake requests using the prepared local system credentials" -Text $clientSystemHandshake `
    -Pattern 'BPP_INIT\(ID_SYSTEM_HANDSHAKE\).*packet\.setSystem\(Main::get\(\)\.getLocalSystem\(\)\);.*packet\.Send\(serverAddr\);' `
    -Missing $missing

Test-Pattern -Name "Server rejects blank account names and stores the account login name/hash before gameplay packets" -Text $serverNetworking `
    -Pattern 'if\s*\(packet->id\(\)\s*==\s*ID_SYSTEM_HANDSHAKE\).*myPacket->setSystem\(&baseSystem\);.*myPacket->Read\(\);.*validateServerPassword\(serverPassword,\s*baseSystem\.serverPassword\).*if\s*\(isBlank\(baseSystem\.playerName\)\).*kickPlayer\(player->guid\);.*player->setLoginName\(baseSystem\.playerName\.substr\(0,\s*35\)\);.*player->setLoginPasswordHash\(baseSystem\.accountPasswordHash\);.*player->setHandshake\(\);' `
    -Missing $missing

Test-Pattern -Name "Client sends Loaded once for login bootstrap and delays initial gameplay state until selected character is logged in" -Text $clientMain `
    -Pattern 'if\s*\(!mInitialLoadedSent\)\s*\{.*mInitialLoadedSent\s*=\s*true;.*getPlayerPacket\(ID_LOADED\)->setPlayer\(getLocalPlayer\(\)\);.*getPlayerPacket\(ID_LOADED\)->Send\(\);.*return;.*if\s*\(!loggedIn\)\s*return;.*if\s*\(!mInitialPlayerPacketsSent\)\s*\{.*mInitialPlayerPacketsSent\s*=\s*true;.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(getLocalPlayer\(\)\);.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->Send\(\);.*mLocalPlayer->updateStatsDynamic\(true\);.*setChatVisible\(true\);' `
    -Missing $missing

Test-Pattern -Name "Client waits for server-requested chargen instead of opening chargen during existing-character login" -Text $clientLocalPlayer `
    -Pattern 'LocalPlayer::LocalPlayer\(\).*Wait for the server to request character generation.*charGenState\.currentStage\s*=\s*1;.*charGenState\.endStage\s*=\s*1;.*charGenState\.isFinished\s*=\s*true;.*mCharGenBaseInfo\.blank\(\);.*mHasCharGenClass\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Multiplayer startup skips the engine default new-game block after TES3MP post-init" -Text $engine `
    -Pattern 'bool\s+skipDefaultGameStart\s*=\s*false;.*mwmp::Main::postInit\(\);.*skipDefaultGameStart\s*=\s*true;.*if\s*\(!skipDefaultGameStart\)\s*\{.*mStateManager->newGame\(!mNewGame\);' `
    -Missing $missing

Test-Pattern -Name "TES3MP bypassed world startup uses Census Office staging instead of vanilla startup scripts" -Text $world `
    -Pattern 'bypass.*&&\s*!mwmp::Main::isInitialized\(\).*&&\s*!mStartCell\.empty\(\).*else\s+if\s*\(bypass\s*&&\s*mwmp::Main::isInitialized\(\)\).*pos\.pos\[0\]\s*=\s*1130\.3388671875f;.*pos\.pos\[1\]\s*=\s*-387\.14947509766f;.*pos\.pos\[2\]\s*=\s*193\.f;.*pos\.rot\[0\]\s*=\s*0\.09375f;.*pos\.rot\[2\]\s*=\s*1\.5078122615814f;.*changeToInteriorCell\("Seyda Neen,\s*Census and Excise Office",\s*pos,\s*true\);.*else\s*\{.*getScriptManager\(\)->getGlobalScripts\(\)\.run\(\);' `
    -Missing $missing

Test-Pattern -Name "TES3MP bypassed new game does not register vanilla startup scripts" -Text $stateManager `
    -Pattern 'void\s+MWState::StateManager::newGame\(bool\s+bypass\).*if\s*\(!bypass\s*\|\|\s*!isMultiplayerSession\(\)\)\s*MWBase::Environment::get\(\)\.getScriptManager\(\)->getGlobalScripts\(\)\.addStartup\(\);.*getWorld\(\)->startNewGame\(bypass\);' `
    -Missing $missing

Test-Pattern -Name "Final chargen sends complete base info before stats, class, spellbook, and EndCharGen" -Text $clientLocalPlayer `
    -Pattern 'if\s*\(!charGenState\.isFinished\)\s*\{.*if\s*\(!mCharGenBaseInfo\.mRace\.empty\(\)\).*npc\.mRace\s*=\s*mCharGenBaseInfo\.mRace;.*npc\.mHead\s*=\s*mCharGenBaseInfo\.mHead;.*npc\.mHair\s*=\s*mCharGenBaseInfo\.mHair;.*npc\.setIsMale\(mCharGenBaseInfo\.isMale\(\)\);.*birthsign\s*=\s*refIdToString\(world->getPlayer\(\)\.getBirthSign\(\)\);.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_BASEINFO\)->Send\(\);.*updateStatsDynamic\(true\);.*updateAttributes\(true\);.*updateSkills\(true\);.*updateLevel\(true\);.*sendClass\(\);.*sendSpellbook\(\);.*charGenState\.currentStage\s*=\s*charGenState\.endStage;.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_CHARGEN\)->Send\(\);.*charGenState\.isFinished\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "Incoming local PlayerBaseInfo accepts valid saved characters and marks existing-character login ready" -Text ($baseInfoProcessor + "`n" + $clientLocalPlayer) `
    -Pattern 'BPP_INIT\(ID_PLAYER_BASEINFO\).*if\s*\(isLocal\(\)\).*if\s*\(isRequest\(\)\).*if\s*\(player\s*==\s*nullptr\)\s*return;.*packet\.setPlayer\(player\);.*packet\.Send\(serverAddr\);.*else\s*\{.*static_cast<LocalPlayer\*>\(player\)->setCharacter\(\);.*void\s+LocalPlayer::setCharacter\(\).*receivedCharacter\s*=\s*false;.*search\(npc\.mRace\)\s*!=\s*0.*receivedCharacter\s*=\s*true;.*if\s*\(charGenState\.endStage\s*<=\s*1\).*charGenState\.isFinished\s*=\s*true;.*mPendingCharGenStage\s*=\s*-1;' `
    -Missing $missing

Test-Pattern -Name "Server Player construction treats the connection name as account name, not saved character display name" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:__init\(pid,\s*playerName\).*if\s+playerName\s*==\s*nil\s+then\s+self\.accountName\s*=\s*tes3mp\.GetName\(pid\)\s+else\s+self\.accountName\s*=\s*playerName\s+end.*self\.loggedIn\s*=\s*false.*self\.isNewlyRegistered\s*=\s*false.*self\.hasFinishedInitialTeleportation\s*=\s*false' `
    -Missing $missing

Test-Pattern -Name "Server persistence uses XML account world cell and recordstore saves" -Text $serverCore `
    -Pattern 'Database\s*=\s*nil.*Player\s*=\s*nil.*Cell\s*=\s*nil.*RecordStore\s*=\s*nil.*World\s*=\s*nil.*Player\s*=\s*require\("communitymp\.saves\.playerAccountStore"\).*Cell\s*=\s*require\("communitymp\.saves\.cellStateStore"\).*RecordStore\s*=\s*require\("communitymp\.saves\.recordStateStore"\).*World\s*=\s*require\("communitymp\.saves\.worldStateStore"\)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP save codec encodes validates saves and loads XML documents" -Text $saveCodec `
    -Pattern 'saveCodec\.formatName\s*=\s*"CommunityMP XML Save".*function\s+saveCodec\.encode\(kind,\s*data,\s*metadata\).*function\s+saveCodec\.decode\(content\).*function\s+saveCodec\.decodeDocument\(content\).*function\s+saveCodec\.save\(relativePath,\s*kind,\s*data,\s*metadata\).*function\s+saveCodec\.load\(relativePath\)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP account save store keeps roster and character documents under server data saves" -Text $playerAccountStore `
    -Pattern 'local\s+saveRoot\s*=\s*"saves".*local\s+accountFileName\s*=\s*"account\.xml".*local\s+characterRoot\s*=\s*"characters".*storage\.path\s*=\s*characterRoot\s*\.\.\s*"/"\s*\.\.\s*folder\s*\.\.\s*"/"\s*\.\.\s*file.*local\s+function\s+buildAccountDocument\(player\).*accountData\.characters\s*=\s*\{.*selectedIndex.*entries\s*=\s*\{\}.*saveCodec\.save\(storage\.relativePath,\s*"character",\s*characterData\).*local\s+function\s+writeAccountSnapshot\(player,\s*options\).*player:SaveActiveCharacterSlot\(options\.preserveCreatingNewCharacter\s*==\s*true\).*saveCodec\.save\(player\.accountFile,\s*"account",\s*buildAccountDocument\(player\)\).*function\s+Player:LoadFromDrive\(\).*saveCodec\.load\(self\.accountFile\).*jsonInterface\.load\("player/"\s*\.\.\s*self\.legacyAccountFile\).*Migrated legacy JSON player save' `
    -Missing $missing

Test-Pattern -Name "CommunityMP account save store tests cover split documents and legacy migration" -Text $serverLuaCompat `
    -Pattern 'CommunityMpPlayerAccountStoreSplitsAccountsAndCharacters.*saves/Account/account\.xml.*saves/Account/characters/Joe/Joe\.xml.*CommunityMpPlayerAccountStoreMigratesLegacyJsonAccounts.*saves/Legacy/characters/LegacyJoe/LegacyJoe\.xml' `
    -Missing $missing

Test-Pattern -Name "Shared CommunityMP save codec uses atomic temp writes and backup recovery" -Text ($saveCodec + "`n" + $serverLuaCompat) `
    -Pattern 'function\s+saveCodec\.setFileOps\(fileOps\).*function\s+saveCodec\.decodeDocument\(content\).*content:match\("</save>%s\*\$"\).*local\s+tempPath\s*=\s*relativePath\s*\.\.\s*"\.tmp".*local\s+backupPath\s*=\s*relativePath\s*\.\.\s*"\.bak".*movedExistingFile\s*=\s*moveFile\(relativePath,\s*backupPath\).*moveFile\(tempPath,\s*relativePath\).*function\s+saveCodec\.loadDocument\(relativePath\).*Recovered XML save.*CommunityMpSaveCodecRecoversCorruptPrimaryFromBackup.*saves/test\.xml\.bak' `
    -Missing $missing

Test-Pattern -Name "CommunityMP save codec rejects unsafe relative paths before touching disk" -Text ($saveCodec + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+function\s+normalizeRelativePath\(relativePath\).*relativePath:gsub\("\\\\",\s*"/"\).*segment\s*==\s*"\.\.".*function\s+saveCodec\.normalizeRelativePath\(relativePath\).*function\s+saveCodec\.isSafeRelativePath\(relativePath\).*local\s+function\s+getFullPath\(relativePath\).*safeRelativePath\s*==\s*nil.*CommunityMpSaveCodecRejectsUnsafeRelativePaths.*saveCodec\.save\("\.\./server\.cfg".*#openedPaths\s*==\s*0' `
    -Missing $missing

Test-Pattern -Name "CommunityMP account save store sanitizes roster storage paths into account-local character XML" -Text ($playerAccountStore + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+maxStorageSegmentLength\s*=\s*96.*local\s+function\s+getSafeStorageFolder\(characterData,\s*characterIndex,\s*storedFolder\).*fileHelper\.fixFilename\(storedFolder\).*local\s+function\s+getSafeStorageFile\(storedFile,\s*folder\).*storedFile:find.*local\s+function\s+getStorageSeedFromRosterEntry\(rosterEntry\).*storage\.path:match.*Sanitized character XML path.*saveCodec\.load\(storage\.relativePath\).*CommunityMpPlayerAccountStoreSanitizesRosterStoragePaths.*characters/Unsafe_Folder/Unsafe_Folder\.xml' `
    -Missing $missing

Test-Pattern -Name "CommunityMP world cell and record stores use indexed world save registry with legacy migration" -Text ($worldSaveRegistry + "`n" + $worldStateStore + "`n" + $cellStateStore + "`n" + $recordStateStore + "`n" + $serverLuaCompat) `
    -Pattern 'worldSaveRegistry\.schemaVersion\s*=\s*2.*local\s+stateDirectory\s*=\s*saveRoot\s*\.\.\s*"/state".*local\s+manifestPath\s*=\s*saveRoot\s*\.\.\s*"/manifest\.xml".*function\s+worldSaveRegistry\.getGlobalPaths\(\).*coreVariablesPath\s*=\s*coreVariablesPath.*worldPath\s*=\s*worldStatePath.*function\s+worldSaveRegistry\.getCellEntry\(cellDescription\).*"cell\.xml".*function\s+worldSaveRegistry\.getRecordStoreEntry\(storeType\).*"records\.xml".*function\s+worldSaveRegistry\.upsertCell\(cellDescription,\s*cellData,\s*entry\).*objectCount.*function\s+worldSaveRegistry\.upsertRecordStore\(storeType,\s*recordStoreData,\s*entry\).*generatedRecordCount.*local\s+worldSaveRegistry\s*=\s*require\("communitympWorldSaveRegistry"\).*legacyWorldFile\s*=\s*"world/world\.json".*legacyCommunityWorldFile\s*=\s*"saves/world/world\.xml".*Migrated legacy CommunityMP XML world save to indexed world saves.*Migrated legacy JSON world save to XML world saves.*local\s+function\s+saveCellDocument\(cell,\s*quicksave\).*worldSaveRegistry\.upsertCell.*self\.entryPath\s*=\s*self\.worldSaveEntry\.relativePath.*function\s+Cell:QuicksaveToDrive\(\).*compactArray\(self\.data\.packets\).*Migrated legacy CommunityMP XML cell save.*Migrated legacy JSON cell save.*local\s+function\s+saveRecordStoreDocument\(recordStore,\s*quicksave\).*worldSaveRegistry\.upsertRecordStore.*self\.recordstorePath\s*=\s*self\.worldSaveEntry\.relativePath.*Migrated legacy CommunityMP XML recordstore save.*Migrated legacy JSON recordstore save.*CommunityMpWorldStateStoresMigratesLegacyJsonSaves.*globalPaths\.worldPath.*cellEntry\.relativePath.*recordStoreEntry\.relativePath.*CommunityMpWorldStateStoresMigrateOldXmlLayout' `
    -Missing $missing

Test-Pattern -Name "CommunityMP server state registry owns built-in admin config outside flat JSON" -Text ($serverStateRegistry + "`n" + $serverCore + "`n" + $serverConfig + "`n" + $serverLuaCompat) `
    -Pattern 'serverStateRegistry\.schemaVersion\s*=\s*1.*local\s+saveRoot\s*=\s*"saves/server".*local\s+securityDirectory\s*=\s*saveRoot\s*\.\.\s*"/security".*local\s+configDirectory\s*=\s*saveRoot\s*\.\.\s*"/config".*local\s+manifestPath\s*=\s*saveRoot\s*\.\.\s*"/manifest\.xml".*local\s+banListPath\s*=\s*securityDirectory\s*\.\.\s*"/banlist\.xml".*local\s+dataFileRequirementsPath\s*=\s*configDirectory\s*\.\.\s*"/data-files\.xml".*function\s+serverStateRegistry\.saveBanList\(banList\).*saveCodec\.save\(banListPath,\s*"server-banlist".*upsertBanListManifest.*function\s+serverStateRegistry\.loadBanList\(\).*saveCodec\.load\(banListPath\).*jsonInterface\.load\(legacyBanListPath\).*Migrated legacy JSON banlist.*function\s+serverStateRegistry\.loadDataFileRequirements\(filename\).*saveCodec\.load\(dataFileRequirementsPath\).*jsonInterface\.load\(legacyDataFileRequirementsPath\).*Migrated legacy JSON data file requirements.*local\s+serverStateRegistry\s*=\s*require\("communitympServerStateRegistry"\).*function\s+LoadBanList\(\).*serverStateRegistry\.loadBanList\(\).*function\s+SaveBanList\(\).*serverStateRegistry\.saveBanList\(banList\).*function\s+LoadDataFileList\(filename\).*serverStateRegistry\.loadDataFileRequirements\(filename\).*data/saves/server/config/data-files\.xml.*CommunityMpServerStateRegistryMigratesLegacyJsonAdminFiles' `
    -Missing $missing

Test-Pattern -Name "Install staging excludes generated server database smoke and legacy root JSON artifacts" -Text ($cmakeLists + "`n" + $installVerifier + "`n" + $deployScript) `
    -Pattern 'INSTALL\(DIRECTORY\s+"\$\{INSTALL_SOURCE\}/server"\s+DESTINATION\s+"\.".*PATTERN\s+"\*\.db"\s+EXCLUDE.*PATTERN\s+"\*\.json"\s+EXCLUDE.*PATTERN\s+"\*\.txt"\s+EXCLUDE.*PATTERN\s+"\*\.bak"\s+EXCLUDE.*PATTERN\s+"\*\.tmp"\s+EXCLUDE.*PATTERN\s+"data/\*\.db"\s+EXCLUDE.*PATTERN\s+"data/\*\.json"\s+EXCLUDE.*PATTERN\s+"data/\*\.txt"\s+EXCLUDE.*PATTERN\s+"data/\*\.bak"\s+EXCLUDE.*PATTERN\s+"data/\*\.tmp"\s+EXCLUDE.*PATTERN\s+"data/_backup\*"\s+EXCLUDE.*PATTERN\s+"data/saves"\s+EXCLUDE.*PATTERN\s+"data/saves/\*"\s+EXCLUDE.*REGEX\s+"\.\*/data\[\\\\/\]saves\(\[\\\\/\]\.\*\)\?\$"\s+EXCLUDE.*file\(REMOVE_RECURSE\s+"\$\{CMAKE_INSTALL_PREFIX\}/server/data/saves"\).*INSTALL\(FILES\s+"\$\{INSTALL_SOURCE\}/server/data/saves/server/security/banlist\.xml".*INSTALL\(FILES\s+"\$\{INSTALL_SOURCE\}/server/data/saves/server/config/data-files\.xml".*function\s+Assert-NoInstalledServerSaveArtifacts.*\$forbiddenExtensions\s*=\s*@\("\.db",\s*"\.txt",\s*"\.json",\s*"\.bak",\s*"\.tmp"\).*\$unexpectedSaveDirectories\s*=\s*@\(Get-ChildItem\s+-LiteralPath\s+\$saveRoot\s+-Directory.*Where-Object\s+\{\s*\$_\.Name\s+-ne\s+"server"\s*\}\).*Assert-NotInstalledFile\s+"server\\data\\saves\\server\\manifest\.xml".*Assert-InstalledFile\s+"server\\data\\saves\\server\\security\\banlist\.xml".*Assert-InstalledFile\s+"server\\data\\saves\\server\\config\\data-files\.xml".*Assert-NotInstalledFile\s+"server\\data\\database\.db".*Assert-NotInstalledFile\s+"server\\data\\banlist\.json".*Assert-NotInstalledFile\s+"server\\data\\requiredDataFiles\.json".*Assert-NoInstalledServerSaveArtifacts.*saves\\server\\security\\banlist\.xml.*saves\\server\\config\\data-files\.xml' `
    -Missing $missing

foreach ($legacyJsonDefault in @(
        "files\tes3mp\server\data\banlist.json",
        "files\tes3mp\server\data\requiredDataFiles.json"
    )) {
    if (Test-Path -LiteralPath (Join-Path $SourceRoot $legacyJsonDefault) -PathType Leaf) {
        $missing += "Legacy root JSON server defaults are absent from source data"
        break
    }
}

Test-Pattern -Name "Character normalization keeps legacy data but writes current account name only to login.name" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:NormalizeCharacterData\(\).*local\s+legacyLoginName\s*=\s*nil.*if\s+self\.data\.login\s*~=\s*nil\s+and\s+self\.data\.login\.name\s*~=\s*nil\s+and\s+self\.data\.login\.name\s*~=\s*""\s+then\s+legacyLoginName\s*=\s*self\.data\.login\.name.*if\s+\(self\.data\.character\.name\s*==\s*nil\s+or\s+self\.data\.character\.name\s*==\s*""\)\s+and\s+legacyLoginName\s*~=\s*nil\s+then\s+self\.data\.character\.name\s*=\s*legacyLoginName.*if\s+\(self\.data\.character\.name\s*==\s*nil\s+or\s+self\.data\.character\.name\s*==\s*""\)\s+and\s+self\.accountName\s*~=\s*nil\s+and\s+self\.accountName\s*~=\s*""\s+then\s+self\.data\.character\.name\s*=\s*self\.accountName.*self\.data\.login\.name\s*=\s*self\.accountName\s+or\s*""' `
    -Missing $missing

Test-Pattern -Name "New account registration authenticates account and waits in character lobby" -Text ($playerBase + "`n" + $eventHandler) `
    -Pattern 'function\s+BasePlayer:Register\(clientPasswordHash\).*self:EnsureCharacterSlots\(true\).*self\.loggedIn\s*=\s*false.*self\.isNewlyRegistered\s*=\s*true.*self\.accountAuthenticated\s*=\s*true.*self\.creatingNewCharacter\s*=\s*false.*self\.activeCharacterIndex\s*=\s*nil.*self:StopLoginTimer\(\).*self:GenerateSaltedHash\(clientPasswordHash\).*self\.data\.settings\.consoleAllowed\s*=\s*"default".*Players\[pid\]:Register\(data\).*Players\[pid\]:Message\("You have successfully registered\.\\n"\).*Players\[pid\]:Message\("Account accepted\. Select a character or create a new one\.\\n"\).*guiHelper\.ShowCharacterList\(pid\)' `
    -Missing $missing

Test-Pattern -Name "Server connect flow accepts handshake credentials without fallback GUI and processes them after OnPlayerConnect handlers" -Text $eventHandler `
    -Pattern 'eventHandler\.OnPlayerConnect\s*=\s*function\(pid,\s*playerName\).*Players\[pid\]\s*=\s*Player\(pid,\s*playerName\).*if\s+Players\[pid\]\.invalidAccountName\s+then.*Players\[pid\]\s*=\s*nil.*return.*local\s+hasAccount\s*=\s*Players\[pid\]:HasAccount\(\).*local\s+loginAction\s*=\s*guiHelper\.ID\.REGISTER.*if\s+hasAccount\s+then\s+loginAction\s*=\s*guiHelper\.ID\.LOGIN.*local\s+handshakePasswordHash\s*=\s*tes3mp\.GetHandshakePasswordHash\(pid\).*if\s+handshakePasswordHash\s*~=\s*nil\s+and\s+handshakePasswordHash\s*~=\s*""\s+then\s+tes3mp\.ClearHandshakePasswordHash\(pid\).*pendingHandshakePasswordHash\s*=\s*handshakePasswordHash.*pendingLoginAction\s*=\s*loginAction.*if\s+handshakePasswordHash\s*==\s*nil\s+or\s+handshakePasswordHash\s*==\s*""\s+then.*guiHelper\.ShowLogin\(pid\).*guiHelper\.ShowRegister\(pid\).*StartLoginTimer\(pid\).*customEventHooks\.triggerHandlers\("OnPlayerConnect",\s*eventStatus,\s*\{pid\}\).*if\s+pendingHandshakePasswordHash\s*~=\s*nil\s+and\s+pendingHandshakePasswordHash\s*~=\s*""\s+and\s+Players\[pid\]\s*~=\s*nil\s+then\s+ProcessAccountPassword\(pid,\s*pendingLoginAction,\s*pendingHandshakePasswordHash\)' `
    -Missing $missing

Test-Pattern -Name "Login and chargen default messages avoid repeated normal-player instruction popups" -Text ($eventHandler + "`n" + $serverConfig) `
    -Pattern 'local\s+SendStartupScriptsInstructions\s*=\s*function\(pid\).*Players\[pid\]\s*~=\s*nil\s+and\s+Players\[pid\]:IsAdmin\(\)\s+and\s+WorldInstance:HasRunStartupScripts\(\)\s*==\s*false.*Players\[pid\]:Message\(config\.startupScriptsInstructions\).*local\s+SendChatWindowInstructions\s*=\s*function\(pid\).*Players\[pid\]\s*~=\s*nil\s+and\s+config\.chatWindowInstructions\s*~=\s*nil.*Players\[pid\]:Message\(config\.chatWindowInstructions\).*local\s+SendLoginSuccessMessages\s*=\s*function\(pid\).*Players\[pid\]:Message\("You have successfully logged in\.\\n"\).*SendChatWindowInstructions\(pid\).*SendStartupScriptsInstructions\(pid\).*Players\[pid\]:Message\("You have successfully registered\.\\n"\).*Players\[pid\]:Message\("Account accepted\. Select a character or create a new one\.\\n"\).*guiHelper\.ShowCharacterList\(pid\).*Players\[pid\]:StartNewCharacter\(\).*Players\[pid\]:Message\("Create a new character\.\\n"\).*SendChatWindowInstructions\(pid\).*SendStartupScriptsInstructions\(pid\).*config\.useInstancedSpawn\s*=\s*false.*config\.instancedSpawn\s*=\s*\{.*text\s*=\s*nil.*config\.suppressedTutorialInventoryItems\s*=\s*\{\s*"chargen statssheet"\s*\}.*config\.noninstancedSpawn\s*=\s*\{.*cellDescription\s*=\s*"-2,\s*-9".*text\s*=\s*nil' `
    -Missing $missing

Test-Pattern -Name "Client suppresses vanilla startup tutorial popups only before a character is loaded" -Text ($windowManager + "`n" + $clientLocalPlayerHeader + "`n" + $clientLocalPlayer) `
    -Pattern '#include\s+"\.\./mwmp/LocalPlayer\.hpp".*bool\s+isVanillaStartupTutorialMessage\(std::string_view\s+message\).*sInventoryMessage1.*sInventoryMessage2.*sInventoryMessage3.*sInventoryMessage4.*sInventoryMessage5.*sCharGenDoorWarning.*sCharGenDialogueMessage.*bool\s+shouldSuppressMultiplayerPreCharacterMessage\(std::string_view\s+message\).*mwmp::Main::isInitialized\(\).*mwmp::Main::get\(\)\.getLocalPlayer\(\).*localPlayer->hasLoadedCharacter\(\).*Press Space to talk to the Captain.*void\s+WindowManager::messageBox\(std::string_view\s+message.*if\s*\(shouldSuppressMultiplayerPreCharacterMessage\(message\)\)\s+return;.*bool\s+LocalPlayer::hasLoadedCharacter\(\)\s+const.*bool\s+LocalPlayer::isLoggedIn\(\)\s+const.*return\s+hasLoadedCharacter\(\);' `
    -Missing $missing

Test-Pattern -Name "Character selection sends chat instructions before pending startup-script warning" -Text $serverLuaCompat `
    -Pattern 'EventHandlerCharacterSelectionSendsChatAndStartupInstructions.*Message:You have successfully logged in\.\\n.*Message:chat instructions\\n.*IsAdmin.*HasRunStartupScripts.*Message:startup instructions\\n' `
    -Missing $missing

Test-Pattern -Name "EndCharGen saves login, character, class, dynamic stats, equipment, and account before world entry" -Text ($eventHandler + "`n" + $playerBase) `
    -Pattern 'eventHandler\.OnPlayerEndCharGen\s*=\s*function\(pid\).*triggerValidators\("OnPlayerEndCharGen",\s*\{pid\}\).*if\s+eventStatus\.validDefaultHandler\s+then\s+Players\[pid\]:EndCharGen\(\).*triggerHandlers\("OnPlayerEndCharGen",\s*eventStatus,\s*\{pid\}\).*triggerHandlers\("OnPlayerAuthentified",\s*eventStatus,\s*\{pid\}\).*function\s+BasePlayer:EndCharGen\(\).*self:SaveLogin\(\).*self:SaveCharacter\(\).*self:SaveClass\(packetReader\.GetPlayerPacketTables\(self\.pid,\s*"PlayerClass"\)\).*self:SaveStatsDynamic\(packetReader\.GetPlayerPacketTables\(self\.pid,\s*"PlayerStatsDynamic"\)\).*self:SaveEquipment\(packetReader\.GetPlayerPacketTables\(self\.pid,\s*"PlayerEquipment"\)\).*self:SaveIpAddress\(\).*local\s+spawnUsed\s*=\s*self:GetInitialSpawn\(\).*local\s+releaseStateChanges\s*=\s*self:EnsureStartingOfficeReleaseState\(spawnUsed\).*local\s+sharedReleaseStateChanges\s*=\s*self:EnsureSharedStartingOfficeReleaseState\(\).*if\s+self\.hasAccount\s+then\s+self:SaveToDrive\(\)\s+else\s+self:CreateAccount\(\)' `
    -Missing $missing

Test-Pattern -Name "New characters receive server-owned release papers journal and dialogue topics" -Text ($eventHandler + "`n" + $playerBase + "`n" + $playerAccountStore + "`n" + $serverLuaCompat) `
    -Pattern 'startingOfficeReleaseJournal\s*=\s*\{.*quest\s*=\s*"a1_1_findspymaster".*index\s*=\s*1.*startingOfficeReleaseItems\s*=\s*\{.*bk_A1_1_DirectionsCaiusCosades.*bk_a1_1_caiuspackage.*Gold_001.*startingOfficeReleaseTopics\s*=\s*\{.*"duties".*"Caius Cosades".*function\s+BasePlayer:EnsureStartingOfficeReleaseState\(spawnUsed\).*self\.creatingNewCharacter\s*~=\s*true.*inventoryHelper\.addItem\(self\.data\.inventory,\s*item\.refId,\s*item\.count,\s*item\.charge,\s*item\.enchantmentCharge,\s*item\.soul\).*mergeJournalItem\(self\.data\.journal,\s*journalItem\).*recordJournalChanges\(self,\s*\{\s*journalItem\s*\}\).*local\s+acceptedTopics\s*=\s*\{\}.*tableHelper\.containsCaseInsensitiveString\(self\.data\.topics,\s*topicId,\s*false\).*table\.insert\(acceptedTopics,\s*topicId\).*recordTopicChanges\(self,\s*acceptedTopics\).*function\s+BasePlayer:EnsureSharedStartingOfficeReleaseState\(\).*config\.shareJournal\s*==\s*true.*mergeJournalItem\(WorldInstance\.data\.journal,\s*journalItem\).*recordJournalChanges\(WorldInstance,\s*\{\s*journalItem\s*\}\).*config\.shareTopics\s*==\s*true.*local\s+acceptedTopics\s*=\s*\{\}.*tableHelper\.containsCaseInsensitiveString\(WorldInstance\.data\.topics,\s*topicId,\s*false\).*table\.insert\(acceptedTopics,\s*topicId\).*recordTopicChanges\(WorldInstance,\s*acceptedTopics\).*function\s+BasePlayer:QueueStartingOfficeReleaseStateChanges\(releaseStateChanges,\s*sharedReleaseStateChanges\).*pendingStartingOfficeReleaseStateChanges\s*=\s*\{.*items\s*=\s*tableHelper\.deepCopy\(releaseStateChanges\.items\s*or\s*\{\}\).*sharedJournal.*sharedTopics.*function\s+BasePlayer:ApplyStartingOfficeReleaseStateChanges\(\).*pendingStartingOfficeReleaseStateChanges\s*=\s*nil.*self:LoadItemChanges\(releaseStateChanges\.items,\s*enumerations\.inventory\.ADD\).*self:LoadJournal\(\).*self:LoadTopics\(\).*function\s+BasePlayer:EndCharGen\(\).*local\s+releaseStateChanges\s*=\s*self:EnsureStartingOfficeReleaseState\(spawnUsed\).*local\s+sharedReleaseStateChanges\s*=\s*self:EnsureSharedStartingOfficeReleaseState\(\).*QueueStartingOfficeReleaseStateChanges\(.*self:SendLocation\(spawnUsed,\s*\{\s*reason\s*=\s*"chargenSpawn"\s*\}\).*elseif\s+hasQueuedReleaseStateChanges\s+then\s+self:ApplyStartingOfficeReleaseStateChanges\(\).*eventHandler\.OnPlayerCellChange\s*=\s*function\(pid\).*pendingServerLocationChange\.reason\s*==\s*"chargenSpawn".*Players\[pid\]:ApplyStartingOfficeReleaseStateChanges\(\).*PlayerBaseRegisterAndEndCharGenKeepAccountAndCharacterNamesSeparate.*player\.GetInitialSpawn.*return\s+nil.*PlayerBaseEndCharGenSendsInstancedSpawnRecordForCharacterName.*ApplyStartingOfficeReleaseStateChanges.*bk_A1_1_DirectionsCaiusCosades.*a1_1_findspymaster.*Caius Cosades' `
    -Missing $missing

Test-Pattern -Name "Character journal loads hydrate silently while gameplay journal changes stay announced" -Text ($basePlayerHeader + "`n" + $questFunctions + "`n" + $playerJournalPacket + "`n" + $stateHelper + "`n" + $journalInterface + "`n" + $journalImpl + "`n" + $clientLocalPlayer + "`n" + $basePacketTest + "`n" + $serverLuaCompat) `
    -Pattern 'std::vector<JournalItem>\s+journalChanges;\s*bool\s+journalChangesAreLoad\s*=\s*false;.*player->journalChanges\.clear\(\);\s*player->journalChangesAreLoad\s*=\s*false;.*void\s+QuestFunctions::SetJournalChangesAreLoad\(unsigned\s+short\s+pid,\s*bool\s+value\).*player->journalChangesAreLoad\s*=\s*value;.*RW\(player->journalChangesAreLoad,\s*send\).*local\s+function\s+BeginJournalLoadBatch\(pid\).*tes3mp\.SetJournalChangesAreLoad\(pid,\s*true\).*local\s+function\s+FinishJournalLoad\(pid\).*tes3mp\.SetJournalChangesAreLoad\(pid,\s*false\).*function\s+StateHelper:LoadJournal\(pid,\s*stateObject\).*BeginJournalLoadBatch\(pid\).*FinishJournalLoad\(pid\).*virtual\s+void\s+addEntry\(const\s+ESM::RefId&\s+id,\s*int\s+index,\s*const\s+MWWorld::Ptr&\s+actor,\s*bool\s+showMessage\s*=\s*true\).*void\s+Journal::addEntry\(const\s+ESM::RefId&\s+id,\s*int\s+index,\s*const\s+MWWorld::Ptr&\s+actor,\s*bool\s+showMessage\).*if\s*\(showMessage\)\s*MWBase::Environment::get\(\)\.getWindowManager\(\)->messageBox\("#\{sJournalEntry\}"\).*const\s+bool\s+showJournalMessage\s*=\s*!journalChangesAreLoad;.*journalChangesAreLoad\s*&&\s*!mApplyingServerJournalLoad.*getJournal\(\)->clear\(\).*addEntry\(\s*stringRefId\(journalItem\.quest\),\s*journalItem\.index,\s*ptrFound,\s*showJournalMessage\).*sent\.journalChangesAreLoad\s*=\s*true;.*EXPECT_TRUE\(received\.journalChangesAreLoad\).*SetJournalChangesAreLoad:true.*SetJournalChangesAreLoad:false' `
    -Missing $missing

Test-Pattern -Name "Character book loads hydrate by replacing stale read-book state and batching large snapshots" -Text ($basePlayerHeader + "`n" + $playerBookPacket + "`n" + $bookFunctions + "`n" + $playerBase + "`n" + $clientLocalPlayerHeader + "`n" + $npcStats + "`n" + $clientLocalPlayer + "`n" + $basePacketTest + "`n" + $serverLuaCompat) `
    -Pattern 'std::vector<Book>\s+bookChanges;\s*bool\s+bookChangesAreLoad\s*=\s*false;.*RW\(player->bookChangesAreLoad,\s*send\).*player->bookChanges\.clear\(\);\s*player->bookChangesAreLoad\s*=\s*false;.*void\s+BookFunctions::SetBookChangesAreLoad\(unsigned\s+short\s+pid,\s*bool\s+value\).*player->bookChangesAreLoad\s*=\s*value;.*local\s+maxBookChangesPerPacket\s*=\s*3000.*function\s+BasePlayer:LoadBooks\(\).*tes3mp\.SetBookChangesAreLoad\(self\.pid,\s*true\).*pendingChanges\s*>=\s*maxBookChangesPerPacket.*tes3mp\.SetBookChangesAreLoad\(self\.pid,\s*false\).*bool\s+mApplyingServerBookLoad\s*=\s*false;.*void\s+MWMechanics::NpcStats::clearUsedIds\(\).*mUsedIds\.clear\(\).*void\s+LocalPlayer::setBooks\(\).*bookChangesAreLoad\s*&&\s*!mApplyingServerBookLoad.*ptrNpcStats\.clearUsedIds\(\).*mApplyingServerBookLoad\s*=\s*true.*else\s+if\s*\(!bookChangesAreLoad\).*mApplyingServerBookLoad\s*=\s*false.*ptrNpcStats\.flagAsUsed\(stringRefId\(book\.bookId\)\).*bookChangesAreLoad\s*=\s*false;.*playerBookRoundTripsLoadMarker.*PlayerBaseBatchesLargeBookLoads' `
    -Missing $missing

Test-Pattern -Name "Character spellbook loads preserve SET hydration while batching large saved spellbooks" -Text ($playerSpellbookPacket + "`n" + $playerBase + "`n" + $spellbookProcessor + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxSpellbookChanges\s*=\s*3000;.*count\s*>\s*maxSpellbookChanges.*packetValid\s*=\s*false)(?=.*local\s+maxSpellbookChangesPerPacket\s*=\s*3000)(?=.*function\s+BasePlayer:LoadSpellbook\(\).*tes3mp\.SetSpellbookChangesAction\(self\.pid,\s*enumerations\.spellbook\.SET\).*pendingChanges\s*>=\s*maxSpellbookChangesPerPacket.*tes3mp\.SendSpellbookChanges\(self\.pid\).*tes3mp\.SetSpellbookChangesAction\(self\.pid,\s*enumerations\.spellbook\.ADD\).*tes3mp\.AddSpell\(self\.pid,\s*spellId\).*tes3mp\.SendSpellbookChanges\(self\.pid\))(?=.*class\s+ProcessorPlayerSpellbook\s+final.*SpellbookChanges::ADD.*localPlayer\.addSpells\(\).*SpellbookChanges::REMOVE.*localPlayer\.removeSpells\(\).*localPlayer\.setSpellbook\(\))(?=.*PlayerBaseBatchesLargeSpellbookLoads)' `
    -Missing $missing

Test-Pattern -Name "Character active spell loads preserve SET hydration while batching large saved active effects" -Text ($playerSpellsActivePacket + "`n" + $playerBase + "`n" + $packetBuilder + "`n" + $playerSpellsActiveProcessor + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxActiveSpells\s*=\s*3000;.*count\s*>\s*maxActiveSpells.*packetValid\s*=\s*false)(?=.*local\s+maxActiveSpellsPerPacket\s*=\s*3000)(?=.*packetBuilder\.AddPlayerSpellsActive\s*=\s*function\(pid,\s*spellsActive,\s*action,\s*maxChangesPerPacket,\s*sendChanges\).*local\s+currentAction\s*=\s*action.*pendingChanges\s*>=\s*maxChangesPerPacket.*sendChanges\(pid\).*tes3mp\.ClearSpellsActiveChanges\(pid\).*currentAction\s*=\s*enumerations\.spellbook\.ADD.*tes3mp\.SetSpellsActiveChangesAction\(pid,\s*currentAction\).*tes3mp\.AddSpellActiveEffect\(pid,.*tes3mp\.AddSpellActive\(pid,\s*spellId)(?=.*function\s+BasePlayer:LoadSpellsActive\(\).*packetBuilder\.AddPlayerSpellsActive\(self\.pid,\s*self\.data\.spellsActive,\s*enumerations\.spellbook\.SET,\s*maxActiveSpellsPerPacket.*tes3mp\.SendSpellsActiveChanges\(pid,\s*true\).*tes3mp\.SendSpellsActiveChanges\(self\.pid,\s*true\))(?=.*class\s+ProcessorPlayerSpellsActive\s+final.*SpellsActiveChanges::ADD.*localPlayer\.addSpellsActive\(\).*SpellsActiveChanges::REMOVE.*localPlayer\.removeSpellsActive\(\).*localPlayer\.setSpellsActive\(\))(?=.*PlayerBaseBatchesLargeSpellsActiveLoads)' `
    -Missing $missing

Test-Pattern -Name "Equipment saves remove empty slots instead of persisting dense empty equipment records" -Text ($playerBase + "`n" + $serverLuaCompat) `
    -Pattern 'function\s+BasePlayer:SaveEquipment\(playerPacket\).*local\s+newRefId\s*=\s*equipmentItem\.refId.*local\s+newCount\s*=\s*equipmentItem\.count.*if\s+newRefId\s*==\s*""\s+or\s+newCount\s*==\s*nil\s+or\s+newCount\s*<=\s*0\s+then\s+self\.data\.equipment\[slot\]\s*=\s*nil\s+self\.previousEquipment\[slot\]\s*=\s*nil.*self\.data\.equipment\[slot\]\s*=\s*\{.*refId\s*=\s*newRefId.*count\s*=\s*newCount.*self\.previousEquipment\[slot\]\s*=\s*tableHelper\.deepCopy\(self\.data\.equipment\[slot\]\).*PlayerBaseCharacterSlotsPreserveAccountAndLegacyData.*player:SaveEquipment\(\{.*refId\s*=\s*"".*count\s*=\s*0.*assert\(player\.data\.equipment\[enumerations\.equipment\.BOOTS\]\s*==\s*nil\).*assert\(player\.previousEquipment\[enumerations\.equipment\.BOOTS\]\s*==\s*nil\).*common_shirt_01' `
    -Missing $missing

Test-Pattern -Name "Server reconciles missing inventory for accepted equipment outside character generation" -Text $playerBase `
    -Pattern 'local\s+function\s+getInventoryRefIdCount\(inventory,\s*refId\).*type\(inventory\)\s*~=\s*"table"\s+or\s+type\(refId\)\s*~=\s*"string".*local\s+normalizedRefId\s*=\s*string\.lower\(refId\).*string\.lower\(item\.refId\)\s*==\s*normalizedRefId.*local\s+function\s+hasInventoryForEquipmentItem\(data,\s*equipmentItem\).*equipmentItem\.refId\s*==\s*nil\s+or\s+equipmentItem\.refId\s*==\s*"".*equipmentItem\.count\s*==\s*nil\s+or.*equipmentItem\.count\s*<=\s*0.*return\s+getInventoryRefIdCount\(data\.inventory,\s*equipmentItem\.refId\)\s*>=\s*equipmentItem\.count.*function\s+BasePlayer:SaveEquipment\(playerPacket\).*local\s+previousItem\s*=\s*self\.previousEquipment\[slot\].*self\.creatingNewCharacter\s*~=\s*true\s+and\s+\(previousItem\s*==\s*nil\s+or\s+previousItem\.refId\s*~=\s*newRefId\)\s+and\s+not\s+hasInventoryForEquipmentItem\(self\.data,\s*equipmentItem\).*Reconciled equipment for missing inventory item.*self\.data\.equipment\[slot\]\s*=\s*\{.*ensureEquippedItemsInInventory\(self\.data\)' `
    -Missing $missing

Test-Pattern -Name "Equipment ownership accepts charge-mismatched packets for owned items" -Text $serverLuaCompat `
    -Pattern 'local\s+reloads\s*=\s*0.*player\.LoadEquipment\s*=\s*function\(self\).*reloads\s*=\s*reloads\s*\+\s*1.*steel cuirass.*charge\s*=\s*200.*assert\(reloads\s*==\s*0\).*assert\(player\.data\.equipment\[enumerations\.equipment\.CUIRASS\]\.refId\s*==\s*"steel cuirass"\).*Steel Cuirass.*assert\(reloads\s*==\s*0\).*common_shirt_01.*assert\(reloads\s*==\s*0\).*assert\(hasItem\(player\.data\.inventory,\s*"common_shirt_01",\s*1\)\)' `
    -Missing $missing

Test-Pattern -Name "Server ignores inventory SET snapshots missing saved equipped items" -Text ($playerBase + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+function\s+isSuppressedTutorialInventoryItem\(refId\).*config\.suppressedTutorialInventoryItems.*local\s+function\s+removeSuppressedTutorialInventoryItems\(data\).*tableHelper\.cleanNils\(data\.inventory\).*function\s+BasePlayer:CreateCharacterSnapshot\(\).*removeSuppressedTutorialInventoryItems\(self\.data\).*function\s+BasePlayer:LoadInventory\(\).*removeSuppressedTutorialInventoryItems\(self\.data\).*self:QuicksaveToDrive\(\).*function\s+BasePlayer:SaveInventory\(playerPacket\).*removeSuppressedTutorialInventoryItems\(\{\s*inventory\s*=\s*playerPacket\.inventory\s*\}\).*inventorySnapshotIsMissingSavedEquipment\(self\.data,\s*playerPacket\.inventory\).*sent an inventory snapshot missing saved equipped items; keeping saved inventory.*return.*PlayerBaseInventoryPersistenceKeepsEquippedItemsForRelog.*config\.suppressedTutorialInventoryItems\s*=\s*\{\s*"chargen statssheet"\s*\}.*assert\(not\s+hasItem\(snapshot\.inventory,\s*"chargen statssheet",\s*1\)\).*assert\(not\s+hasItem\(sentItems,\s*"chargen statssheet",\s*1\)\).*assert\(quicksaves\s*==\s*1\).*player:SaveInventory\(\{.*action\s*=\s*enumerations\.inventory\.SET,.*chargen statssheet.*assert\(not\s+hasItem\(player\.data\.inventory,\s*"chargen statssheet",\s*1\)\).*assert\(hasItem\(player\.data\.inventory,\s*"common_shirt_01",\s*1\)\).*assert\(hasItem\(player\.data\.inventory,\s*"common_pants_01",\s*1\)\).*assert\(quicksaves\s*==\s*0\)' `
    -Missing $missing

Test-Pattern -Name "Character inventory loads preserve SET hydration while batching large saved inventories" -Text ($playerInventoryPacket + "`n" + $playerBase + "`n" + $inventoryProcessor + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxInventoryChanges\s*=\s*3000;.*count\s*>\s*maxInventoryChanges.*packetValid\s*=\s*false)(?=.*local\s+maxInventoryChangesPerPacket\s*=\s*3000)(?=.*function\s+BasePlayer:LoadInventory\(\).*tes3mp\.SetInventoryChangesAction\(self\.pid,\s*enumerations\.inventory\.SET\).*pendingChanges\s*>=\s*maxInventoryChangesPerPacket.*tes3mp\.SendInventoryChanges\(self\.pid\).*tes3mp\.SetInventoryChangesAction\(self\.pid,\s*enumerations\.inventory\.ADD\).*packetBuilder\.AddPlayerInventoryItemChange\(self\.pid,\s*currentItem\).*tes3mp\.SendInventoryChanges\(self\.pid\))(?=.*class\s+ProcessorPlayerInventory\s+final.*InventoryChanges::ADD.*localPlayer\.addItems\(\).*InventoryChanges::REMOVE.*localPlayer\.removeItems\(\).*localPlayer\.setInventory\(\))(?=.*PlayerBaseBatchesLargeInventoryLoads)' `
    -Missing $missing

Test-Pattern -Name "Character cooldown and faction loads batch large saved snapshots under packet caps" -Text ($playerCooldownsPacket + "`n" + $playerFactionPacket + "`n" + $playerBase + "`n" + $stateHelper + "`n" + $cooldownsProcessor + "`n" + $factionProcessor + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxCooldownChanges\s*=\s*3000;.*count\s*>\s*maxCooldownChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxFactionChanges\s*=\s*3000;.*count\s*>\s*maxFactionChanges.*packetValid\s*=\s*false)(?=.*local\s+maxCooldownChangesPerPacket\s*=\s*3000)(?=.*function\s+BasePlayer:LoadCooldowns\(\).*pendingChanges\s*>=\s*maxCooldownChangesPerPacket.*tes3mp\.SendCooldownChanges\(self\.pid\).*tes3mp\.ClearCooldownChanges\(self\.pid\).*tes3mp\.AddCooldownSpell\(self\.pid,\s*cooldown\.spellId,\s*cooldown\.startDay,\s*cooldown\.startHour\).*tes3mp\.SendCooldownChanges\(self\.pid\))(?=.*local\s+maxFactionChangesPerPacket\s*=\s*3000)(?=.*local\s+function\s+BeginFactionLoadBatch\(pid,\s*action\).*tes3mp\.ClearFactionChanges\(pid\).*tes3mp\.SetFactionChangesAction\(pid,\s*action\))(?=.*local\s+function\s+SendFactionLoadBatchIfFull\(pid,\s*action,\s*pendingChanges\).*pendingChanges\s*<\s*maxFactionChangesPerPacket.*tes3mp\.SendFactionChanges\(pid\).*BeginFactionLoadBatch\(pid,\s*action\).*return\s+0)(?=.*function\s+StateHelper:LoadFactionRanks\(pid,\s*stateObject\).*local\s+action\s*=\s*enumerations\.faction\.RANK.*pendingChanges\s*=\s*SendFactionLoadBatchIfFull\(pid,\s*action,\s*pendingChanges\).*tes3mp\.SetFactionRank\(rank\).*tes3mp\.AddFaction\(pid\).*tes3mp\.SendFactionChanges\(pid\))(?=.*function\s+StateHelper:LoadFactionExpulsion\(pid,\s*stateObject\).*local\s+action\s*=\s*enumerations\.faction\.EXPULSION.*pendingChanges\s*=\s*SendFactionLoadBatchIfFull\(pid,\s*action,\s*pendingChanges\).*tes3mp\.SetFactionExpulsionState\(state\).*tes3mp\.AddFaction\(pid\).*tes3mp\.SendFactionChanges\(pid\))(?=.*function\s+StateHelper:LoadFactionReputation\(pid,\s*stateObject\).*local\s+action\s*=\s*enumerations\.faction\.REPUTATION.*pendingChanges\s*=\s*SendFactionLoadBatchIfFull\(pid,\s*action,\s*pendingChanges\).*tes3mp\.SetFactionReputation\(reputation\).*tes3mp\.AddFaction\(pid\).*tes3mp\.SendFactionChanges\(pid\))(?=.*class\s+ProcessorPlayerCooldowns\s+final.*localPlayer\.setCooldowns\(\))(?=.*class\s+ProcessorPlayerFaction\s+final.*setFactions\(\))(?=.*PlayerBaseBatchesLargeCooldownLoads)(?=.*StateHelperBatchesLargeFactionLoads)' `
    -Missing $missing

Test-Pattern -Name "Worldstate client global map and kill loads batch large saved snapshots under packet caps" -Text ($clientScriptGlobalPacket + "`n" + $worldMapPacket + "`n" + $worldKillCountPacket + "`n" + $stateHelper + "`n" + $playerBase + "`n" + $worldBase + "`n" + $clientWorldstate + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxClientGlobals\s*=\s*3000;.*clientGlobalsCount\s*>\s*maxClientGlobals.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxMapTileChanges\s*=\s*3000;.*changesCount\s*>\s*maxMapTileChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxKillChanges\s*=\s*3000;.*killChangesCount\s*>\s*maxKillChanges.*packetValid\s*=\s*false)(?=.*local\s+maxClientGlobalsPerPacket\s*=\s*3000)(?=.*local\s+maxMapTileChangesPerPacket\s*=\s*3000)(?=.*function\s+StateHelper:LoadClientScriptVariables\(pid,\s*stateObject\).*pendingChanges\s*>=\s*maxClientGlobalsPerPacket.*tes3mp\.SendClientScriptGlobal\(pid\).*tes3mp\.ClearClientGlobals\(\).*tes3mp\.AddClientGlobal.*tes3mp\.SendClientScriptGlobal\(pid\))(?=.*function\s+StateHelper:LoadMap\(pid,\s*stateObject\).*pendingChanges\s*>=\s*maxMapTileChangesPerPacket.*tes3mp\.SendWorldMap\(pid\).*tes3mp\.ClearMapChanges\(\).*tes3mp\.LoadMapTileImageFile\(cellX,\s*cellY,\s*filePath\).*tes3mp\.SendWorldMap\(pid\))(?=.*local\s+maxKillChangesPerPacket\s*=\s*3000.*function\s+BasePlayer:LoadKills\(pid,\s*forEveryone\).*pendingChanges\s*>=\s*maxKillChangesPerPacket.*tes3mp\.SendWorldKillCount\(pid,\s*forEveryone\).*tes3mp\.ClearKillChanges\(\).*tes3mp\.AddKill\(refId,\s*killCount\).*tes3mp\.SendWorldKillCount\(pid,\s*forEveryone\))(?=.*local\s+maxKillChangesPerPacket\s*=\s*3000.*function\s+BaseWorld:LoadKills\(pid,\s*forEveryone\).*pendingChanges\s*>=\s*maxKillChangesPerPacket.*tes3mp\.SendWorldKillCount\(pid,\s*forEveryone\).*tes3mp\.ClearKillChanges\(\).*tes3mp\.AddKill\(refId,\s*killCount\).*tes3mp\.SendWorldKillCount\(pid,\s*forEveryone\))(?=.*void\s+Worldstate::setClientGlobals\(\).*for\s*\(const\s+auto\s+&clientGlobal\s*:\s*clientGlobals\).*world->setGlobal)(?=.*void\s+Worldstate::setKills\(\).*for\s*\(const\s+auto\s+&killChange\s*:\s*killChanges\).*setDeaths)(?=.*void\s+Worldstate::setMapExplored\(\).*for\s*\(const\s+auto\s+&mapTile\s*:\s*mapTiles\).*setGlobalMapImage)(?=.*StateHelperBatchesLargeClientGlobalAndMapLoads)(?=.*PlayerAndWorldBatchLargeKillLoads)' `
    -Missing $missing

Test-Pattern -Name "Player inventory packets reject invalid mutations and sequence authority before character persistence" -Text ($basePlayerHeader + "`n" + $playerInventoryPacket + "`n" + $serverInventoryProcessor + "`n" + $inventoryProcessor + "`n" + $clientLocalPlayer + "`n" + $itemFunctions + "`n" + $playerBase + "`n" + $basePacketTest) `
    -Pattern 'isNewerPlayerInventorySequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*void\s+acceptCurrentInventoryPacket\(\).*acceptedInventorySequence\s*=\s*inventorySequence.*acceptedInventoryChanges\s*=\s*inventoryChanges.*bool\s+acceptInventoryPacket\(\).*isNewerPlayerInventorySequence\(inventorySequence,\s*acceptedInventorySequence\).*restoreAcceptedInventoryPacket\(\).*isValidInventoryAction\(int\s+action\).*InventoryChanges::SET.*InventoryChanges::ADD.*InventoryChanges::REMOVE.*isValidInventoryItem\(const\s+Item&\s+item\).*std::isfinite\(item\.enchantmentCharge\).*item\.refId\.empty\(\).*item\.refId\.find\("\$dynamic"\).*item\.count\s*>\s*0\s*&&\s*item\.count\s*<=\s*maxInventoryItemStackCount.*PacketPlayerInventory::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\).*RW\(inventorySequence,\s*send\).*InventoryChanges\s+inventoryChanges.*inventoryChanges\.items\.reserve\(count\).*if\s*\(!send\s*&&\s*isValidInventoryItem\(item\)\).*inventoryChanges\.items\.push_back\(item\).*player->inventorySequence\s*=\s*inventorySequence;.*player->inventoryChanges\s*=\s*std::move\(inventoryChanges\);.*class\s+ProcessorPlayerInventory.*if\s*\(!player\.acceptInventoryPacket\(\)\).*return;.*class\s+ProcessorPlayerInventory\s+final.*player->acceptInventoryPacket\(\).*void\s+LocalPlayer::sendInventory\(\).*\+\+inventorySequence;.*acceptCurrentInventoryPacket\(\);.*void\s+ItemFunctions::SendInventoryChanges\(unsigned\s+short\s+pid,\s*bool\s+sendToOtherPlayers,\s*bool\s+skipAttachedPlayer\).*\+\+player->inventorySequence;.*player->acceptCurrentInventoryPacket\(\);.*function\s+BasePlayer:SaveInventory\(playerPacket\).*for\s+itemIndex,\s*item\s+in\s+pairs\(playerPacket\.inventory\)\s+do.*playerInventoryRejectsInvalidActions.*playerInventorySkipsInvalidItemMutations.*playerInventorySequenceRejectsStaleAndRestoresAcceptedChanges' `
    -Missing $missing

Test-Pattern -Name "Server clamps impossible inventory removals and reloads authoritative inventory equipment" -Text $playerBase `
    -Pattern '(?s)local\s+function\s+getInventoryRefIdCount\(inventory,\s*refId\).*function\s+BasePlayer:SaveInventory\(playerPacket\).*local\s+reloadAtEnd\s*=\s*false.*local\s+savedCount\s*=\s*getInventoryRefIdCount\(self\.data\.inventory,\s*item\.refId\).*Rejected inventory remove for missing item.*Clamped inventory remove count.*local\s+journalItem\s*=\s*tableHelper\.deepCopy\(item\).*journalItem\.count\s*=\s*removeCount.*recordPlayerInventoryChange\(self,\s*action,\s*\{\s*journalItem\s*\}.*inventoryHelper\.removeClosestItem\(self\.data\.inventory,\s*item\.refId,\s*removeCount,.*ensureEquippedItemsInInventory\(self\.data\).*quicksaveCharacterState\(self\).*if\s+reloadAtEnd\s+then.*self:LoadInventory\(\).*self:LoadEquipment\(\)' `
    -Missing $missing

Test-Pattern -Name "Player equipment packets sequence and validate server-authoritative equipment changes" -Text ($baseStructs + "`n" + $basePlayerHeader + "`n" + $playerEquipmentPacket + "`n" + $serverEquipmentProcessor + "`n" + $equipmentProcessor + "`n" + $clientLocalPlayer + "`n" + $itemFunctions + "`n" + $basePacketTest) `
    -Pattern 'equipmentSlotCount\s*=\s*19.*maxEquipmentItemStackCount\s*=\s*1000000.*isValidEquipmentItem\(const\s+Item&\s+item\).*std::isfinite\(item\.enchantmentCharge\).*item\.refId\.empty\(\).*item\.count\s*==\s*0.*item\.refId\.find\("\$dynamic"\).*item\.count\s*>\s*0\s*&&\s*item\.count\s*<=\s*maxEquipmentItemStackCount.*isNewerPlayerEquipmentSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*void\s+acceptCurrentEquipmentPacket\(\).*acceptedEquipmentSequence\s*=\s*equipmentSequence.*acceptedEquipmentItems\[i\]\s*=\s*equipmentItems\[i\].*bool\s+acceptEquipmentPacket\(\).*!hasValidEquipmentItems\(\).*restoreAcceptedEquipmentPacket\(\).*isNewerPlayerEquipmentSequence\(equipmentSequence,\s*acceptedEquipmentSequence\).*PacketPlayerEquipment::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\).*RW\(equipmentSequence,\s*send\).*isValidEquipmentItem\(equipmentItem\).*class\s+ProcessorPlayerEquipment.*if\s*\(!player\.acceptEquipmentPacket\(\)\).*return;.*class\s+ProcessorPlayerEquipment\s+final.*player->acceptEquipmentPacket\(\).*void\s+LocalPlayer::updateEquipment\(bool\s+forceUpdate\).*\+\+equipmentSequence;.*acceptCurrentEquipmentPacket\(\);.*void\s+ItemFunctions::SendEquipment\(unsigned\s+short\s+pid\).*\+\+player->equipmentSequence;.*player->acceptCurrentEquipmentPacket\(\);.*playerEquipmentRoundTripsSequenceAndCompactChanges.*playerEquipmentCompactChangesPreserveUnmentionedSlots.*playerEquipmentRejectsInvalidItemPayloadsWithoutSlotReplay.*playerEquipmentSequenceRejectsStaleAndRestoresAcceptedSnapshot' `
    -Missing $missing

Test-Pattern -Name "Server corrects sender after rejected inventory and equipment authority packets" -Text ($serverInventoryProcessor + "`n" + $serverEquipmentProcessor) `
    -Pattern 'class\s+ProcessorPlayerInventory.*if\s*\(!player\.acceptInventoryPacket\(\)\).*if\s*\(player\.hasAcceptedInventoryPacket\).*packet\.setPlayer\(&player\);.*packet\.Send\(player\.guid\);.*return;.*class\s+ProcessorPlayerEquipment.*if\s*\(!player\.acceptEquipmentPacket\(\)\).*if\s*\(player\.hasAcceptedEquipmentPacket\).*const\s+bool\s+previousExchangeFullInfo\s*=\s*player\.exchangeFullInfo;.*player\.exchangeFullInfo\s*=\s*true;.*packet\.setPlayer\(&player\);.*packet\.Send\(player\.guid\);.*player\.exchangeFullInfo\s*=\s*previousExchangeFullInfo;.*return;.*player\.sendToLoaded\(&packet\);' `
    -Missing $missing

Test-Pattern -Name "Compact equipment packets only apply changed slots on clients" -Text ($clientLocalPlayer + "`n" + $clientDedicatedPlayer) `
    -Pattern 'void\s+LocalPlayer::setEquipment\(\).*const\s+auto\s+applySlot\s*=\s*\[&\]\(int\s+slot\).*if\s*\(slot\s*<\s*0\s*\|\|\s*slot\s*>=\s*MWWorld::InventoryStore::Slots\).*if\s*\(exchangeFullInfo\).*for\s*\(int\s+slot\s*=\s*0;\s*slot\s*<\s*MWWorld::InventoryStore::Slots;\s*slot\+\+\).*applySlot\(slot\);.*else.*for\s*\(const\s+int\s+slot\s*:\s*equipmentIndexChanges\).*applySlot\(slot\);.*void\s+DedicatedPlayer::setEquipment\(\).*const\s+auto\s+applySlot\s*=\s*\[&\]\(int\s+slot\).*if\s*\(slot\s*<\s*0\s*\|\|\s*slot\s*>=\s*MWWorld::InventoryStore::Slots\).*if\s*\(exchangeFullInfo\).*for\s*\(int\s+slot\s*=\s*0;\s*slot\s*<\s*MWWorld::InventoryStore::Slots;\s*\+\+slot\).*applySlot\(slot\);.*else.*for\s*\(const\s+int\s+slot\s*:\s*equipmentIndexChanges\).*applySlot\(slot\);' `
    -Missing $missing

Test-Pattern -Name "Client defers equipment echo during full server inventory reloads and restores equipped items from authoritative inventory" -Text ($clientLocalPlayerHeader + "`n" + $clientLocalPlayer + "`n" + $inventoryProcessor + "`n" + $equipmentProcessor) `
    -Pattern 'void\s+expectServerEquipmentReload\(\);.*void\s+completeServerEquipmentReload\(\);.*void\s+restoreEquipmentFromInventory\(\);.*float\s+mServerEquipmentReloadTimer\s*=\s*0\.f;.*constexpr\s+float\s+serverEquipmentReloadTimeout\s*=\s*1\.0f;.*void\s+LocalPlayer::expectServerEquipmentReload\(\).*mServerEquipmentReloadTimer\s*=\s*serverEquipmentReloadTimeout;.*void\s+LocalPlayer::completeServerEquipmentReload\(\).*mServerEquipmentReloadTimer\s*=\s*0\.f;.*void\s+LocalPlayer::updateEquipment\(bool\s+forceUpdate\).*if\s*\(mServerEquipmentReloadTimer\s*>\s*0\.f\)\s*return;.*void\s+LocalPlayer::restoreEquipmentFromInventory\(\).*std::find_if.*ptrInventory\.equip\(slot,\s*it\);.*int\s+inventoryAction\s*=\s*localPlayer\.inventoryChanges\.action;.*else\s*//\s*InventoryChanges::SET\s*\{.*localPlayer\.expectServerEquipmentReload\(\);.*localPlayer\.setInventory\(\);.*localPlayer\.restoreEquipmentFromInventory\(\);.*static_cast<LocalPlayer\*>\(player\)->setEquipment\(\);.*static_cast<LocalPlayer\*>\(player\)->completeServerEquipmentReload\(\);' `
    -Missing $missing

Test-Pattern -Name "Client ignores request-style full local snapshots for server-authoritative inventory equipment and spellbook" -Text ($inventoryProcessor + "`n" + $equipmentProcessor + "`n" + $spellbookProcessor) `
    -Pattern 'class\s+ProcessorPlayerInventory\s+final.*if\s*\(isRequest\(\)\)\s*\{.*full local inventory snapshot.*return;.*\}.*else\s+if\s*\(player->acceptInventoryPacket\(\)\).*class\s+ProcessorPlayerEquipment\s+final.*if\s*\(isRequest\(\)\)\s*\{.*full local equipment snapshot.*return;.*\}.*else\s+if\s*\(player->acceptEquipmentPacket\(\)\).*class\s+ProcessorPlayerSpellbook\s+final.*if\s*\(isRequest\(\)\)\s*\{.*full local spellbook snapshot.*return;.*\}.*else\s*\{' `
    -Missing $missing

Test-Pattern -Name "Server quicksaves relog-critical equipment spellbook and quickkey changes while rejecting empty spellbook snapshots" -Text ($playerBase + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+function\s+quicksaveCharacterState\(player\).*type\(player\.QuicksaveToDrive\)\s*==\s*"function".*player:QuicksaveToDrive\(\).*function\s+BasePlayer:SaveEquipment\(playerPacket\).*ensureEquippedItemsInInventory\(self\.data\).*quicksaveCharacterState\(self\).*function\s+BasePlayer:SaveSpellbook\(playerPacket\).*playerPacket\s*==\s*nil\s+or\s+type\(playerPacket\.spellbook\)\s*~=\s*"table".*empty spellbook snapshot; keeping saved spellbook.*return.*quicksaveCharacterState\(self\).*function\s+BasePlayer:SaveQuickKeys\(playerPacket\).*playerPacket\s*==\s*nil\s+or\s+type\(playerPacket\.quickKeys\)\s*~=\s*"table".*quicksaveCharacterState\(self\).*PlayerBaseRelogPersistenceKeepsEquipmentSpellbookAndQuickKeys.*player:SaveEquipment\(\{.*assert\(quicksaves\s*==\s*1\).*player:SaveSpellbook\(\{.*spellbook\s*=\s*\{\}.*assert\(quicksaves\s*==\s*1\).*empty spellbook snapshot.*player:SaveSpellbook\(\{.*almsivi intervention.*assert\(quicksaves\s*==\s*2\).*player:SaveQuickKeys\(\{.*demon_tanto.*assert\(quicksaves\s*==\s*3\)' `
    -Missing $missing

Test-Pattern -Name "Accepted dynamic stat updates persist after login without quicksaving chargen packets" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:SaveStatsDynamic\(playerPacket\).*self:NormalizeDeathState\(\).*self\.data\.death\.isDead\s*==\s*true.*self\.data\.stats\.healthCurrent\s*=\s*0.*local\s+healthBase\s*=\s*playerPacket\.stats\.healthBase.*if\s+healthBase\s*>\s*1\s+then.*self\.data\.stats\.healthCurrent\s*=\s*playerPacket\.stats\.healthCurrent.*self\.data\.stats\.magickaCurrent\s*=\s*playerPacket\.stats\.magickaCurrent.*self\.data\.stats\.fatigueCurrent\s*=\s*playerPacket\.stats\.fatigueCurrent.*if\s+self\.loggedIn\s*==\s*true\s+and\s+self\.hasAccount\s*==\s*true\s+and\s+self\.creatingNewCharacter\s*~=\s*true\s+then\s+quicksaveCharacterState\(self\)' `
    -Missing $missing

Test-Pattern -Name "Account save store keeps character slots while exposing the selected character through legacy player data" -Text ($playerBase + "`n" + $playerAccountStore + "`n" + $serverLuaCompat) `
    -Pattern 'function\s+BasePlayer:StartNewCharacter\(\).*local\s+entries\s*=\s*self:EnsureCharacterSlots\(true\).*self\.activeCharacterIndex\s*=\s*#entries\s*\+\s*1.*self\.creatingNewCharacter\s*=\s*true.*function\s+BasePlayer:SaveActiveCharacterSlot\(preserveCreatingNewCharacter\).*local\s+wasCreatingNewCharacter\s*=\s*self\.creatingNewCharacter\s*==\s*true.*if\s+preserveCreatingNewCharacter\s*~=\s*true\s+then\s+self\.creatingNewCharacter\s*=\s*false\s+else\s+self\.creatingNewCharacter\s*=\s*wasCreatingNewCharacter\s+end.*entries\[targetIndex\]\s*=\s*self:CreateCharacterSnapshot\(\).*function\s+Player:SaveToDrive\(\).*writeAccountSnapshot\(self\).*function\s+Player:QuicksaveToDrive\(\).*writeAccountSnapshot\(self,\s*\{\s*preserveCreatingNewCharacter\s*=\s*self\.creatingNewCharacter\s*==\s*true\s*\}\).*PlayerBaseCharacterSlotsPreserveAccountAndLegacyData.*legacy_quest.*new_quest.*legacy topic.*new topic.*customVariables\.questFlag.*player:SaveActiveCharacterSlot\(true\).*CommunityMpPlayerAccountStoreSplitsAccountsAndCharacters.*player:QuicksaveToDrive\(\).*player\.creatingNewCharacter\s*==\s*true' `
    -Missing $missing

Test-Pattern -Name "Character slot cleanup preserves nested numeric save maps" -Text ($playerBase + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+function\s+compactCharacterEntries\(entries\).*table\.sort\(numericEntries,\s*function\(left,\s*right\).*left\.key\s*<\s*right\.key.*for\s+key\s+in\s+pairs\(entries\)\s+do\s+entries\[key\]\s*=\s*nil.*table\.insert\(entries,\s*entry\.value\).*function\s+BasePlayer:EnsureCharacterSlots\(skipLegacySnapshot\).*compactCharacterEntries\(self\.data\.characters\.entries\).*CommunityMpPlayerAccountStoreSplitsAccountsAndCharacters.*player\.data\.equipment\[enumerations\.equipment\.CARRIED_RIGHT\].*loadedEntry\.equipment\[enumerations\.equipment\.CARRIED_RIGHT\]\.refId\s*==\s*"demon_tanto"' `
    -Missing $missing

Test-Pattern -Name "Generated record player links are scoped to selected character slots" -Text ($playerBase + "`n" + $recordStoreBase + "`n" + $serverLuaCompat) `
    -Pattern 'function\s+BasePlayer:GetCharacterStorageKey\(\).*not\s+self\.creatingNewCharacter.*accountName\s*\.\.\s*"#character:".*function\s+BasePlayer:GetRecordLinkKey\(\).*self:GetCharacterStorageKey\(\).*local\s+function\s+getPlayerRecordLinkKey\(player\).*player:GetRecordLinkKey\(\).*local\s+function\s+playerHasRecordLink\(player,\s*storeType,\s*recordId\).*player\.data\.characters\.entries.*function\s+BaseRecordStore:AddLinkToPlayer\(recordId,\s*player\).*playerLinkKey.*function\s+BaseRecordStore:RemoveLinkToPlayer\(recordId,\s*player\).*legacyAccountName.*not\s+playerHasRecordLink\(player,\s*self\.storeType,\s*recordId\).*RecordStoreKeepsLegacyLinkAndGeneratedRecordSemantics.*AccountName#character:2' `
    -Missing $missing

Test-Pattern -Name "Allies and shared kill credit resolve character storage keys with legacy account fallback" -Text ($defaultCommands + "`n" + $playerBase + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+getPlayerAllyKey\s*=\s*function\(player\).*player:GetCharacterStorageKey\(\).*local\s+containsPlayerReference\s*=\s*function\(values,\s*player\).*tableHelper\.containsValue\(values,\s*getPlayerAllyKey\(player\)\).*tableHelper\.containsValue\(values,\s*player\.accountName\).*local\s+removePlayerReferences\s*=\s*function\(values,\s*player\).*tableHelper\.removeValue\(values,\s*getPlayerAllyKey\(player\)\).*tableHelper\.removeValue\(values,\s*player\.accountName\).*defaultCommands\.inviteAlly.*table\.insert\(Players\[pid\]\.allyInvitesSent,\s*targetAllyKey\).*table\.insert\(Players\[targetPid\]\.allyInvitesReceived,\s*playerAllyKey\).*defaultCommands\.joinTeam.*table\.insert\(Players\[pid\]\.data\.alliedPlayers,\s*getPlayerAllyKey\(Players\[targetPid\]\)\).*defaultCommands\.leaveTeam.*removePlayerReferences\(Players\[pid\]\.data\.alliedPlayers,\s*Players\[targetPid\]\).*function\s+BasePlayer:LoadAllies\(\).*logicHandler\.GetLoggedInPlayerByStorageKey\(otherAllyKey\).*for\s+_,\s*alliedName\s+in\s+ipairs\(Players\[pid\]\.data\.alliedPlayers\).*logicHandler\.GetLoggedInPlayerByStorageKey\(alliedName\).*DefaultAllyCommandsUseCharacterStorageKeysWithLegacyFallback.*PlayerBaseKeepsAlliesBooksMarkAndSelectedSpellCompatibility' `
    -Missing $missing

Test-Pattern -Name "Saved actor AI targets resolve selected character storage keys before legacy account names" -Text ($dataTableBuilder + "`n" + $cellBase + "`n" + $serverLuaCompat) `
    -Pattern 'ai\.targetPlayer\s*=\s*targetPlayer\.accountName.*ai\.targetAccountName\s*=\s*targetPlayer\.accountName.*ai\.targetCharacterName\s*=\s*targetPlayer\.name.*ai\.targetPlayerKey\s*=\s*targetPlayer:GetCharacterStorageKey\(\).*function\s+BaseCell:LoadActorAI\(pid,\s*objectData,\s*uniqueIndexArray\).*if\s+ai\.targetPlayer\s*~=\s*nil\s+or\s+ai\.targetPlayerKey\s*~=\s*nil\s+or\s+ai\.targetAccountName\s*~=\s*nil\s+then.*local\s+targetPlayer\s*=\s*getLoggedInPlayerByStorageKey\(ai\.targetPlayerKey\).*local\s+targetPlayerName\s*=\s*ai\.targetPlayer\s+or\s+ai\.targetAccountName.*if\s+targetPlayer\s*==\s*nil\s+and\s+targetPlayerName\s*~=\s*nil\s+then.*logicHandler\.GetLoggedInPlayerByName\(targetPlayerName\).*CellBaseLoadsActorAITargetsByCharacterStorageKey' `
    -Missing $missing

Test-Pattern -Name "Instanced spawn cell records use the finalized character-name cell before teleport" -Text ($playerBase + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern 'function\s+BasePlayer:SendInstancedSpawnCellRecord\(location\).*local\s+originalCellDescription\s*=\s*config\.instancedSpawn\.cellDescription.*local\s+instancedCellPrefix\s*=\s*originalCellDescription\s*\.\.\s*" - Instance for ".*string\.sub\(location\.cellDescription,\s*1,\s*string\.len\(instancedCellPrefix\)\).*tes3mp\.ClearRecords\(\).*tes3mp\.SetRecordType\(enumerations\.recordType\["CELL"\]\).*packetBuilder\.AddCellRecord\(location\.cellDescription,\s*\{baseId\s*=\s*originalCellDescription\}\).*tes3mp\.SendRecordDynamic\(self\.pid,\s*false,\s*false\).*function\s+BasePlayer:SendLocation\(location,\s*options\).*options\s*=\s*options\s*or\s*\{\}.*self:SendInstancedSpawnCellRecord\(location\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\(options\.reason\s+or\s+"sendLocation",\s*location\.cellDescription,\s*options\).*tes3mp\.SetCell\(self\.pid,\s*location\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\).*tes3mp\.SetPos\(self\.pid,\s*location\.position\[1\],\s*location\.position\[2\],\s*location\.position\[3\]\).*tes3mp\.SetRot\(self\.pid,\s*location\.rotation\[1\],\s*location\.rotation\[2\]\).*tes3mp\.SendCell\(self\.pid\).*tes3mp\.SendPos\(self\.pid\).*Players\[pid\]:SendInstancedSpawnCellRecord\(Players\[pid\]:GetInitialSpawn\(\)\).*PlayerBaseEndCharGenSendsInstancedSpawnRecordForCharacterName.*SetCellChangeReason:8:.*Instance for DisplayName.*Instance for ServerAccount' `
    -Missing $missing

Test-Pattern -Name "PlayerBaseInfo saves character data from packet tables and persists through quicksave" -Text $eventHandler `
    -Pattern 'eventHandler\.OnPlayerBaseInfo\s*=\s*function\(pid\).*local\s+playerPacket\s*=\s*packetReader\.GetPlayerPacketTables\(pid,\s*"PlayerBaseInfo"\).*triggerValidators\("OnPlayerBaseInfo",\s*\{pid,\s*playerPacket\}\).*if\s+eventStatus\.validDefaultHandler\s+then\s+Players\[pid\]:SaveCharacter\(playerPacket\)\s+Players\[pid\]:QuicksaveToDrive\(\).*triggerHandlers\("OnPlayerBaseInfo",\s*eventStatus,\s*\{pid,\s*playerPacket\}\)' `
    -Missing $missing

Test-Pattern -Name "SaveCharacter preserves existing names/race/birthsign when incoming identity packets are incomplete" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:SaveCharacter\(playerPacket\).*local\s+name\s*=.*if\s+name\s*~=\s*nil\s+and\s+name\s*~=\s*""\s+then\s+self\.data\.character\.name\s*=\s*name\s+self\.name\s*=\s*name\s+elseif\s+self\.data\.character\.name\s*~=\s*nil\s+and\s+self\.data\.character\.name\s*~=\s*""\s+then.*keeping saved name.*if\s+race\s*~=\s*nil\s+and\s+race\s*~=\s*""\s+and\s+head\s*~=\s*nil\s+and\s+head\s*~=\s*""\s+and\s+hair\s*~=\s*nil\s+and\s+hair\s*~=\s*""\s+then.*self\.data\.character\.race\s*=\s*race.*elseif\s+self\.data\.character\.race\s*~=\s*nil\s+and\s+self\.data\.character\.race\s*~=\s*"".*keeping saved race.*if\s+birthsign\s*~=\s*nil\s+and\s+birthsign\s*~=\s*""\s+then\s+self\.data\.character\.birthsign\s*=\s*birthsign\s+elseif\s+self\.data\.character\.birthsign\s*~=\s*nil\s+and\s+self\.data\.character\.birthsign\s*~=\s*""\s+then.*keeping saved birthsign' `
    -Missing $missing

Test-Pattern -Name "FinishLogin rejects incomplete saved characters but otherwise loads saved character before gameplay state and saved cell" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:FinishLogin\(\).*if\s+self\.hasAccount\s+then.*self:SaveIpAddress\(\).*self\.data\.timestamps\.lastLogin\s*=\s*os\.time\(\).*self:LoadSettings\(\).*self:NormalizeDeathState\(\).*if\s+not\s+self:HasCompleteCharacter\(\)\s+then\s+self:RestartCharacterGeneration\(\)\s+return\s+false\s+end.*self:LoadCharacter\(\).*self:LoadClass\(\).*self:LoadLevel\(\).*self:LoadAttributes\(\).*self:LoadSkills\(\).*self:LoadStatsDynamic\(\).*WorldInstance:LoadTime\(self\.pid,\s*false\).*self:LoadInventory\(\).*self:LoadEquipment\(\).*self\.loggedIn\s*=\s*true\s+self:StopLoginTimer\(\)\s+self:LoadCell\(\).*triggerHandlers\("OnPlayerFinishLogin",\s*customEventHooks\.makeEventStatus\(true,\s*true\),\s*\{self\.pid\}\).*return\s+true' `
    -Missing $missing

Test-Pattern -Name "LoadCharacter sends saved character display identity back to the client" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:LoadCharacter\(\).*self:NormalizeCharacterData\(\).*tes3mp\.SetName\(self\.pid,\s*self\.data\.character\.name\).*self\.name\s*=\s*self\.data\.character\.name.*tes3mp\.SetRace\(self\.pid,\s*self\.data\.character\.race\).*tes3mp\.SetHead\(self\.pid,\s*self\.data\.character\.head\).*tes3mp\.SetHair\(self\.pid,\s*self\.data\.character\.hair\).*tes3mp\.SetIsMale\(self\.pid,\s*self\.data\.character\.gender\).*tes3mp\.SetBirthsign\(self\.pid,\s*self\.data\.character\.birthsign\).*tes3mp\.SendBaseInfo\(self\.pid\)' `
    -Missing $missing

Write-Host "TES3MP character persistence sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 58"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
