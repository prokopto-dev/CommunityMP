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

    if ($Name -eq "Player position sequences reject stale decoded transforms on client and server") {
        $requiredPatterns = @(
            'bool\s+isNewerSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*const\s+std::uint32_t\s+delta\s*=\s*incoming\s*-\s*current;.*delta\s*!=\s*0u\s*&&\s*delta\s*<\s*0x80000000u',
            'bool\s+isNewerPlayerPositionSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);',
            'bool\s+isPlayerPositionSequenceAtLeast\(std::uint32_t\s+incoming,\s*std::uint32_t\s+minimum\).*return\s+incoming\s*==\s*minimum\s*\|\|\s*isNewerPlayerPositionSequence\(incoming,\s*minimum\);',
            'std::uint32_t\s+positionSequence\s*=\s*0;',
            'bool\s+mHasPendingCellChangePositionSequence\s*=\s*false;.*std::uint32_t\s+mPendingCellChangePositionSequence\s*=\s*0;',
            'bool\s+hasFinitePositionPacket\(\)\s+const.*return\s+isFinitePlayerPosition\(position\)\s*&&\s*isFinitePlayerPosition\(direction\);',
            'bool\s+hasStalePositionPacket\(\)\s+const.*return\s+hasAcceptedPositionPacket.*!isNewerPlayerPositionSequence\(positionSequence,\s*acceptedPositionSequence\);',
            'void\s+restoreAcceptedPositionPacket\(\).*if\s*\(!hasAcceptedPositionPacket\)\s*return;.*positionSequence\s*=\s*acceptedPositionSequence;.*position\s*=\s*acceptedPosition;.*direction\s*=\s*acceptedDirection;',
            'bool\s+acceptPositionPacket\(\).*if\s*\(!hasFinitePositionPacket\(\)\).*restoreAcceptedPositionPacket\(\).*return\s+false;.*if\s*\(hasStalePositionPacket\(\)\).*restoreAcceptedPositionPacket\(\).*return\s+false;.*acceptedPositionSequence\s*=\s*positionSequence;.*acceptedPosition\s*=\s*position;.*acceptedDirection\s*=\s*direction;.*hasAcceptedPositionPacket\s*=\s*true;.*return\s+true;',
            'void\s+LocalPlayer::updateCell\(bool\s+forceUpdate,\s*bool\s+sendPositionPacket\).*if\s*\(sendPositionPacket\).*updatePosition\(true,\s*false,\s*false\);.*\+\+positionSequence;.*mHasPendingCellChangePositionSequence\s*=\s*true;.*mPendingCellChangePositionSequence\s*=\s*positionSequence;',
            'void\s+LocalPlayer::setPosition\(\).*if\s*\(mHasPendingCellChangePositionSequence\s*&&\s*!isPlayerPositionSequenceAtLeast\(positionSequence,\s*mPendingCellChangePositionSequence\)\).*Ignored stale cross-cell server position sequence.*updatePosition\(true,\s*false,\s*false\);.*return;.*if\s*\(mHasPendingCellChangePositionSequence\)\s*mHasPendingCellChangePositionSequence\s*=\s*false;',
            'bool\s+DedicatedPlayer::readPositionPacket\(\).*if\s*\(!acceptPositionPacket\(\)\)\s*return\s+false;',
            'class\s+ProcessorPlayerPosition.*BPP_INIT\(ID_PLAYER_POSITION\).*getServerSimulation\(\)\.acceptPlayerMovementSnapshot\(player,\s*packet\);',
            'bool\s+ServerSimulation::acceptPlayerMovementSnapshot\(Player&\s+player,\s*PlayerPacket&\s+packet\).*if\s*\(!player\.hasFinitePositionPacket\(\)\).*player\.restoreAcceptedPositionPacket\(\);.*packet\.SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\);.*return\s+false;',
            'bool\s+ServerSimulation::acceptPlayerMovementSnapshot\(Player&\s+player,\s*PlayerPacket&\s+packet\).*if\s*\(player\.hasStalePositionPacket\(\)\).*player\.restoreAcceptedPositionPacket\(\);.*return\s+false;',
            'bool\s+ServerSimulation::acceptPlayerMovementSnapshot\(Player&\s+player,\s*PlayerPacket&\s+packet\).*needsInitialSeed.*player\.acceptPositionPacket\(\).*player\.sendToLoaded\(&packet\);',
            'bool\s+isPlausiblePlayerMovement\(\s*const\s+ESM::Position&\s+acceptedPosition,\s*const\s+ESM::Position&\s+clientPosition,\s*float\s+deltaSeconds\).*horizontalDistanceSquared.*maxPlayerMovementUnitsPerSecond\s*\*\s*deltaSeconds\s*\+\s*playerMovementCorrectionAllowance.*maxPlayerVerticalUnitsPerSecond\s*\*\s*deltaSeconds\s*\+\s*playerMovementCorrectionAllowance',
            'bool\s+isLikelyCellSpaceTransitionSnapshot\(\s*const\s+ESM::Position&\s+acceptedPosition,\s*const\s+ESM::Position&\s+clientPosition\).*cellSpaceTransitionDistanceSquared',
            'bool\s+ServerSimulation::acceptPlayerMovementSnapshot\(Player&\s+player,\s*PlayerPacket&\s+packet\).*if\s*\(!isPlausiblePlayerMovement\(player\.acceptedPosition,\s*clientPosition,\s*deltaSeconds\)\).*isLikelyCellSpaceTransitionSnapshot\(.*player\.acceptedPosition,\s*clientPosition\).*player\.restoreAcceptedPositionPacket\(\);.*if\s*\(likelyCellSpaceTransition\).*Ignoring implausible cell-less movement snapshot.*return\s+false;.*packet\.setPlayer\(&player\);.*packet\.SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\);.*return\s+false;.*sanitizeFinitePosition\(clientDirection\);.*normalizeHorizontalIntent\(clientDirection\.pos\[0\],\s*clientDirection\.pos\[1\]\);.*player\.position\s*=\s*clientPosition;.*player\.direction\s*=\s*clientDirection;.*if\s*\(!player\.acceptPositionPacket\(\)\).*return\s+false;.*packet\.setPlayer\(&player\);.*player\.sendToLoaded\(&packet\);',
            'uint32_t\s+BasePacket::SendWithReliability\(bool\s+toOther,\s*PacketReliability\s+forcedReliability\).*reliability\s*=\s*forcedReliability;.*const\s+uint32_t\s+sent\s*=\s*Send\(toOther\);.*reliability\s*=\s*previousReliability;',
            'void\s+PositionFunctions::SendPos\(unsigned\s+short\s+pid\).*isRedundantServerAuthoredPosition\(\*player\).*return;.*\+\+player->positionSequence;.*acceptServerAuthoredPlayerState\(\*player\).*GetPacket\(ID_PLAYER_POSITION\).*packet->setPlayer\(player\);.*packet->SendWithReliability\(false,\s*mwmp::PacketReliability::ReliableOrdered\);',
            'playerPositionRoundTripsSequenceAndMovement',
            'playerPositionUsesUnreliableSequencedMovementDelivery',
            'playerPositionSequenceRejectsAndRestoresStaleDecodedPackets',
            'movementSequenceHelpersHandleWrapAroundBoundaries'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server-authored cell changes carry typed transition reasons") {
        $requiredPatterns = @(
            'SetCellChangeReason.*CellFunctions::SetCellChangeReason',
            'void\s+CellFunctions::SetCellChangeReason\(unsigned\s+short\s+pid,\s*unsigned\s+int\s+reason\).*mwmp::isValidCellChangeReason\(reason\).*player->cellChangeReason\s*=\s*reason;',
            'serverLocationChangeReasons\s*=\s*\{.*respawn\s*=\s*enumerations\.cellChangeReason\.RESPAWN.*questMove\s*=\s*enumerations\.cellChangeReason\.SCRIPT.*teleportToPlayer\s*=\s*enumerations\.cellChangeReason\.SERVER',
            'local\s+function\s+setCellChangeReason\(pid,\s*reason\).*tes3mp\.SetCellChangeReason\(pid,\s*reason\)',
            'function\s+BasePlayer:BeginServerLocationChange\(reason,\s*cellDescription,\s*options\).*cellChangeReason\s*=\s*getServerLocationChangeReason\(reason,\s*options\)',
            'function\s+BasePlayer:SendLocation\(location,\s*options\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\(options\.reason\s+or\s+"sendLocation",\s*location\.cellDescription,\s*options\).*tes3mp\.SetCell\(self\.pid,\s*location\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\)',
            'function\s+BasePlayer:Resurrect\(\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\("respawn",\s*config\.defaultRespawn\.cellDescription\).*tes3mp\.SetCell\(self\.pid,\s*config\.defaultRespawn\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\)',
            'function\s+BasePlayer:LoadCell\(\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\("loadCell",\s*newCell\).*tes3mp\.SetCell\(self\.pid,\s*newCell\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\)',
            'logicHandler\.TeleportToPlayer\s*=\s*function\(pid,\s*originPid,\s*targetPid\).*local\s+pendingLocationChange\s*=\s*originPlayer:BeginServerLocationChange\("teleportToPlayer",\s*targetCell\).*tes3mp\.SetCell\(originPid,\s*targetCell\).*tes3mp\.SetCellChangeReason\(originPid,\s*pendingLocationChange\.cellChangeReason\)',
            'eventHandler\.OnPlayerCellChange\s*=\s*function\(pid\).*pendingServerLocationChange\s*~=\s*nil.*playerPacket\.location\.reason\s*=\s*pendingServerLocationChange\.reason.*playerPacket\.location\.cellChangeReason\s*=\s*pendingServerLocationChange\.cellChangeReason',
            'PlayerBaseConsumesServerLocationChangeForExteriorAliases.*pending\.cellChangeReason\s*==\s*enumerations\.cellChangeReason\.SERVER.*pending\.cellChangeReason\s*==\s*enumerations\.cellChangeReason\.RESPAWN',
            'EventHandlerServerLocationAckPreservesTypedReason'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Client transition sources queue explicit cell-change reasons") {
        $requiredPatterns = @(
            'void\s+LocalPlayer::queueCellChangeReason\(unsigned\s+int\s+reason\).*mwmp::isValidCellChangeReason\(reason\).*cellChangeReason\s*=\s*reason;',
            'void\s+LocalPlayer::updateCell\(bool\s+forceUpdate,\s*bool\s+sendPositionPacket\).*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_CELL_CHANGE\)->Send\(\).*cellChangeReason\s*=\s*mwmp::CELL_CHANGE_REASON_NORMAL;',
            'localPlayer->queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_DOOR\);',
            'queueLocalPlayerCellChangeReason\(const\s+MWWorld::Ptr&\s+target,\s*unsigned\s+int\s+reason\).*localPlayer->queueCellChangeReason\(reason\);',
            'CELL_CHANGE_REASON_MAGIC_DIVINE_INTERVENTION.*CELL_CHANGE_REASON_MAGIC_ALMSIVI_INTERVENTION',
            'queueLocalPlayerCellChangeReason\(target,\s*mwmp::CELL_CHANGE_REASON_MAGIC_RECALL\);',
            'localPlayer->queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_GUIDED_TRAVEL\);',
            'localPlayer->queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_JAIL\);',
            'void\s+LocalPlayer::resurrect\(\).*queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_RESPAWN\).*teleportToClosestMarker\(ptrPlayer,\s*stringRefId\("divinemarker"\)\).*queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_RESPAWN\).*teleportToClosestMarker\(ptrPlayer,\s*stringRefId\("templemarker"\)\)',
            'void\s+queueScriptCellChangeReason\(\).*localPlayer->queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_SCRIPT\);',
            'class\s+OpCOC.*queueScriptCellChangeReason\(\).*MWWorld::ActionTeleport\(refId,\s*pos,\s*false\).*class\s+OpCOE.*queueScriptCellChangeReason\(\).*MWWorld::ActionTeleport\(ESM::RefId::esm3ExteriorCell\(x,\s*y\),\s*pos,\s*false\)',
            'void\s+queueTes3mpScriptCellChangeReasonForPlayer\(\).*localPlayer->queueCellChangeReason\(mwmp::CELL_CHANGE_REASON_SCRIPT\);',
            'void\s+teleportPlayer\(.*bool\s+differentCell\s*=\s*ptr\.getCell\(\)\s*!=\s*destCell;.*if\s*\(differentCell\)\s*queueTes3mpScriptCellChangeReasonForPlayer\(\);.*world->changeToCell'
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

$basePlayer = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$sequence = Get-SourceText "components\openmw-mp\Base\Sequence.hpp"
$dedicatedPlayer = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.cpp"
$dedicatedPlayerHeader = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.hpp"
$localPlayerHeader = Get-SourceText "apps\openmw\mwmp\LocalPlayer.hpp"
$playerList = Get-SourceText "apps\openmw\mwmp\PlayerList.cpp"
$dedicatedActor = Get-SourceText "apps\openmw\mwmp\DedicatedActor.cpp"
$dedicatedActorHeader = Get-SourceText "apps\openmw\mwmp\DedicatedActor.hpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$doorClass = Get-SourceText "apps\openmw\mwclass\door.cpp"
$spellEffects = Get-SourceText "apps\openmw\mwmechanics\spelleffects.cpp"
$travelWindow = Get-SourceText "apps\openmw\mwgui\travelwindow.cpp"
$jailScreen = Get-SourceText "apps\openmw\mwgui\jailscreen.cpp"
$cellExtensions = Get-SourceText "apps\openmw\mwscript\cellextensions.cpp"
$objectBindings = Get-SourceText "apps\openmw\mwlua\objectbindings.cpp"
$characterController = Get-SourceText "apps\openmw\mwmechanics\character.cpp"
$clientCell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$mechanicsHelper = Get-SourceText "apps\openmw\mwmp\MechanicsHelper.cpp"
$playerPositionProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerPosition.hpp"
$playerAnimFlagsProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAnimFlags.hpp"
$playerAnimPlayProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAnimPlay.hpp"
$playerBountyProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerBounty.hpp"
$playerLevelProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerLevel.hpp"
$playerSpellsActiveProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerSpellsActive.hpp"
$clientPlayerProcessor = Get-SourceText "apps\openmw\mwmp\processors\PlayerProcessor.cpp"
$serverPlayerProcessor = Get-SourceText "apps\openmw-mp\processors\PlayerProcessor.cpp"
$serverPlayerPositionProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerPosition.hpp"
$serverSimulation = Get-SourceText "apps\openmw-mp\ServerSimulation.cpp"
$serverPlayerAnimFlagsProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerAnimFlags.hpp"
$serverPlayerMovementSnapshot = Get-SourceText "apps\openmw-mp\processors\player\PlayerMovementSnapshot.hpp"
$serverPlayerAnimPlayProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerAnimPlay.hpp"
$serverPlayerAttackProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerAttack.hpp"
$serverPlayerCastProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerCast.hpp"
$serverPlayerDeathProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerDeath.hpp"
$serverPlayerResurrectProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerResurrect.hpp"
$serverPlayerCellChangeProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerCellChange.hpp"
$serverPlayerDispositionProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerDisposition.hpp"
$serverPlayerShapeshiftProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerShapeshift.hpp"
$serverPlayer = Get-SourceText "apps\openmw-mp\Player.cpp"
$serverCellController = Get-SourceText "apps\openmw-mp\CellController.cpp"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$statsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Stats.cpp"
$itemFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Items.cpp"
$mechanicsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Mechanics.cpp"
$shapeshiftFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Shapeshift.cpp"
$positionFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Positions.cpp"
$cellFunctionsHeader = Get-SourceText "apps\openmw-mp\Script\Functions\Cells.hpp"
$cellFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Cells.cpp"
$networkMessages = Get-SourceText "components\openmw-mp\NetworkMessages.hpp"
$actorSpellsActivePacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorSpellsActive.cpp"
$playerAllyPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAlly.cpp"
$playerBaseInfoPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerBaseInfo.cpp"
$playerBookPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerBook.cpp"
$playerAttributePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAttribute.cpp"
$playerSkillPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerSkill.cpp"
$playerEquipmentPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerEquipment.cpp"
$playerShapeshiftPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerShapeshift.cpp"
$playerAnimFlagsPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAnimFlags.cpp"
$playerAnimPlayPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAnimPlay.cpp"
$playerAttackPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAttack.cpp"
$playerCellChangePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCellChange.cpp"
$playerCellStatePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCellState.cpp"
$playerCastPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCast.cpp"
$playerCooldownsPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCooldowns.cpp"
$playerDeathPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerDeath.cpp"
$playerFactionPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerFaction.cpp"
$playerInventoryPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerInventory.cpp"
$playerJournalPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerJournal.cpp"
$playerPositionPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerPosition.cpp"
$playerQuickKeysPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerQuickKeys.cpp"
$playerSpellbookPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerSpellbook.cpp"
$playerSpellsActivePacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerSpellsActive.cpp"
$playerStatsDynamicPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerStatsDynamic.cpp"
$playerTopicPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerTopic.cpp"
$serverPlayerStatsDynamicProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerStatsDynamic.hpp"
$playerAttackProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAttack.hpp"
$playerCastProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerCast.hpp"
$gnsTransport = Get-SourceText "components\openmw-mp\Transport\GnsTransport.cpp"
$gnsTransportTest = Get-SourceText "apps\components_tests\openmw-mp\gnstransport.cpp"
$basePacket = Get-SourceText "components\openmw-mp\Packets\BasePacket.cpp"
$basePacketTest = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$serverLuaCompat = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"
$serverNetworking = Get-SourceText "apps\openmw-mp\Networking.cpp"
$serverObjectProcessorInitializer = Get-SourceText "apps\openmw-mp\processors\ProcessorInitializer.cpp"
$serverDoorDestinationProcessor = Get-SourceText "apps\openmw-mp\processors\object\ProcessorDoorDestination.hpp"
$objectFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Objects.cpp"
$objectFunctionsHeader = Get-SourceText "apps\openmw-mp\Script\Functions\Objects.hpp"
$scriptFunctions = Get-SourceText "apps\openmw-mp\Script\ScriptFunctions.hpp"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$packetBuilderLua = Get-SourceText "files\tes3mp\server\scripts\packetBuilder.lua"
$packetReaderLua = Get-SourceText "files\tes3mp\server\scripts\packetReader.lua"
$cellBaseLua = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Player movement and bootstrap packets stay on a dedicated NoNagle movement lane" -Text ($networkMessages + "`n" + $playerBaseInfoPacket + "`n" + $playerAttributePacket + "`n" + $playerSkillPacket + "`n" + $playerEquipmentPacket + "`n" + $playerPositionPacket + "`n" + $playerCellChangePacket + "`n" + $playerAnimFlagsPacket + "`n" + $playerAnimPlayPacket + "`n" + $gnsTransport + "`n" + $gnsTransportTest + "`n" + $basePacketTest) `
    -Pattern 'enum\s+OrderingChannel.*CHANNEL_ACTOR,\s*CHANNEL_MOVEMENT,\s*CHANNEL_PLAYER,.*PacketPlayerBaseInfo::PacketPlayerBaseInfo\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_BASEINFO;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerAttribute::PacketPlayerAttribute\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_ATTRIBUTE;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerSkill::PacketPlayerSkill\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_SKILL;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerEquipment::PacketPlayerEquipment\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_EQUIPMENT;.*orderChannel\s*=\s*CHANNEL_PLAYER;.*PacketPlayerPosition::PacketPlayerPosition\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_POSITION;.*priority\s*=\s*PacketPriority::High;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*void\s+PacketPlayerPosition::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*PlayerPacket::Packet\(newBitstream,\s*send\);.*RW\(player->positionSequence,\s*send\);.*RW\(player->position,\s*send,\s*1\);.*RW\(player->direction,\s*send,\s*1\);.*PacketPlayerCellChange::PacketPlayerCellChange\(\).*packetID\s*=\s*ID_PLAYER_CELL_CHANGE;.*priority\s*=\s*PacketPriority::Immediate;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerAnimFlags::PacketPlayerAnimFlags\(\).*packetID\s*=\s*ID_PLAYER_ANIM_FLAGS;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*void\s+mwmp::PacketPlayerAnimFlags::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*PlayerPacket::Packet\(newBitstream,\s*send\);.*RW\(player->positionSequence,\s*send\);.*RW\(player->position,\s*send,\s*true\);.*RW\(player->direction,\s*send,\s*true\);.*RW\(player->animFlagsSequence,\s*send\);.*RW\(player->movementFlags,\s*send\);.*PacketPlayerAnimPlay::PacketPlayerAnimPlay\(\).*packetID\s*=\s*ID_PLAYER_ANIM_PLAY;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*void\s+mwmp::PacketPlayerAnimPlay::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*RW\(player->positionSequence,\s*send\);.*RW\(player->position,\s*send,\s*true\);.*RW\(player->direction,\s*send,\s*true\);.*bool\s+mwmp::GnsTransport::shouldBypassNagle\(PacketPriority\s+priority\).*case\s+PacketPriority::High:.*return\s+true;.*\{\s*ID_ACTOR_POSITION,\s*CHANNEL_ACTOR,\s*PacketPriority::High\s*\}.*\{\s*ID_PLAYER_POSITION,\s*CHANNEL_MOVEMENT,\s*PacketPriority::High\s*\}.*\{\s*ID_PLAYER_INVENTORY,\s*CHANNEL_PLAYER,\s*PacketPriority::High\s*\}.*playerBootstrapStateUsesExpectedDelivery' `
    -Missing $missing

Test-Pattern -Name "New-player bootstrap requests and sends current position and animation snapshots reliably" -Text $serverNetworking `
    -Pattern 'void\s+Networking::newPlayer\(PacketGuid\s+guid\).*GetPacket\(ID_PLAYER_BASEINFO\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_POSITION\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_CELL_CHANGE\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_EQUIPMENT\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_ATTRIBUTE\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_SKILL\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_CELL_CHANGE\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_EQUIPMENT\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_BASEINFO\)->Send\(guid\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->Send\(guid\);.*GetPacket\(ID_PLAYER_ATTRIBUTE\)->Send\(guid\);.*GetPacket\(ID_PLAYER_SKILL\)->Send\(guid\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->SendWithReliability\(\s*guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_CELL_CHANGE\)->Send\(guid\);.*GetPacket\(ID_PLAYER_EQUIPMENT\)->Send\(guid\);' `
    -Missing $missing

Test-Pattern -Name "Player shapeshift appearance state is movement-ordered, bootstrapped, and scoped to loaded clients" -Text ($playerShapeshiftPacket + "`n" + $serverNetworking + "`n" + $serverPlayerShapeshiftProcessor + "`n" + $shapeshiftFunctions + "`n" + $basePacketTest) `
    -Pattern '(?=.*PacketPlayerShapeshift::PacketPlayerShapeshift\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_SHAPESHIFT;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;)(?=.*void\s+Networking::newPlayer\(PacketGuid\s+guid\).*GetPacket\(ID_PLAYER_SHAPESHIFT\)->RequestData\(guid\);.*GetPacket\(ID_PLAYER_SHAPESHIFT\)->setPlayer\(pl->second\);.*GetPacket\(ID_PLAYER_SHAPESHIFT\)->Send\(guid\);)(?=.*class\s+ProcessorPlayerShapeshift.*player\.sendToLoaded\(&packet\);)(?=.*void\s+ShapeshiftFunctions::SendShapeshift\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);)(?=.*PacketPlayerShapeshift\s+shapeshiftPacket;.*EXPECT_EQ\(transport\.sentOrderChannel,\s*CHANNEL_MOVEMENT\);)' `
    -Missing $missing

Test-Pattern -Name "Cell-change player exchanges reset server movement baseline before state fanout" -Text ($serverPlayerCellChangeProcessor + "`n" + $serverSimulation + "`n" + $cellFunctions + "`n" + $positionFunctions) `
    -Pattern 'ProcessorPlayerCellChange.*getPtr\(\)->getServerSimulation\(\)\.acceptPlayerCellChange\(player,\s*packet\).*Script::Call<Script::CallbackIdentity\("OnPlayerCellChange"\)>.*player\.forEachLoaded\(\[this\]\(Player\s*\*pl,\s*Player\s*\*other\).*GetPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(other\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->setPlayer\(other\);.*GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(other\);.*GetPacket\(ID_PLAYER_BASEINFO\)->Send\(pl->guid\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->Send\(pl->guid\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*pl->guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->SendWithReliability\(\s*pl->guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_BASEINFO\)->setPlayer\(pl\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->setPlayer\(pl\);.*GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(pl\);.*GetPacket\(ID_PLAYER_BASEINFO\)->Send\(other->guid\);.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\)->Send\(other->guid\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*other->guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->SendWithReliability\(\s*other->guid,\s*PacketReliability::ReliableOrdered\);.*bool\s+ServerSimulation::acceptServerAuthoredPlayerState\(Player&\s+player,\s*bool\s+cellChangePacket\).*player\.hasAcceptedPositionPacket\s*=\s*false;.*player\.acceptPositionPacket\(\).*movementState\.lastMovementPacket\s*=\s*Clock::now\(\);.*movementState\.hasServerCellChangePacket\s*=\s*true;.*mPlayerAcceptedCells\[player\.guid\]\s*=\s*player\.cell;.*bool\s+ServerSimulation::acceptPlayerCellChange\(Player&\s+player,\s*PlayerPacket&\s+packet\).*acceptServerAuthoredPlayerState\(player,\s*true\).*moveFollowingActorsAcrossPlayerCellChange\(player,\s*previousAcceptedCell\).*void\s+CellFunctions::SendCell\(unsigned\s+short\s+pid\).*?\+\+player->positionSequence;.*acceptServerAuthoredPlayerState\(\*player,\s*true\).*GetPacket\(ID_PLAYER_CELL_CHANGE\).*packet->Send\(false\);.*void\s+PositionFunctions::SendPos\(unsigned\s+short\s+pid\).*isRedundantServerAuthoredPosition\(\*player\).*acceptServerAuthoredPlayerState\(\*player\)' `
    -Missing $missing

$farExteriorEchoPattern = @'
bool\s+isSameSimulationCell\(const\s+ESM::Cell&\s+left,\s*const\s+ESM::Cell&\s+right\).*getCellSimulationKey\(left\)\s*==\s*getCellSimulationKey\(right\).*bool\s+areAdjacentExteriorCells\(const\s+ESM::Cell&\s+left,\s*const\s+ESM::Cell&\s+right\).*left\.isExterior\(\).*right\.isExterior\(\).*deltaX\s*>=\s*-1\s*&&\s*deltaX\s*<=\s*1\s*&&\s*deltaY\s*>=\s*-1\s*&&\s*deltaY\s*<=\s*1.*bool\s+exteriorAxisMatchesPosition\(int\s+cellIndex,\s*float\s+coordinate\).*std::floor\(coordinateValue\s*/\s*cellSize\).*cellIndex\s*==\s*positionCellIndex\s*\+\s*1.*playerMovementCorrectionAllowance.*cellIndex\s*==\s*positionCellIndex\s*-\s*1.*playerMovementCorrectionAllowance.*bool\s+isExteriorCellConsistentWithPosition\(const\s+ESM::Cell&\s+cell,\s*const\s+ESM::Position&\s+position\).*if\s*\(!cell\.isExterior\(\)\)\s*return\s+true;.*exteriorAxisMatchesPosition\(cell\.mData\.mX,\s*position\.pos\[0\]\).*exteriorAxisMatchesPosition\(cell\.mData\.mY,\s*position\.pos\[1\]\).*bool\s+hasServerAcceptedDestinationTransform\(const\s+Player&\s+player\).*player\.hasAcceptedPositionPacket.*isExteriorCellConsistentWithPosition\(player\.cell,\s*player\.position\).*squaredDistance\(player\.acceptedPosition,\s*player\.position\)\s*<=\s*playerMovementCorrectionAllowance\s*\*\s*playerMovementCorrectionAllowance.*bool\s+isCellChangePlausibleFromAcceptedState\(const\s+Player&\s+player,\s*const\s+ESM::Cell&\s+acceptedCell\).*if\s*\(!isExteriorCellConsistentWithPosition\(player\.cell,\s*player\.position\)\)\s*return\s+false;.*if\s*\(isSameSimulationCell\(acceptedCell,\s*player\.cell\)\)\s*return\s+true;.*if\s*\(!acceptedCell\.isExterior\(\)\s*\|\|\s*!player\.cell\.isExterior\(\)\)\s*return\s+mwmp::isExplicitCellChangeReason\(player\.cellChangeReason\);.*if\s*\(areAdjacentExteriorCells\(acceptedCell,\s*player\.cell\)\)\s*return\s+true;.*return\s+mwmp::isExplicitCellChangeReason\(player\.cellChangeReason\)\s*\|\|\s*hasServerAcceptedDestinationTransform\(player\);.*void\s+sendAcceptedPlayerCellCorrection\(Player&\s+player,\s*(?:mwmp::)?PlayerPacket&\s+packet,\s*const\s+ESM::Cell&\s+acceptedCell\).*Rejecting implausible cell change.*player\.cell\s*=\s*acceptedCell;.*player\.cellChangeReason\s*=\s*mwmp::CELL_CHANGE_REASON_SERVER;.*if\s*\(player\.hasAcceptedPositionPacket\)\s*player\.restoreAcceptedPositionPacket\(\);.*packet\.setPlayer\(&player\);.*packet\.SendWithReliability\(player\.guid,\s*(?:mwmp::)?PacketReliability::ReliableOrdered\);.*bool\s+ServerSimulation::acceptPlayerCellChange\(Player&\s+player,\s*PlayerPacket&\s+packet\).*hasPreviousAcceptedCell\s*&&\s*!isCellChangePlausibleFromAcceptedState\(player,\s*previousAcceptedCell\).*sendAcceptedPlayerCellCorrection\(player,\s*packet,\s*previousAcceptedCell\);.*return\s+false;
'@
Test-Pattern -Name "Server rejects implausible far exterior cell-change echoes" -Text ($serverSimulation + "`n" + $serverPlayerCellChangeProcessor) `
    -Pattern $farExteriorEchoPattern `
    -Missing $missing

$playerPositionAuthorityPattern = @'
(?=.*bool\s+isNewerSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*const\s+std::uint32_t\s+delta\s*=\s*incoming\s*-\s*current;.*delta\s*!=\s*0u\s*&&\s*delta\s*<\s*0x80000000u)(?=.*bool\s+isNewerPlayerPositionSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);)(?=.*std::uint32_t\s+positionSequence\s*=\s*0;)(?=.*bool\s+hasFinitePositionPacket\(\)\s+const.*return\s+isFinitePlayerPosition\(position\)\s*&&\s*isFinitePlayerPosition\(direction\);)(?=.*bool\s+hasStalePositionPacket\(\)\s+const.*return\s+hasAcceptedPositionPacket.*!isNewerPlayerPositionSequence\(positionSequence,\s*acceptedPositionSequence\);)(?=.*void\s+restoreAcceptedPositionPacket\(\).*if\s*\(!hasAcceptedPositionPacket\)\s*return;.*positionSequence\s*=\s*acceptedPositionSequence;.*position\s*=\s*acceptedPosition;.*direction\s*=\s*acceptedDirection;)(?=.*bool\s+acceptPositionPacket\(\).*if\s*\(!hasFinitePositionPacket\(\)\).*restoreAcceptedPositionPacket\(\).*return\s+false;.*if\s*\(hasStalePositionPacket\(\)\).*restoreAcceptedPositionPacket\(\).*return\s+false;.*acceptedPositionSequence\s*=\s*positionSequence;.*acceptedPosition\s*=\s*position;.*acceptedDirection\s*=\s*direction;.*hasAcceptedPositionPacket\s*=\s*true;.*return\s+true;)(?=.*bool\s+DedicatedPlayer::readPositionPacket\(\).*if\s*\(!acceptPositionPacket\(\)\)\s*return\s+false;)(?=.*class\s+ProcessorPlayerPosition.*BPP_INIT\(ID_PLAYER_POSITION\).*getServerSimulation\(\)\.acceptPlayerMovementSnapshot\(player,\s*packet\);)(?=.*bool\s+ServerSimulation::acceptPlayerMovementSnapshot\(Player&\s+player,\s*PlayerPacket&\s+packet\).*if\s*\(!player\.hasFinitePositionPacket\(\)\).*player\.restoreAcceptedPositionPacket\(\);.*packet\.SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\);.*return\s+false;.*if\s*\(player\.hasStalePositionPacket\(\)\).*player\.restoreAcceptedPositionPacket\(\);.*return\s+false;.*needsInitialSeed.*player\.acceptPositionPacket\(\).*player\.sendToLoaded\(&packet\);.*if\s*\(!isPlausiblePlayerMovement\(player\.acceptedPosition,\s*clientPosition,\s*deltaSeconds\)\).*packet\.SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\).*sanitizeFinitePosition\(clientDirection\);.*normalizeHorizontalIntent\(clientDirection\.pos\[0\],\s*clientDirection\.pos\[1\]\);.*player\.position\s*=\s*clientPosition;.*player\.direction\s*=\s*clientDirection;.*if\s*\(!player\.acceptPositionPacket\(\)\).*return\s+false;.*player\.sendToLoaded\(&packet\);)(?=.*bool\s+ServerSimulation::acceptServerAuthoredPlayerState\(Player&\s+player,\s*bool\s+cellChangePacket\).*player\.hasAcceptedPositionPacket\s*=\s*false;.*player\.acceptPositionPacket\(\).*movementState\.lastMovementPacket\s*=\s*Clock::now\(\);)(?=.*bool\s+ServerSimulation::isRedundantServerAuthoredPosition\(const\s+Player&\s+player\)\s+const.*hasServerCellChangePacket.*lastServerCellChangePositionSequence.*positionsMatchWithinEpsilon\(player\.position,\s*movementState\.lastServerCellChangePosition\))(?=.*uint32_t\s+BasePacket::SendWithReliability\(bool\s+toOther,\s*PacketReliability\s+forcedReliability\).*reliability\s*=\s*forcedReliability;.*const\s+uint32_t\s+sent\s*=\s*Send\(toOther\);.*reliability\s*=\s*previousReliability;)(?=.*void\s+PositionFunctions::SendPos\(unsigned\s+short\s+pid\).*isRedundantServerAuthoredPosition\(\*player\).*return;.*\+\+player->positionSequence;.*acceptServerAuthoredPlayerState\(\*player\).*GetPacket\(ID_PLAYER_POSITION\).*packet->setPlayer\(player\);.*packet->SendWithReliability\(false,\s*mwmp::PacketReliability::ReliableOrdered\);)(?=.*playerPositionRoundTripsSequenceAndMovement)(?=.*playerPositionUsesUnreliableSequencedMovementDelivery)(?=.*playerPositionSequenceRejectsAndRestoresStaleDecodedPackets)(?=.*movementSequenceHelpersHandleWrapAroundBoundaries)
'@
Test-Pattern -Name "Player position sequences reject stale decoded transforms on client and server" -Text ($basePlayer + "`n" + $sequence + "`n" + $localPlayer + "`n" + $localPlayerHeader + "`n" + $dedicatedPlayer + "`n" + $serverPlayerPositionProcessor + "`n" + $serverSimulation + "`n" + $positionFunctions + "`n" + $basePacket + "`n" + $basePacketTest) `
    -Pattern $playerPositionAuthorityPattern `
    -Missing $missing

Test-Pattern -Name "Player movement derives animation direction from packet and visual transform deltas" -Text ($mechanicsHelper + "`n" + $localPlayer + "`n" + $dedicatedPlayer + "`n" + $dedicatedActor) `
    -Pattern '(?=.*void\s+MechanicsHelper::deriveMissingMovementDirection\(\s*ESM::Position&\s+direction,\s*const\s+ESM::Position&\s+currentPosition,\s*const\s+ESM::Position&\s+previousPosition\).*if\s*\(direction\.pos\[0\]\s*!=\s*0\.f\s*\|\|\s*direction\.pos\[1\]\s*!=\s*0\.f\)\s*return;.*minDerivedMovementDistance.*maxDerivedMovementDistance.*const\s+float\s+yaw\s*=\s*currentPosition\.rot\[2\];.*std::isfinite\(yaw\).*const\s+float\s+localSide\s*=.*const\s+float\s+localForward\s*=.*direction\.pos\[0\]\s*=\s*sanitizeMovementComponent\(localSide\s*/\s*localDistance\);.*direction\.pos\[1\]\s*=\s*sanitizeMovementComponent\(localForward\s*/\s*localDistance\);)(?=.*void\s+LocalPlayer::updatePosition\(bool\s+forceUpdate,\s*bool\s+reliable,\s*bool\s+sendPacket\).*const\s+MWMechanics::Movement&\s+movement\s*=.*direction\.pos\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mPosition\[axis\]\);.*transformWasChanged.*MechanicsHelper::deriveMissingMovementDirection\(direction,\s*position,\s*oldPosition\);)(?=.*bool\s+DedicatedPlayer::readPositionPacket\(\).*const\s+bool\s+hadAcceptedPosition\s*=\s*hasAcceptedPositionPacket;.*const\s+ESM::Position\s+previousAcceptedPosition\s*=\s*acceptedPosition;.*if\s*\(!acceptPositionPacket\(\)\)\s*return\s+false;.*if\s*\(hadAcceptedPosition\).*MechanicsHelper::deriveMissingMovementDirection\(direction,\s*position,\s*previousAcceptedPosition\);.*acceptedDirection\s*=\s*direction;)(?=.*void\s+DedicatedPlayer::move\(float\s+dt\).*const\s+ESM::Position\s+previousVisualPosition\s*=.*ptr\.getRefData\(\)\.getPosition\(\);.*setMovementSettingsFromVisualDelta\(previousVisualPosition\);)(?=.*void\s+DedicatedPlayer::setMovementSettingsFromVisualDelta\(const\s+ESM::Position&\s+previousPosition\).*animationDirection\.pos\[0\]\s*=\s*0\.f;.*animationDirection\.pos\[1\]\s*=\s*0\.f;.*MechanicsHelper::deriveMissingMovementDirection\(animationDirection,\s*ptr\.getRefData\(\)\.getPosition\(\),\s*previousPosition\);.*if\s*\(animationDirection\.pos\[0\]\s*==\s*0\.f\s*&&\s*animationDirection\.pos\[1\]\s*==\s*0\.f\).*animationDirection\.pos\[0\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[0\]\);.*animationDirection\.pos\[1\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[1\]\);.*setMovementSettings\(animationDirection\);)(?=.*void\s+DedicatedActor::move\(float\s+dt\).*const\s+ESM::Position\s+previousVisualPosition\s*=.*ptr\.getRefData\(\)\.getPosition\(\);.*setMovementSettingsFromVisualDelta\(previousVisualPosition\);)(?=.*void\s+DedicatedActor::setMovementSettingsFromVisualDelta\(const\s+ESM::Position&\s+previousPosition\).*animationDirection\.pos\[0\]\s*=\s*0\.f;.*animationDirection\.pos\[1\]\s*=\s*0\.f;.*MechanicsHelper::deriveMissingMovementDirection\(animationDirection,\s*ptr\.getRefData\(\)\.getPosition\(\),\s*previousPosition\);.*if\s*\(animationDirection\.pos\[0\]\s*==\s*0\.f\s*&&\s*animationDirection\.pos\[1\]\s*==\s*0\.f\).*animationDirection\.pos\[0\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[0\]\);.*animationDirection\.pos\[1\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[1\]\);.*setMovementSettings\(animationDirection\);).*' `
    -Missing $missing

Test-Pattern -Name "Remote proxies derive animation direction from pending interpolation targets" -Text ($dedicatedPlayer + "`n" + $dedicatedActor) `
    -Pattern '(?=.*void\s+DedicatedPlayer::setMovementSettingsFromVisualDelta\(const\s+ESM::Position&\s+previousPosition\).*const\s+ESM::Position\s+currentPosition\s*=.*if\s*\(animationDirection\.pos\[0\]\s*==\s*0\.f\s*&&\s*animationDirection\.pos\[1\]\s*==\s*0\.f\)\s*MechanicsHelper::deriveMissingMovementDirection\(animationDirection,\s*position,\s*currentPosition\);)(?=.*void\s+DedicatedActor::setMovementSettingsFromVisualDelta\(const\s+ESM::Position&\s+previousPosition\).*const\s+ESM::Position\s+currentPosition\s*=.*if\s*\(animationDirection\.pos\[0\]\s*==\s*0\.f\s*&&\s*animationDirection\.pos\[1\]\s*==\s*0\.f\)\s*MechanicsHelper::deriveMissingMovementDirection\(animationDirection,\s*position,\s*currentPosition\);)' `
    -Missing $missing

Test-Pattern -Name "Player animation flag sequences reject stale decoded state on client and server" -Text ($basePlayer + "`n" + $sequence + "`n" + $localPlayer + "`n" + $playerAnimFlagsProcessor + "`n" + $serverPlayerAnimFlagsProcessor + "`n" + $serverPlayerCellChangeProcessor + "`n" + $basePacketTest) `
    -Pattern '(?=.*bool\s+isNewerPlayerAnimFlagsSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);)(?=.*std::uint32_t\s+animFlagsSequence\s*=\s*0;)(?=.*bool\s+hasStaleAnimFlagsPacket\(\)\s+const.*return\s+hasAcceptedAnimFlagsPacket.*!isNewerPlayerAnimFlagsSequence\(animFlagsSequence,\s*acceptedAnimFlagsSequence\);)(?=.*void\s+restoreAcceptedAnimFlagsPacket\(\).*if\s*\(!hasAcceptedAnimFlagsPacket\)\s*return;.*animFlagsSequence\s*=\s*acceptedAnimFlagsSequence;.*movementFlags\s*=\s*acceptedMovementFlags;.*drawState\s*=\s*acceptedDrawState;.*isJumping\s*=\s*acceptedIsJumping;.*isFlying\s*=\s*acceptedIsFlying;.*hasTcl\s*=\s*acceptedHasTcl;)(?=.*bool\s+acceptAnimFlagsPacket\(\).*if\s*\(hasStaleAnimFlagsPacket\(\)\).*restoreAcceptedAnimFlagsPacket\(\);.*return\s+false;.*acceptedAnimFlagsSequence\s*=\s*animFlagsSequence;.*acceptedMovementFlags\s*=\s*movementFlags;.*acceptedDrawState\s*=\s*drawState;.*acceptedIsJumping\s*=\s*isJumping;.*acceptedIsFlying\s*=\s*isFlying;.*acceptedHasTcl\s*=\s*hasTcl;.*hasAcceptedAnimFlagsPacket\s*=\s*true;.*return\s+true;)(?=.*void\s+LocalPlayer::updateAnimFlags\(bool\s+forceUpdate\).*?\+\+animFlagsSequence;.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_ANIM_FLAGS\)->setPlayer\(this\);.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_ANIM_FLAGS\)->Send\(\);)(?=.*BPP_INIT\(ID_PLAYER_ANIM_FLAGS\).*if\s*\(player\.hasStaleAnimFlagsPacket\(\)\).*player\.restoreAcceptedAnimFlagsPacket\(\);.*return;.*if\s*\(!player\.acceptAnimFlagsPacket\(\)\)\s*return;.*if\s*\(!normalizePlayerMovementSnapshot\(player\)\)\s*return;.*player\.sendToLoaded\(&packet\);)(?=.*else\s+if\s*\(player\s*!=\s*0\s*&&\s*player->acceptAnimFlagsPacket\(\)\).*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*dedicatedPlayer\.readPositionPacket\(\);.*dedicatedPlayer\.setAnimFlags\(\);)(?=.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->SendWithReliability\(\s*pl->guid,\s*PacketReliability::ReliableOrdered\);)(?=.*GetPacket\(ID_PLAYER_ANIM_FLAGS\)->SendWithReliability\(\s*other->guid,\s*PacketReliability::ReliableOrdered\);)(?=.*playerAnimFlagsRoundTripsSequenceAndState)(?=.*playerAnimFlagsUseUnreliableSequencedMovementDelivery)(?=.*playerAnimFlagsSequenceRejectsAndRestoresStaleDecodedPackets)(?=.*movementSequenceHelpersHandleWrapAroundBoundaries)' `
    -Missing $missing

Test-Pattern -Name "Live player combat packets stay on the movement lane and replay through sequenced position snapshots" -Text ($playerAttackPacket + "`n" + $playerCastPacket + "`n" + $playerAttackProcessor + "`n" + $playerCastProcessor) `
    -Pattern 'PacketPlayerAttack::PacketPlayerAttack\(\)\s*:\s*PlayerPacket\(\)\s*\{.*packetID\s*=\s*ID_PLAYER_ATTACK;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerAttack::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*player->positionSequence.*player->position.*player->direction.*PacketPlayerCast::PacketPlayerCast\(\)\s*:\s*PlayerPacket\(\)\s*\{.*packetID\s*=\s*ID_PLAYER_CAST;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*PacketPlayerCast::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*player->positionSequence.*player->position.*player->direction.*class\s+ProcessorPlayerAttack.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*MechanicsHelper::processAttack\(player->attack,\s*dedicatedPlayer\.getPtr\(\),\s*false\);.*class\s+ProcessorPlayerCast.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*MechanicsHelper::processCast\(player->cast,\s*dedicatedPlayer\.getPtr\(\),\s*false\);' `
    -Missing $missing

Test-Pattern -Name "Server player event packets normalize stale movement snapshots before forwarding" -Text ($serverPlayerMovementSnapshot + "`n" + $serverPlayerAnimFlagsProcessor + "`n" + $serverPlayerAnimPlayProcessor + "`n" + $serverPlayerAttackProcessor + "`n" + $serverPlayerCastProcessor + "`n" + $serverPlayerDeathProcessor) `
    -Pattern 'sendAcceptedPlayerPositionCorrection\(Player&\s+player\).*if\s*\(!player\.hasAcceptedPositionPacket\)\s*return;.*player\.restoreAcceptedPositionPacket\(\);.*GetPacket\(ID_PLAYER_POSITION\);.*packet->setPlayer\(&player\);.*packet->SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\);.*normalizePlayerMovementSnapshot\(Player&\s+player\).*if\s*\(!player\.hasFinitePositionPacket\(\)\).*sendAcceptedPlayerPositionCorrection\(player\);.*return\s+true;.*if\s*\(player\.hasStalePositionPacket\(\)\).*player\.restoreAcceptedPositionPacket\(\);.*return\s+true;.*if\s*\(player\.acceptPositionPacket\(\)\)\s*return\s+true;.*if\s*\(player\.hasAcceptedPositionPacket\)\s+player\.restoreAcceptedPositionPacket\(\);.*if\s*\(!player\.hasAcceptedPositionPacket\)\s*return\s+false;.*return\s+true;.*acceptSequencedPlayerCombatEvent\(Player&\s+player\).*if\s*\(!player\.isCombatPacketSequenceAllowed\(\)\).*player\.acceptCombatPacket\(\);.*return\s+false;.*if\s*\(!normalizePlayerMovementSnapshot\(player\)\).*player\.acceptCurrentCombatPacket\(\);.*class\s+ProcessorPlayerAnimFlags.*if\s*\(player\.hasStaleAnimFlagsPacket\(\)\).*player\.restoreAcceptedAnimFlagsPacket\(\);.*return;.*if\s*\(!player\.acceptAnimFlagsPacket\(\)\)\s*return;.*if\s*\(!normalizePlayerMovementSnapshot\(player\)\)\s*return;.*player\.sendToLoaded\(&packet\);.*class\s+ProcessorPlayerAnimPlay.*if\s*\(!acceptSequencedPlayerCombatEvent\(player\)\)\s*return;.*player\.sendToLoaded\(&packet\);.*class\s+ProcessorPlayerAttack.*if\s*\(!acceptSequencedPlayerCombatEvent\(player\)\)\s*return;.*player\.sendToLoaded\(&packet\);.*class\s+ProcessorPlayerCast.*if\s*\(!acceptSequencedPlayerCombatEvent\(player\)\)\s*return;.*player\.sendToLoaded\(&packet\);.*class\s+ProcessorPlayerDeath.*if\s*\(!player\.isClientDeathPacketAllowed\(\)\).*sendAcceptedStatsDynamicCorrection\(player\);.*return;.*if\s*\(!acceptSequencedPlayerCombatEvent\(player\)\)\s*return;.*player\.creatureStats\.mDead\s*=\s*true;.*player\.sendToLoaded\(&packet\);' `
    -Missing $missing

Test-Pattern -Name "Server ignores client PlayerResurrect packets and resurrects only from script authority" -Text ($serverPlayerResurrectProcessor + "`n" + $mechanicsFunctions) `
    -Pattern '^(?!.*class\s+ProcessorPlayerResurrect.*player\.creatureStats\.mDead\s*=\s*false)(?!.*class\s+ProcessorPlayerResurrect.*player\.sendToLoaded\(&packet\))(?=.*class\s+ProcessorPlayerResurrect.*Received\s+%s\s+from\s+%s)(?=.*void\s+MechanicsFunctions::Resurrect\(unsigned\s+short\s+pid,\s*unsigned\s+int\s+type\).*player->creatureStats\.mDead\s*=\s*false;.*\+\+player->statsDynamicSequence;.*player->acceptCurrentStatsDynamicPacket\(\);.*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*Script::Call<Script::CallbackIdentity\("OnPlayerResurrect"\)>\(player->getId\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "Scripted remote player world-state sends the target directly and scopes observers to loaded clients" -Text ($statsFunctions + "`n" + $itemFunctions + "`n" + $mechanicsFunctions) `
    -Pattern '(?=.*void\s+StatsFunctions::SendBaseInfo\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);)(?=.*void\s+StatsFunctions::SendStatsDynamic\(unsigned\s+short\s+pid\).*\+\+player->statsDynamicSequence;.*player->acceptCurrentStatsDynamicPacket\(\);.*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*player->statsDynamicIndexChanges\.clear\(\);)(?=.*void\s+StatsFunctions::SendAttributes\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*player->attributeIndexChanges\.clear\(\);)(?=.*void\s+StatsFunctions::SendSkills\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*player->skillIndexChanges\.clear\(\);)(?=.*void\s+StatsFunctions::SendLevel\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);)(?=.*void\s+StatsFunctions::SendBounty\(unsigned\s+short\s+pid\).*packet->Send\(false\);.*player->sendToLoaded\(packet\);)(?=.*void\s+ItemFunctions::SendEquipment\(unsigned\s+short\s+pid\).*\+\+player->equipmentSequence;.*player->acceptCurrentEquipmentPacket\(\);.*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*player->equipmentIndexChanges\.clear\(\);)(?=.*void\s+MechanicsFunctions::Resurrect\(unsigned\s+short\s+pid,\s*unsigned\s+int\s+type\).*player->acceptCurrentStatsDynamicPacket\(\);.*packet->Send\(false\);.*player->sendToLoaded\(packet\);.*OnPlayerResurrect)' `
    -Missing $missing

Test-Pattern -Name "Legacy player disposition packets are scoped to loaded observers before Lua callbacks" -Text $serverPlayerDispositionProcessor `
    -Pattern 'BPP_INIT\(ID_PLAYER_DISPOSITION\).*DEBUG_PRINTF\(strPacketID\.c_str\(\)\);.*player\.sendToLoaded\(&packet\);.*Script::Call<Script::CallbackIdentity\("OnPlayerDisposition"\)>\(player\.getId\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Server player packet fanout skips null and uninitialized observer sessions" -Text $serverPlayer `
    -Pattern 'void\s+Player::sendToLoaded\(mwmp::PlayerPacket\s+\*myPacket\).*for\s*\(auto\s+loadedCell\s*:\s*cells\).*if\s*\(loadedCell\s*==\s*nullptr\)\s*continue;.*for\s*\(auto\s+pl\s*:\s*\*loadedCell\).*pl\s*!=\s*nullptr\s*&&\s*!pl->npc\.mName\.empty\(\).*plList\.push_back\(pl\);.*plList\.sort\(\);.*plList\.unique\(\);.*if\s*\(pl\s*==\s*this\)\s*continue;.*myPacket->setPlayer\(this\);.*myPacket->Send\(pl->guid\);' `
    -Missing $missing

Test-Pattern -Name "Server player loaded-cell iteration skips null cells during callbacks and cleanup" -Text ($serverPlayer + "`n" + $serverCellController) `
    -Pattern 'void\s+Player::forEachLoaded\(std::function<void\(Player\s*\*pl,\s*Player\s*\*other\)>\s+func\).*for\s*\(auto\s+loadedCell\s*:\s*cells\).*if\s*\(loadedCell\s*==\s*nullptr\)\s*continue;.*for\s*\(auto\s+pl\s*:\s*\*loadedCell\).*pl\s*!=\s*nullptr\s*&&\s*!pl->npc\.mName\.empty\(\).*void\s+CellController::deletePlayer\(Player\s+\*player\).*Cell\s+\*c\s*=\s*\*it;.*if\s*\(c\s*==\s*nullptr\)\s*continue;.*c->removePlayer\(player,\s*false\);' `
    -Missing $missing

Test-Pattern -Name "Server cell deletion scrubs active player loaded-cell pointers before freeing cells" -Text $serverCellController `
    -Pattern 'void\s+CellController::removeCell\(Cell\s+\*cell\).*if\s*\(cell\s*==\s*nullptr\)\s*return;.*for\s*\(auto\s+it\s*=\s*cells\.begin\(\);\s*it\s*!=\s*cells\.end\(\);\).*if\s*\(\*it\s*!=\s*nullptr\s*&&\s*\*it\s*==\s*cell\).*for\s*\(auto&&\s+playerEntry\s*:\s*\*Players::getPlayers\(\)\).*Player\s+\*player\s*=\s*playerEntry\.second;.*if\s*\(player\s*==\s*nullptr\)\s*continue;.*CellController::TContainer\s+\*loadedCells\s*=\s*player->getCells\(\);.*loadedCells->erase\(std::remove\(loadedCells->begin\(\),\s*loadedCells->end\(\),\s*cell\),\s*loadedCells->end\(\)\);.*delete\s+\*it;' `
    -Missing $missing

Test-Pattern -Name "Local player attack and cast packets do not defer each other in the same update" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::updateAttackOrCast\(\).*const\s+bool\s+attackReady\s*=\s*attack\.shouldSend\s*&&\s*!MechanicsHelper::shouldDeferLocalAttack\(attack\);.*if\s*\(attackReady\s*\|\|\s*cast\.shouldSend\)\s*updatePosition\(true\);.*if\s*\(attackReady\).*getPlayerPacket\(ID_PLAYER_ATTACK\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_ATTACK\)->Send\(\);.*attack\.shouldSend\s*=\s*false;.*if\s*\(cast\.shouldSend\).*getPlayerPacket\(ID_PLAYER_CAST\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_CAST\)->Send\(\);.*cast\.shouldSend\s*=\s*false;.*cast\.hasProjectile\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Player combat packet coverage pins movement snapshots" -Text ($playerAttackPacket + "`n" + $playerCastPacket + "`n" + $basePacketTest) `
    -Pattern 'PacketPlayerAttack::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*player->positionSequence.*player->position.*player->direction.*PacketPlayerCast::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*player->positionSequence.*player->position.*player->direction.*playerAttackRoundTripsCombatTransformAndHitState.*playerCastRoundTripsCombatTransformAndProjectileState' `
    -Missing $missing

Test-Pattern -Name "Player damage and death state packets stay on the movement lane" -Text ($playerStatsDynamicPacket + "`n" + $playerDeathPacket + "`n" + $basePacketTest) `
    -Pattern 'PacketPlayerStatsDynamic::PacketPlayerStatsDynamic\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_STATS_DYNAMIC;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*RW\(player->statsDynamicSequence,\s*send\);.*RW\(player->creatureStats\.mDead,\s*send\);.*PacketPlayerDeath::PacketPlayerDeath\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_DEATH;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*playerDeathUsesMovementDelivery.*playerStatsDynamicUsesMovementDelivery.*playerStatsDynamicRoundTripsDeathState.*playerStatsDynamicRejectsStaleSequencesAndRestoresAcceptedSnapshot' `
    -Missing $missing

Test-Pattern -Name "Server corrects sender after rejected player dynamic stat updates" -Text $serverPlayerStatsDynamicProcessor `
    -Pattern 'ProcessorPlayerStatsDynamic.*if\s*\(!player\.acceptStatsDynamicPacket\(true\)\).*if\s*\(player\.hasAcceptedStatsDynamicPacket\).*packet\.setPlayer\(&player\);.*packet\.Send\(player\.guid\);.*return;.*player\.sendToLoaded\(&packet\);' `
    -Missing $missing

Test-Pattern -Name "Server corrects premature player death packets with full accepted stats" -Text $serverPlayerDeathProcessor `
    -Pattern 'sendAcceptedStatsDynamicCorrection\(Player&\s+player\).*if\s*\(!player\.hasAcceptedStatsDynamicPacket\)\s*return;.*previousExchangeFullInfo\s*=\s*player\.exchangeFullInfo;.*previousStatsDynamicIndexChanges\s*=\s*player\.statsDynamicIndexChanges;.*player\.restoreAcceptedStatsDynamicPacket\(\);.*player\.exchangeFullInfo\s*=\s*true;.*GetPacket\(ID_PLAYER_STATS_DYNAMIC\);.*packet->setPlayer\(&player\);.*packet->SendWithReliability\(player\.guid,\s*PacketReliability::ReliableOrdered\);.*player\.exchangeFullInfo\s*=\s*previousExchangeFullInfo;.*player\.statsDynamicIndexChanges\s*=\s*previousStatsDynamicIndexChanges;.*if\s*\(!player\.isClientDeathPacketAllowed\(\)\).*sendAcceptedStatsDynamicCorrection\(player\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Invalid decoded player packets are not forwarded or replayed" -Text ($clientPlayerProcessor + "`n" + $serverPlayerProcessor) `
    -Pattern 'bool\s+PlayerProcessor::Process\(ReceivedPacket&\s+packet\).*myPacket->Read\(\);.*if\s*\(!myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*return\s+true;.*bool\s+PlayerProcessor::Process\(ReceivedPacket&\s+packet\)\s+noexcept.*myPacket->Read\(\);.*if\s*\(!myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*return\s+true;' `
    -Missing $missing

Test-Pattern -Name "Compact player stat and equipment packets reject oversized counts and invalid slots" -Text ($playerAttributePacket + "`n" + $playerSkillPacket + "`n" + $playerStatsDynamicPacket + "`n" + $playerEquipmentPacket + "`n" + $basePacketTest) `
    -Pattern 'constexpr\s+uint32_t\s+maxAttributeIndexes\s*=\s*8;.*count\s*>\s*maxAttributeIndexes.*packetValid\s*=\s*false;.*attributeIndex\s*>=\s*maxAttributeIndexes.*constexpr\s+uint32_t\s+maxSkillIndexes\s*=\s*27;.*count\s*>\s*maxSkillIndexes.*packetValid\s*=\s*false;.*skillId\s*>=\s*maxSkillIndexes.*constexpr\s+uint32_t\s+maxStatsDynamicIndexes\s*=\s*3;.*count\s*>\s*maxStatsDynamicIndexes.*packetValid\s*=\s*false;.*constexpr\s+uint32_t\s+maxEquipmentIndexes\s*=\s*equipmentSlotCount;.*count\s*>\s*maxEquipmentIndexes.*packetValid\s*=\s*false;.*equipmentIndex\s*<\s*0\s*\|\|\s*static_cast<uint32_t>\(equipmentIndex\)\s*>=\s*maxEquipmentIndexes.*playerCompactStatPacketsRejectOversizedCountsBeforeResize.*playerEquipmentRejectsInvalidCompactSlotBeforeItemReplay' `
    -Missing $missing

Test-Pattern -Name "List-based player and active spell packets reject oversized network counts before resize" -Text ($playerAllyPacket + "`n" + $playerBookPacket + "`n" + $playerCellStatePacket + "`n" + $playerCooldownsPacket + "`n" + $playerFactionPacket + "`n" + $playerInventoryPacket + "`n" + $playerJournalPacket + "`n" + $playerQuickKeysPacket + "`n" + $playerSpellbookPacket + "`n" + $playerSpellsActivePacket + "`n" + $playerTopicPacket + "`n" + $actorSpellsActivePacket + "`n" + $basePacketTest) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxAlliedPlayers\s*=\s*1000;.*count\s*>\s*maxAlliedPlayers.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxBookChanges\s*=\s*3000;.*count\s*>\s*maxBookChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxCellStateChanges\s*=\s*3000;.*count\s*>\s*maxCellStateChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxCooldownChanges\s*=\s*3000;.*count\s*>\s*maxCooldownChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxFactionChanges\s*=\s*3000;.*count\s*>\s*maxFactionChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxInventoryChanges\s*=\s*3000;.*count\s*>\s*maxInventoryChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxJournalChanges\s*=\s*3000;.*count\s*>\s*maxJournalChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxQuickKeyChanges\s*=\s*10;.*count\s*>\s*maxQuickKeyChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxSpellbookChanges\s*=\s*3000;.*count\s*>\s*maxSpellbookChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxTopicChanges\s*=\s*3000;.*count\s*>\s*maxTopicChanges.*packetValid\s*=\s*false)(?=.*PacketPlayerSpellsActive::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*count\s*>\s*maxActiveSpells.*packetValid\s*=\s*false)(?=.*PacketActorSpellsActive::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*count\s*>\s*maxActiveSpells.*packetValid\s*=\s*false)(?=.*effectCount\s*>\s*maxEffects.*packetValid\s*=\s*false)(?=.*hasFiniteActiveSpellValues\(const\s+ActiveSpell&\s+activeSpell\).*std::isfinite\(activeSpell\.timestampHour\).*std::isfinite\(effect\.mMagnitude\).*std::isfinite\(effect\.mDuration\).*std::isfinite\(effect\.mTimeLeft\))(?=.*PacketPlayerSpellsActive::Packet.*!hasFiniteActiveSpellValues\(activeSpell\).*packetValid\s*=\s*false.*player->spellsActiveChanges\.activeSpells\.clear\(\))(?=.*PacketActorSpellsActive::Actor.*!hasFiniteActiveSpellValues\(activeSpell\).*packetValid\s*=\s*false.*actor\.spellsActiveChanges\.activeSpells\.clear\(\))(?=.*playerListPacketsRejectOversizedCountsBeforeResize)(?=.*actorSpellsActiveRejectsOversizedCountBeforeResize)(?=.*playerSpellsActiveRejectsNonFiniteEffectValues)(?=.*actorSpellsActiveRejectsNonFiniteEffectValues)' `
    -Missing $missing

Test-Pattern -Name "Player animation play packets apply movement snapshots through sequenced replay" -Text ($playerAnimPlayPacket + "`n" + $playerAnimPlayProcessor + "`n" + $basePacketTest) `
    -Pattern 'PacketPlayerAnimPlay::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*RW\(player->positionSequence,\s*send\);.*RW\(player->position,\s*send,\s*true\);.*RW\(player->direction,\s*send,\s*true\);.*class\s+ProcessorPlayerAnimPlay.*DedicatedPlayer&\s+dedicatedPlayer\s*=\s*static_cast<DedicatedPlayer&>\(\*player\);.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*dedicatedPlayer\.playAnimation\(\);.*playerAnimPlayRoundTripsCombatTransformAndAnimation.*playerMovementAnimationPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Local player update preserves fractional packet cadence" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::update\(\).*static\s+float\s+updateTimer\s*=\s*0;.*const\s+float\s+timeoutSec\s*=\s*0\.015f;.*if\s*\(\(updateTimer\s*\+=\s*MWBase::Environment::get\(\)\.getFrameDuration\(\)\)\s*>=\s*timeoutSec\).*updateTimer\s*=\s*std::fmod\(updateTimer,\s*timeoutSec\);.*updatePosition\(\);.*updateAnimFlags\(\);.*updateEquipment\(\);.*updateStatsDynamic\(\);.*updateAttackOrCast\(\);' `
    -Missing $missing

Test-Pattern -Name "Movement snapshot sanitizer drops invalid and near-zero input before send" -Text $mechanicsHelper `
    -Pattern 'float\s+MechanicsHelper::sanitizeMovementComponent\(float\s+value\).*constexpr\s+float\s+movementEpsilon\s*=\s*0\.0001f;.*!std::isfinite\(value\)\s*\|\|\s*std::abs\(value\)\s*<=\s*movementEpsilon.*return\s+0\.f;.*return\s+value;' `
    -Missing $missing

Test-Pattern -Name "Local player position packets snapshot movement input and full transform drift" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::updatePosition\(bool\s+forceUpdate,\s*bool\s+reliable,\s*bool\s+sendPacket\).*static\s+ESM::Position\s+oldPosition;.*position\s*=\s*ptrPlayer\.getRefData\(\)\.getPosition\(\);.*const\s+MWMechanics::Movement&\s+movement\s*=.*direction\.pos\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mPosition\[axis\]\);.*direction\.rot\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mRotation\[axis\]\);.*const\s+float\s+transformEpsilon\s*=\s*0\.0001f;.*transformWasChanged.*bool\s+posIsChanging\s*=' `
    -Missing $missing

Test-Pattern -Name "Local player position sends can be suppressed for server transforms" -Text $localPlayer `
    -Pattern 'const\s+auto\s+sendPosition\s*=\s*\[this,\s*reliable,\s*sendPacket\]\(\).*if\s*\(!sendPacket\)\s*return;.*\+\+positionSequence;.*if\s*\(!sendPacket\s*&&\s*forceUpdate\).*oldPosition\s*=\s*position;.*posWasChanged\s*=\s*false;.*sentJumpEnd\s*=\s*true;.*return;' `
    -Missing $missing

Test-Pattern -Name "Local player position packets use requested delivery" -Text $localPlayer `
    -Pattern 'PlayerPacket\*\s+packet\s*=\s*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_POSITION\);.*packet->setPlayer\(this\);.*if\s*\(reliable\)\s*packet->SendWithReliability\(true,\s*PacketReliability::ReliableOrdered\);.*else\s+packet->Send\(\);.*if\s*\(forceUpdate\s*\|\|\s*posIsChanging\s*\|\|\s*posWasChanged\).*oldPosition\s*=\s*position;.*sendPosition\(\);.*else\s+if\s*\(!sentJumpEnd\).*oldPosition\s*=\s*position;.*sendPosition\(\);' `
    -Missing $missing

Test-Pattern -Name "Local cell-change snapshots embed transform without pre-cell position sends" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::updateCell\(bool\s+forceUpdate,\s*bool\s+sendPositionPacket\).*previousCellPosition\s*=\s*position;.*if\s*\(sendPositionPacket\).*updatePosition\(true,\s*false,\s*false\);.*\+\+positionSequence;.*mHasPendingCellChangePositionSequence\s*=\s*true;.*mPendingCellChangePositionSequence\s*=\s*positionSequence;.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_CELL_CHANGE\)->setPlayer\(this\);.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_CELL_CHANGE\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Client transition sources queue explicit cell-change reasons" -Text ($localPlayer + "`n" + $doorClass + "`n" + $spellEffects + "`n" + $travelWindow + "`n" + $jailScreen + "`n" + $cellExtensions + "`n" + $objectBindings) `
    -Pattern '.' `
    -Missing $missing

Test-Pattern -Name "Server-driven cell changes use packet positions before acknowledgement" -Text ($localPlayer + "`n" + $playerBase) `
    -Pattern 'void\s+LocalPlayer::setCell\(\).*const\s+bool\s+hasServerPosition\s*=\s*hasFinitePositionPacket\(\);.*if\s*\(cell\.isExterior\(\)\).*if\s*\(hasServerPosition\).*pos\s*=\s*position;.*else.*ESM::indexToPosition.*world->changeToCell\(ESM::RefId::esm3ExteriorCell\(x,\s*y\),\s*pos,\s*true\);.*if\s*\(!hasServerPosition\)\s*world->fixPosition\(\);.*else\s+if\s*\(ESM::RefId\s+exteriorCellId\s*=\s*world->findExteriorPosition\(cell\.mName,\s*pos\);.*if\s*\(hasServerPosition\)\s*pos\s*=\s*position;.*if\s*\(!hasServerPosition\)\s*world->fixPosition\(\);.*if\s*\(!hasServerPosition\s*&&\s*world->findInteriorPosition\(cell\.mName,\s*pos\)\.empty\(\)\).*if\s*\(hasServerPosition\)\s*pos\s*=\s*position;.*updateCell\(true,\s*false\);.*function\s+BasePlayer:SendLocation\(location,\s*options\).*local\s+pendingLocationChange\s*=\s*self:BeginServerLocationChange\(options\.reason\s+or\s+"sendLocation",\s*location\.cellDescription,\s*options\).*tes3mp\.SetCell\(self\.pid,\s*location\.cellDescription\).*setCellChangeReason\(self\.pid,\s*pendingLocationChange\.cellChangeReason\).*tes3mp\.SetPos\(self\.pid,\s*location\.position\[1\],\s*location\.position\[2\],\s*location\.position\[3\]\).*tes3mp\.SetRot\(self\.pid,\s*location\.rotation\[1\],\s*location\.rotation\[2\]\).*tes3mp\.SendCell\(self\.pid\).*tes3mp\.SendPos\(self\.pid\)' `
    -Missing $missing

Test-Pattern -Name "Server-authored cell changes carry typed transition reasons" -Text ($cellFunctionsHeader + "`n" + $cellFunctions + "`n" + $playerBase + "`n" + $logicHandler + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern '.' `
    -Missing $missing

Test-Pattern -Name "Server location acknowledgements accept exterior coordinate aliases" -Text ($playerBase + "`n" + $serverLuaCompat) `
    -Pattern 'local\s+function\s+getExteriorCellGrid\(cellDescription\).*string\.find\(cellDescription,\s*"\^%s\*\(%-\?%d\+\),%s\*\(%-\?%d\+\)%s\*\$"\).*string\.find\(cellDescription,\s*"%\(\(%-\?%d\+\),%s\*\(%-\?%d\+\)%\)%s\*\$"\).*local\s+function\s+cellDescriptionsReferToSameLocation\(leftDescription,\s*rightDescription\).*leftDescription\s*==\s*rightDescription.*leftGridX\s*==\s*rightGridX\s+and\s+leftGridY\s*==\s*rightGridY.*function\s+BasePlayer:ConsumeServerLocationChange\(cellDescription\).*not\s+cellDescriptionsReferToSameLocation\(pendingLocationChange\.cell,\s*cellDescription\).*treating packet as a normal client cell change.*PlayerBaseConsumesServerLocationChangeForExteriorAliases' `
    -Missing $missing

Test-Pattern -Name "Client door and portal cell changes carry server-visible transition reasons" -Text ($playerBase + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*clientLocationChangeReasonTimeout\s*=\s*10)(?=.*pendingClientLocationChange\s*=\s*nil)(?=.*function\s+BasePlayer:BeginClientLocationChange\(reason,\s*sourceCellDescription,\s*options\).*reason\s*=\s*reason\s*or\s*"client".*sourceCell\s*=\s*sourceCellDescription.*objectRefId\s*=\s*options\.objectRefId.*objectUniqueIndex\s*=\s*options\.objectUniqueIndex.*timestamp\s*=\s*os\.time\(\))(?=.*function\s+BasePlayer:ConsumeClientLocationChange\(cellDescription,\s*previousCellDescription\).*expired client location change reason.*pendingLocationChange\.sourceCell.*not\s+cellDescriptionsReferToSameLocation\(pendingLocationChange\.sourceCell,\s*previousCellDescription\).*destinationCell\s*=\s*cellDescription)(?=.*eventHandler\.OnPlayerCellChange.*ConsumeClientLocationChange\(.*currentCellDescription,\s*previousCellDescription\).*playerPacket\.location\.reason\s*=\s*pendingClientLocationChange\.reason.*sent unreasoned discontinuous PlayerCellChange)(?=.*eventHandler\.OnGenericObjectEvent.*packetType\s*==\s*"ObjectActivate".*local\s+locationChangeOptions\s*=\s*\{.*objectRefId\s*=\s*objectRefId.*objectUniqueIndex\s*=\s*objectUniqueIndex.*BeginClientLocationChange\("objectActivate",\s*cellDescription,\s*locationChangeOptions\))(?=.*PlayerBaseConsumesClientLocationChangeReasons)' `
    -Missing $missing

Test-Pattern -Name "Server-authentic door destinations constrain client cell transitions" -Text ($serverObjectProcessorInitializer + "`n" + $serverDoorDestinationProcessor + "`n" + $objectFunctions + "`n" + $objectFunctionsHeader + "`n" + $scriptFunctions + "`n" + $serverCore + "`n" + $packetReaderLua + "`n" + $packetBuilderLua + "`n" + $cellBaseLua + "`n" + $playerBase + "`n" + $eventHandler + "`n" + $serverLuaCompat) `
    -Pattern '(?=.*ProcessorDoorDestination.*BPP_INIT\(ID_DOOR_DESTINATION\).*OnDoorDestination)(?=.*ObjectProcessor::AddProcessor\(new\s+ProcessorDoorDestination\(\)\))(?=.*GetObjectDoorTeleportState.*GetObjectDoorDestinationCell.*GetObjectDoorDestinationPosX.*GetObjectDoorDestinationRotZ)(?=.*packetType\s*==\s*"DoorDestination".*object\.teleportState\s*=\s*tes3mp\.GetObjectDoorTeleportState.*object\.doorDestination\s*=\s*\{.*cell\s*=\s*tes3mp\.GetObjectDoorDestinationCell)(?=.*packetBuilder\.AddDoorDestination.*hasTeleportDestination.*SetObjectDoorTeleportState\(hasTeleportDestination\))(?=.*function\s+BaseCell:SaveDoorDestinations.*hasTeleportDestination.*self\.data\.objectData\[uniqueIndex\]\.teleportState\s*=\s*hasTeleportDestination.*packets\.doorDestination)(?=.*function\s+BaseCell:LoadDoorDestinations.*packetBuilder\.AddDoorDestination.*tes3mp\.SendDoorDestination)(?=.*function\s+BasePlayer:BeginClientLocationChange.*expectedCell\s*=\s*options\.expectedCell.*expectedPosition\s*=\s*tableHelper\.deepCopy\(options\.expectedPosition\))(?=.*function\s+BasePlayer:ConsumeClientLocationChange.*pendingLocationChange\.expectedCell.*rejectionReason\s*=\s*"expectedDestinationMismatch".*rejecting client cell change)(?=.*getDoorDestinationForObject.*objectData\.teleportState.*objectData\.doorDestination)(?=.*eventHandler\.OnGenericObjectEvent.*packetType\s*==\s*"ObjectActivate".*expectedCell\s*=\s*doorDestination\.cell.*BeginClientLocationChange\("objectActivate",\s*cellDescription,\s*locationChangeOptions\))(?=.*sendAcceptedPlayerLocationCorrection.*player:SendLocation.*rejectClientDoorDestination)(?=.*function\s+OnDoorDestination\(pid,\s*cellDescription\))(?=.*PacketBuilderKeepsDoorDestinationFacadeCalls)(?=.*PacketReaderKeepsDoorDestinationTableShape)(?=.*CellBaseSavesAndLoadsDoorDestinations)(?=.*EventHandlerRejectsDoorDestinationCellMismatch)' `
    -Missing $missing

Test-Pattern -Name "Server cell-change exchanges use reliable position delivery" -Text $serverPlayerCellChangeProcessor `
    -Pattern 'GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(other\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*pl->guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(pl\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*other->guid,\s*PacketReliability::ReliableOrdered\);.*GetPacket\(ID_PLAYER_POSITION\)->setPlayer\(&player\);.*GetPacket\(ID_PLAYER_POSITION\)->SendWithReliability\(\s*true,\s*PacketReliability::ReliableOrdered\);' `
    -Missing $missing

Test-Pattern -Name "Player cell-change packets preserve reliable movement payload" -Text ($playerCellChangePacket + "`n" + $basePacketTest) `
    -Pattern 'PacketPlayerCellChange::PacketPlayerCellChange\(\)\s*:\s*PlayerPacket\(\).*packetID\s*=\s*ID_PLAYER_CELL_CHANGE;.*priority\s*=\s*PacketPriority::Immediate;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_MOVEMENT;.*void\s+mwmp::PacketPlayerCellChange::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*PlayerPacket::Packet\(newBitstream,\s*send\);.*RW\(player->cell\.mData,\s*send,\s*true\);.*RW\(player->cell\.mName,\s*send,\s*true\);.*RW\(player->positionSequence,\s*send\);.*RW\(player->position,\s*send,\s*1\);.*RW\(player->direction,\s*send,\s*1\);.*!player->hasFinitePositionPacket\(\).*packetValid\s*=\s*false;.*RW\(player->previousCellPosition\.pos,\s*send,\s*true\);.*RW\(player->isChangingRegion,\s*send\);.*if\s*\(player->isChangingRegion\).*RW\(player->cell\.mRegion,\s*send,\s*true\);.*playerCellChangeRoundTripsCellAndPreviousPosition.*playerCellChangeRejectsNonFiniteMovementSnapshot.*playerCellChangeUsesImmediateReliableMovementDelivery.*playerCellAndStatsPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Local player animation flags preserve scripted and airborne jump state" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::updateAnimFlags\(bool\s+forceUpdate\).*bool\s+isForceJumping\s*=\s*ptrNpcStats\.getMovementFlag\(CreatureStats::Flag_ForceJump\);.*isJumping\s*=\s*!world->isOnGround\(ptrPlayer\)\s*&&\s*!isFlying;.*movementFlags\s*=\s*0;.*movementFlags\s*=\s*__SETFLAG\(CreatureStats::Flag_Sneak,\s*isSneaking\);.*movementFlags\s*=\s*__SETFLAG\(CreatureStats::Flag_Run,\s*isRunning\);.*movementFlags\s*=\s*__SETFLAG\(CreatureStats::Flag_ForceJump,\s*isForceJumping\s*\|\|\s*isJumping\);.*movementFlags\s*=\s*__SETFLAG\(CreatureStats::Flag_ForceMoveJump,\s*isForceMoveJumping\);.*if\s*\(isJumping\)\s*updatePosition\(true\);.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_ANIM_FLAGS\)->setPlayer\(this\);.*getNetworking\(\)->getPlayerPacket\(ID_PLAYER_ANIM_FLAGS\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Remote player position packets snap once before falling back to interpolation" -Text ($dedicatedPlayerHeader + "`n" + $dedicatedPlayer + "`n" + $playerPositionProcessor) `
    -Pattern '(?=.*bool\s+hasReceivedInitialPosition;)(?=.*bool\s+hasChangedCell;)(?=.*DedicatedPlayer::DedicatedPlayer\(PacketGuid\s+guid\).*hasReceivedInitialPosition\s*=\s*false;.*hasChangedCell\s*=\s*true;)(?=.*BPP_INIT\(ID_PLAYER_POSITION\).*else\s+if\s*\(player\s*!=\s*0\).*static_cast<DedicatedPlayer\*>\(player\)->readPositionPacket\(\);)(?=.*bool\s+DedicatedPlayer::readPositionPacket\(\).*updateMarker\(\);.*if\s*\(!reference\).*return\s+false;.*if\s*\(!hasReceivedInitialPosition\).*hasReceivedInitialPosition\s*=\s*true;.*setPosition\(\);.*hasChangedCell\s*=\s*false;.*return\s+true;)' `
    -Missing $missing

Test-Pattern -Name "Remote player movement state is initialized before first packet replay" -Text $dedicatedPlayer `
    -Pattern 'DedicatedPlayer::DedicatedPlayer\(PacketGuid\s+guid\).*drawState\s*=\s*static_cast<char>\(MWMechanics::DrawState::Nothing\);.*movementFlags\s*=\s*0;.*animation\.groupname\s*=\s*"";.*sound\s*=\s*"";.*previousDisplayCreatureName\s*=\s*false;.*hasChangedCell\s*=\s*true;.*markerEnabled\s*=\s*false;.*isLevitationPurged\s*=\s*false;.*isJumping\s*=\s*false;.*wasJumping\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Remote player update waits for a created reference and first position" -Text $dedicatedPlayer `
    -Pattern 'void\s+DedicatedPlayer::update\(float\s+dt\).*if\s*\(!reference\)\s*return;.*if\s*\(hasReceivedInitialPosition\)\s*\{.*move\(dt\);.*\}.*setAnimFlags\(\);.*void\s+DedicatedPlayer::setAnimFlags\(\).*if\s*\(!reference\)\s*return;.*if\s*\(!isFlying\s*&&\s*!hasTcl\s*&&\s*!isLevitationPurged\)' `
    -Missing $missing

Test-Pattern -Name "Dedicated players replay serialized player jumping through ForceJump animation state" -Text ($basePlayer + "`n" + $dedicatedPlayer) `
    -Pattern 'class\s+BasePlayer.*bool\s+isJumping\s*=\s*false;.*void\s+DedicatedPlayer::setAnimFlags\(\).*ptrCreatureStats->setMovementFlag\(CreatureStats::Flag_ForceJump,\s*isJumping\s*\|\|\s*\(movementFlags\s*&\s*CreatureStats::Flag_ForceJump\)\s*!=\s*0\);' `
    -Missing $missing

Test-Pattern -Name "Remote player side-state packets wait for a created reference before pointer replay" -Text ($dedicatedPlayerHeader + "`n" + $dedicatedPlayer + "`n" + $playerBountyProcessor + "`n" + $playerLevelProcessor + "`n" + $playerSpellsActiveProcessor) `
    -Pattern '(?=.*bool\s+hasPendingSpellsActiveChanges;)(?=.*DedicatedPlayer::DedicatedPlayer\(PacketGuid\s+guid\).*hasPendingSpellsActiveChanges\s*=\s*false;)(?=.*void\s+DedicatedPlayer::setBaseInfo\(\).*setShapeshift\(\);.*if\s*\(hasPendingSpellsActiveChanges\)\s*applySpellsActiveChanges\(\);)(?=.*void\s+DedicatedPlayer::setShapeshift\(\).*if\s*\(!reference\)\s*return;)(?=.*void\s+DedicatedPlayer::equipItem\(std::string\s+itemId,\s*bool\s+noSound\).*if\s*\(!reference\s*\|\|\s*!ptr\.getClass\(\)\.hasInventoryStore\(ptr\)\)\s*return;)(?=.*void\s+DedicatedPlayer::addSpellsActive\(\).*if\s*\(!reference\)\s*return;)(?=.*void\s+DedicatedPlayer::removeSpellsActive\(\).*if\s*\(!reference\)\s*return;)(?=.*void\s+DedicatedPlayer::setSpellsActive\(\).*if\s*\(!reference\)\s*return;)(?=.*void\s+DedicatedPlayer::applySpellsActiveChanges\(\).*if\s*\(!reference\).*hasPendingSpellsActiveChanges\s*=\s*true;.*return;.*hasPendingSpellsActiveChanges\s*=\s*false;.*if\s*\(spellsActiveAction\s*==\s*SpellsActiveChanges::ADD\).*addSpellsActive\(\);.*else\s+if\s*\(spellsActiveAction\s*==\s*SpellsActiveChanges::REMOVE\).*removeSpellsActive\(\);.*else\s*setSpellsActive\(\);)(?=.*class\s+ProcessorPlayerSpellsActive.*dedicatedPlayer\.applySpellsActiveChanges\(\);)(?=.*class\s+ProcessorPlayerBounty.*if\s*\(!dedicatedPlayer\.hasReference\(\)\)\s*return;.*if\s*\(ptrPlayer\.get<ESM::NPC>\(\)\s*==\s*nullptr\)\s*return;)(?=.*class\s+ProcessorPlayerLevel.*if\s*\(!dedicatedPlayer\.hasReference\(\)\)\s*return;)' `
    -Missing $missing

Test-Pattern -Name "Remote player GUID lookups do not insert null player entries" -Text $playerList `
    -Pattern '(?=.*DedicatedPlayer\s*\*\s*PlayerList::getPlayer\(PacketGuid\s+guid\)\s*\{.*auto\s+player\s*=\s*playerList\.find\(guid\);.*if\s*\(player\s*==\s*playerList\.end\(\)\)\s*return\s+nullptr;.*return\s+player->second;)(?=.*void\s+PlayerList::deletePlayer\(PacketGuid\s+guid\).*auto\s+player\s*=\s*playerList\.find\(guid\);.*if\s*\(player\s*==\s*playerList\.end\(\)\)\s*return;.*if\s*\(player->second\s*!=\s*nullptr\s*&&\s*player->second->reference\).*playerList\.erase\(player\);)(?=.*std::vector<PacketGuid>\s+PlayerList::getPlayersInCell.*if\s*\(playerEntry\.second\s*==\s*nullptr\)\s*continue;)' `
    -Missing $missing

Test-Pattern -Name "Remote player pointer lookups use live reference identity" -Text $playerList `
    -Pattern 'DedicatedPlayer\*\s+PlayerList::getPlayer\(const\s+MWWorld::Ptr&\s+ptr\).*if\s*\(ptr\.mRef\s*==\s*nullptr\)\s*return\s+nullptr;.*if\s*\(playerEntry\.second\s*==\s*nullptr\s*\|\|\s*playerEntry\.second->getPtr\(\)\.mRef\s*==\s*nullptr\)\s*continue;.*if\s*\(playerEntry\.second->getPtr\(\)\s*==\s*ptr\)\s*return\s+playerEntry\.second;' `
    -Missing $missing

Test-Pattern -Name "Dedicated player snap and interpolation share finite movement settings and rotation" -Text $dedicatedPlayer `
    -Pattern '(?=.*void\s+DedicatedPlayer::move\(float\s+dt\).*if\s*\(!reference\)\s*return;.*const\s+ESM::Position\s+previousVisualPosition\s*=.*if\s*\(shouldInterpolate\s*&&\s*!hasChangedCell\).*getLinearInterpolation.*world->moveObject\(ptr,\s*lerp\);.*world->rotateObject\(ptr,\s*osg::Vec3f\(position\.rot\[0\],\s*0,\s*position\.rot\[2\]\)\);.*setMovementSettingsFromVisualDelta\(previousVisualPosition\);.*else\s*\{.*setPosition\(\);.*hasChangedCell\s*=\s*false;.*\})(?=.*void\s+DedicatedPlayer::setPosition\(\).*world->moveObject\(ptr,\s*position\.asVec3\(\)\);.*world->rotateObject\(ptr,\s*osg::Vec3f\(position\.rot\[0\],\s*0,\s*position\.rot\[2\]\)\);.*setMovementSettings\(\);)(?=.*void\s+DedicatedPlayer::setMovementSettings\(\).*setMovementSettings\(direction\);)(?=.*void\s+DedicatedPlayer::setMovementSettings\(const\s+ESM::Position&\s+movementDirection\).*for\s*\(int\s+i\s*=\s*0;\s*i\s*<\s*3;\s*\+\+i\)\s*move->mPosition\[i\]\s*=\s*std::isfinite\(movementDirection\.pos\[i\]\)\s*\?\s*movementDirection\.pos\[i\]\s*:\s*0\.f;.*if\s*\(std::isfinite\(movementDirection\.rot\[0\]\).*std::isfinite\(movementDirection\.rot\[1\]\).*std::isfinite\(movementDirection\.rot\[2\]\).*move->mRotation\[0\]\s*=\s*movementDirection\.rot\[0\];.*move->mRotation\[1\]\s*=\s*movementDirection\.rot\[1\];.*move->mRotation\[2\]\s*=\s*movementDirection\.rot\[2\];).*' `
    -Missing $missing

Test-Pattern -Name "Remote movement interpolation clamps to authoritative targets without integer distance checks" -Text ($mechanicsHelper + "`n" + $dedicatedPlayer + "`n" + $dedicatedActor) `
    -Pattern '#include\s+<algorithm>.*getLinearInterpolation\(osg::Vec3f\s+start,\s*osg::Vec3f\s+end,\s*float\s+percent\).*std::clamp\(percent,\s*0\.f,\s*1\.f\).*osg::Vec3f\s+position\(clampedPercent,\s*clampedPercent,\s*clampedPercent\).*DedicatedPlayer::move\(float\s+dt\).*constexpr\s+float\s+maxInterpolationDistance\s*=\s*512\.f;.*std::abs\(position\.pos\[0\]\s*-\s*refPos\.pos\[0\]\).*std::abs\(position\.pos\[1\]\s*-\s*refPos\.pos\[1\]\).*std::abs\(position\.pos\[2\]\s*-\s*refPos\.pos\[2\]\).*DedicatedActor::move\(float\s+dt\).*constexpr\s+float\s+maxInterpolationDistance\s*=\s*512\.f;.*std::abs\(position\.pos\[0\]\s*-\s*refPos\.pos\[0\]\).*std::abs\(position\.pos\[1\]\s*-\s*refPos\.pos\[1\]\).*std::abs\(position\.pos\[2\]\s*-\s*refPos\.pos\[2\]\)' `
    -Missing $missing

Test-Pattern -Name "Remote movement packets apply capped low-latency catch-up" -Text ($mechanicsHelper + "`n" + $dedicatedPlayer + "`n" + $dedicatedActor + "`n" + $clientCell) `
    -Pattern 'float\s+MechanicsHelper::getRemoteMovementInterpolationFactor\(float\s+dt\).*constexpr\s+float\s+targetCatchupWindow\s*=\s*0\.12f;.*constexpr\s+float\s+maxFrameCatchup\s*=\s*0\.35f;.*std::clamp\(dt\s*/\s*targetCatchupWindow,\s*0\.f,\s*maxFrameCatchup\).*void\s+DedicatedPlayer::move\(float\s+dt\).*getRemoteMovementInterpolationFactor\(dt\).*bool\s+DedicatedPlayer::readPositionPacket\(\).*constexpr\s+float\s+immediateReplayStep\s*=\s*0\.015f;.*move\(immediateReplayStep\);.*return\s+true;.*void\s+DedicatedActor::move\(float\s+dt\).*getRemoteMovementInterpolationFactor\(dt\).*bool\s+applySequencedPosition\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*constexpr\s+float\s+immediateReplayStep\s*=\s*0\.015f;.*actor\.move\(immediateReplayStep\);.*return\s+true;.*void\s+Cell::readPositions\(ActorList&\s+actorList\).*applySequencedPosition\(\*actor,\s*baseActor\);.*void\s+Cell::readAnimFlags\(ActorList&\s+actorList\).*actor->setAnimFlags\(\);' `
    -Missing $missing

Test-Pattern -Name "Remote jump animation flags also provide vertical movement cues" -Text ($dedicatedPlayerHeader + "`n" + $dedicatedPlayer + "`n" + $dedicatedActorHeader + "`n" + $dedicatedActor) `
    -Pattern '(?=.*void\s+applyRemoteJumpMovementCue\(bool\s+wasRemoteJumping\);)(?=.*void\s+DedicatedPlayer::applyRemoteJumpMovementCue\(bool\s+wasRemoteJumping\).*const\s+bool\s+wantsJump\s*=\s*isJumping\s*\|\|.*CreatureStats::Flag_ForceJump.*move->mPosition\[2\]\s*=\s*1\.f;.*else\s+if\s*\(wasRemoteJumping.*!isFlying.*!hasTcl.*move->mPosition\[2\]\s*=\s*0\.f;)(?=.*void\s+DedicatedPlayer::setAnimFlags\(\).*const\s+bool\s+wasRemoteJumping\s*=\s*wasJumping;.*ptrCreatureStats->setMovementFlag\(CreatureStats::Flag_ForceJump,.*applyRemoteJumpMovementCue\(wasRemoteJumping\);)(?=.*bool\s+wasJumping;.*void\s+DedicatedActor::applyRemoteJumpMovementCue\(bool\s+wasRemoteJumping\).*const\s+bool\s+wantsJump\s*=\s*isJumping\s*\|\|.*CreatureStats::Flag_ForceJump.*move->mPosition\[2\]\s*=\s*1\.f;.*else\s+if\s*\(wasRemoteJumping.*!isFlying.*move->mPosition\[2\]\s*=\s*0\.f;)(?=.*void\s+DedicatedActor::setAnimFlags\(\).*const\s+bool\s+wasRemoteJumping\s*=\s*wasJumping;.*wasJumping\s*=\s*isJumping;.*ptrCreatureStats->setMovementFlag\(CreatureStats::Flag_ForceJump,.*applyRemoteJumpMovementCue\(wasRemoteJumping\);)' `
    -Missing $missing

Test-Pattern -Name "Dedicated multiplayer proxies use movement input only for animation" -Text $characterController `
    -Pattern 'bool\s+isDedicatedMultiplayerProxy\(const\s+MWWorld::Ptr&\s+ptr\).*mwmp::PlayerList::isDedicatedPlayer\(ptr\).*mwmp::Main::get\(\)\.getCellController\(\)->isDedicatedActor\(ptr\).*const\s+bool\s+dedicatedMultiplayerProxy\s*=\s*isDedicatedMultiplayerProxy\(mPtr\);.*const\s+bool\s+forceNetworkJumpAnimation\s*=\s*dedicatedMultiplayerProxy.*stats\.getMovementFlag\(MWMechanics::CreatureStats::Flag_ForceJump\).*if\s*\(forceNetworkJumpAnimation\).*mInJump\s*=\s*true;.*jumpstate\s*=\s*JumpState_InAir;.*vec\.z\(\)\s*=\s*0\.f;.*if\s*\(movestate\s*!=\s*CharState_None\).*clearAnimQueue\(\);.*if\s*\(!forceNetworkJumpAnimation\)\s*jumpstate\s*=\s*JumpState_None;.*if\s*\(!dedicatedMultiplayerProxy\s*&&\s*!isKnockedDown\(\)\s*&&\s*!isKnockedOut\(\)\).*world->rotateObject\(mPtr,\s*rot,\s*true\);.*else\s+if\s*\(!dedicatedMultiplayerProxy\).*world->rotateObject\(mPtr,\s*rot,\s*true\);.*if\s*\(dedicatedMultiplayerProxy\)\s*movement\s*=\s*osg::Vec3f\(\);.*world->queueMovement\(mPtr,\s*movement\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated player cell changes preserve immediate movement settings and rotation" -Text $dedicatedPlayer `
    -Pattern 'void\s+DedicatedPlayer::setCell\(\).*setStatsDynamic\(\);.*setAnimFlags\(\);.*setPtr\(world->moveObject\(ptr,\s*cellStore,\s*position\.asVec3\(\)\)\);.*setMovementSettings\(\);.*world->rotateObject\(ptr,\s*osg::Vec3f\(position\.rot\[0\],\s*0,\s*position\.rot\[2\]\)\);.*hasChangedCell\s*=\s*true;.*hasFinishedInitialTeleportation\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "Dedicated player and actor keep clamped interpolation on long frames" -Text ($dedicatedPlayer + "`n" + $dedicatedActor) `
    -Pattern 'void\s+DedicatedPlayer::update\(float\s+dt\).*if\s*\(hasReceivedInitialPosition\)\s*\{.*move\(dt\);.*\}.*setAnimFlags\(\);.*void\s+DedicatedActor::update\(float\s+dt\).*if\s*\(hasPositionData\)\s*\{.*move\(dt\);.*\}.*if\s*\(hasAnimFlagsData\)\s*setAnimFlags\(\);' `
    -Missing $missing

Write-Host "TES3MP player movement sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 48"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
