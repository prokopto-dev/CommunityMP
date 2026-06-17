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

$containerWindow = Get-SourceText "apps\openmw\mwgui\container.cpp"
$inventoryWindow = Get-SourceText "apps\openmw\mwgui\inventorywindow.cpp"
$actionTake = Get-SourceText "apps\openmw\mwworld\actiontake.cpp"
$actionHarvest = Get-SourceText "apps\openmw\mwworld\actionharvest.cpp"
$containerProcessor = Get-SourceText "apps\openmw\mwmp\processors\object\ProcessorContainer.hpp"
$serverContainerProcessor = Get-SourceText "apps\openmw-mp\processors\object\ProcessorContainer.hpp"
$packetContainer = Get-SourceText "components\openmw-mp\Packets\Object\PacketContainer.cpp"
$packetContainerHeader = Get-SourceText "components\openmw-mp\Packets\Object\PacketContainer.hpp"
$packetObjectActivate = Get-SourceText "components\openmw-mp\Packets\Object\PacketObjectActivate.cpp"
$packetConsoleCommand = Get-SourceText "components\openmw-mp\Packets\Object\PacketConsoleCommand.cpp"
$packetObjectHit = Get-SourceText "components\openmw-mp\Packets\Object\PacketObjectHit.cpp"
$packetObjectSound = Get-SourceText "components\openmw-mp\Packets\Object\PacketObjectSound.cpp"
$objectFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Objects.cpp"
$objectProcessorHeader = Get-SourceText "apps\openmw-mp\processors\ObjectProcessor.hpp"
$objectProcessor = Get-SourceText "apps\openmw-mp\processors\ObjectProcessor.cpp"
$processorDoorState = Get-SourceText "apps\openmw-mp\processors\object\ProcessorDoorState.hpp"
$objectPacketHeader = Get-SourceText "components\openmw-mp\Packets\Object\ObjectPacket.hpp"
$objectPacket = Get-SourceText "components\openmw-mp\Packets\Object\ObjectPacket.cpp"
$objectList = Get-SourceText "apps\openmw\mwmp\ObjectList.cpp"
$playerInventoryProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerInventory.hpp"
$objectBindings = Get-SourceText "apps\openmw\mwlua\objectbindings.cpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$main = Get-SourceText "apps\openmw\mwmp\Main.hpp"
$worldImp = Get-SourceText "apps\openmw\mwworld\worldimp.cpp"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$baseCell = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$configScript = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$basePacketTests = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$componentTests = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"
$tradeWindow = Get-SourceText "apps\openmw\mwgui\tradewindow.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "OpenMW active cells are translated into TES3MP cell-state cells" -Text $worldImp `
    -Pattern 'ESM::Cell\s+makeTes3mpCellStateCell\(const\s+MWWorld::Cell&\s+cell\).*cell\.isExterior\(\).*stateCell\.mData\.mX\s*=\s*cell\.getGridX\(\);.*stateCell\.mData\.mY\s*=\s*cell\.getGridY\(\);.*stateCell\.mData\.mFlags\s*=\s*ESM::Cell::Interior;.*stateCell\.mName\s*=\s*std::string\(cell\.getNameId\(\)\);.*stateCell\.updateId\(\);' `
    -Missing $missing

Test-Pattern -Name "Logged-in clients publish active cell LOAD and UNLOAD deltas" -Text $worldImp `
    -Pattern 'void\s+syncTes3mpActiveCellStates\(const\s+Scene::CellStoreCollection&\s+activeCells\).*mwmp::Main::get\(\)\.hasSentInitialPlayerPackets\(\).*localPlayer\s*==\s*nullptr\s*\|\|\s*!localPlayer->isLoggedIn\(\).*reportedActiveCells\.clear\(\);.*makeTes3mpCellStateCell\(\*cellStore->getCell\(\)\).*mwmp::CellState::UNLOAD.*mwmp::CellState::LOAD.*localPlayer->sendCellStates\(\);\s*localPlayer->clearCellStates\(\);' `
    -Missing $missing

Test-Pattern -Name "World update syncs TES3MP active cells after scene updates" -Text $worldImp `
    -Pattern 'mWorldScene->update\(duration\);\s*#ifdef BUILD_TES3MP_CLIENT\s*syncTes3mpActiveCellStates\(mWorldScene->getActiveCells\(\)\);\s*#endif\s*mRendering->update\(duration,\s*paused\);' `
    -Missing $missing

Test-Pattern -Name "Cell-state sync waits for the initial player loaded packet pair" -Text $main `
    -Pattern 'bool\s+hasSentInitialPlayerPackets\(\)\s+const\s*\{\s*return\s+mInitialPlayerPacketsSent;\s*\}' `
    -Missing $missing

Test-Pattern -Name "LocalPlayer sends one-shot ID_PLAYER_CELL_STATE packets" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendCellStates\(\)\s*\{.*ID_PLAYER_CELL_STATE.*setPlayer\(this\);.*Send\(\);.*void\s+LocalPlayer::clearCellStates\(\)\s*\{\s*cellStateChanges\.clear\(\);' `
    -Missing $missing

Test-Pattern -Name "GUI container changes are gameplay Container packets for the container cell" -Text $containerWindow `
    -Pattern 'void\s+sendContainerChange\(const\s+MWWorld::Ptr&\s+container,.*objectList->packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;.*objectList->cell\s*=\s*makePacketCell\(container\);.*objectList->action\s*=\s*action;.*objectList->containerSubAction\s*=\s*subAction;.*objectList->sendContainer\(\);' `
    -Missing $missing

Test-Pattern -Name "Client object packets use TES3MP exterior grid cells" -Text $objectList `
    -Pattern 'ESM::Cell\s+makePacketCell\(const\s+MWWorld::Cell&\s+cell\).*cell\.isExterior\(\).*packetCell\.mData\.mX\s*=\s*cell\.getGridX\(\);.*packetCell\.mData\.mY\s*=\s*cell\.getGridY\(\);.*packetCell\.mData\.mFlags\s*=\s*ESM::Cell::Interior;.*packetCell\.mName\s*=\s*std::string\(cell\.getNameId\(\)\);.*packetCell\.updateId\(\);.*ObjectList::addObjectHit\(.*cell\s*=\s*makePacketCell\(\*ptr\.getCell\(\)->getCell\(\)\);.*ObjectList::addObjectPlace\(.*cell\s*=\s*makePacketCell\(\*ptr\.getCell\(\)->getCell\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Client replies to container requests with TES3MP exterior grid cells" -Text $containerProcessor `
    -Pattern 'ESM::Cell\s+makeContainerPacketCell\(const\s+MWWorld::Cell&\s+cell\).*cell\.isExterior\(\).*packetCell\.mData\.mX\s*=\s*cell\.getGridX\(\);.*packetCell\.mData\.mY\s*=\s*cell\.getGridY\(\);.*objectList\.cell\s*=\s*makeContainerPacketCell\(\*ptrCellStore->getCell\(\)\);.*objectList\.containerSubAction\s*=\s*mwmp::BaseObjectList::REPLY_TO_REQUEST;.*objectList\.sendContainer\(\);' `
    -Missing $missing

Test-Pattern -Name "OpenMW Lua container packets use TES3MP exterior grid cells" -Text $objectBindings `
    -Pattern 'ESM::Cell\s+makeTes3mpLuaPacketCell\(const\s+MWWorld::Cell&\s+cell\).*cell\.isExterior\(\).*packetCell\.mData\.mX\s*=\s*cell\.getGridX\(\);.*packetCell\.mData\.mY\s*=\s*cell\.getGridY\(\);.*sendTes3mpLuaContainerAddPacket\(.*objectList->cell\s*=\s*makeTes3mpLuaPacketCell\(\*container\.getCell\(\)->getCell\(\)\);.*sendTes3mpLuaContainerRemovePacket\(.*objectList->cell\s*=\s*makeTes3mpLuaPacketCell\(\*container\.getCell\(\)->getCell\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GUI drag and direct transfer remove items through server echo" -Text $containerWindow `
    -Pattern 'void\s+ContainerWindow::dragItem\(.*sendContainerChange\(mPtr,\s*item,\s*item\.mCount,\s*count,\s*mwmp::BaseObjectList::REMOVE,\s*mwmp::BaseObjectList::DRAG\);.*void\s+ContainerWindow::transferItem\(.*sendContainerChange\(mPtr,\s*item,\s*item\.mCount,\s*count,\s*mwmp::BaseObjectList::REMOVE,\s*mwmp::BaseObjectList::DRAG\);' `
    -Missing $missing

Test-Pattern -Name "GUI take-all removes every stack through a TAKE_ALL Container packet" -Text $containerWindow `
    -Pattern 'void\s+ContainerWindow::onTakeAllButtonClicked\(.*objectList->packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;.*objectList->action\s*=\s*mwmp::BaseObjectList::REMOVE;.*objectList->containerSubAction\s*=\s*mwmp::BaseObjectList::TAKE_ALL;.*objectList->addContainerItem\(baseObject,\s*item,\s*static_cast<int>\(item\.mCount\),\s*static_cast<int>\(item\.mCount\)\);.*objectList->sendContainer\(\);' `
    -Missing $missing

Test-Pattern -Name "Harvest activation removes plant contents through gameplay Container packets" -Text $actionHarvest `
    -Pattern 'void\s+ActionHarvest::executeImp\(.*#ifdef\s+BUILD_TES3MP_CLIENT.*actor\s*!=\s*world->getPlayerPtr\(\).*return;.*objectList->packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;.*objectList->action\s*=\s*mwmp::BaseObjectList::REMOVE;.*objectList->containerSubAction\s*=\s*mwmp::BaseObjectList::TAKE_ALL;.*objectList->addContainerItem\(baseObject,\s*\*it,\s*itemCount,\s*itemCount\);.*objectList->sendContainer\(\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Loose world item pickups immediately sync inventory ADD and ObjectDelete" -Text ($actionTake + "`n" + $inventoryWindow) `
    -Pattern '(?=.*void\s+syncTes3mpWorldItemPickup\(.*localPlayer->sendItemChange\(inventoryObject,\s*count,\s*mwmp::InventoryChanges::ADD\);.*objectList->packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;.*objectList->addObjectGeneric\(worldObject\);.*objectList->sendObjectDelete\(\);)(?=.*void\s+ActionTake::executeImp\(.*MWWorld::Ptr\s+newitem\s*=\s*\*actor\.getClass\(\)\.getContainerStore\(actor\)\.add\(getTarget\(\),\s*count\);.*syncTes3mpWorldItemPickup\(getTarget\(\),\s*newitem,\s*count\);.*deleteObject\(getTarget\(\)\);)(?=.*void\s+InventoryWindow::pickUpObject\(MWWorld::Ptr\s+object\).*MWWorld::Ptr\s+newObject\s*=\s*\*player\.getClass\(\)\.getContainerStore\(player\)\.add\(object,\s*count\);.*syncTes3mpWorldItemPickup\(object,\s*newObject,\s*count\);.*deleteObject\(object\);)' `
    -Missing $missing

Test-Pattern -Name "Server requires loaded cells for gameplay Container packets" -Text $eventHandler `
    -Pattern 'local\s+requiresLoadedCell\s*=\s*logicHandler\.DoesPacketOriginRequireLoadedCell\(packetOrigin\).*not\s+config\.allowOnContainerForUnloadedCells\s+and\s+not\s+isCellLoaded\s+and\s+requiresLoadedCell.*Invalid Container:.*return' `
    -Missing $missing

Test-Pattern -Name "Server recovers gameplay Container packets that beat cell-state LOADs" -Text ($baseCell + "`n" + $logicHandler + "`n" + $eventHandler + "`n" + $componentTests) `
    -Pattern 'function\s+BaseCell:AddVisitor\(pid,\s*options\).*options\.skipInitialCellData\s*~=\s*true.*options\.skipContainerRequest\s*~=\s*true.*options\.skipActorListRequest\s*~=\s*true.*logicHandler\.LoadCellForPlayer\s*=\s*function\(pid,\s*cellDescription,\s*visitorOptions\).*local\s+cell\s*=\s*LoadedCells\[cellDescription\].*cell:AddVisitor\(pid,\s*visitorOptions\).*local\s+requiresLoadedCell\s*=\s*logicHandler\.DoesPacketOriginRequireLoadedCell\(packetOrigin\).*Recovering Container:.*logicHandler\.LoadCellForPlayer\(pid,\s*cellDescription,\s*\{.*skipInitialCellData\s*=\s*true.*skipContainerRequest\s*=\s*true.*skipActorListRequest\s*=\s*true.*CellBaseCanAddRecoveredVisitorsWithoutSnapshotRequests.*EventHandlerRecoversUnloadedGameplayContainerCells' `
    -Missing $missing

Test-Pattern -Name "Server accepts region-named exterior object packets for loaded grids" -Text ($eventHandler + "`n" + $componentTests) `
    -Pattern 'local\s+function\s+getExteriorCellGrid\(cellDescription\).*local\s+_,\s*_,\s*gridX,\s*gridY\s*=\s*string\.find\(cellDescription,.*return\s+tonumber\(gridX\),\s*tonumber\(gridY\).*local\s+function\s+getLoadedCellDescription\(cellDescription\).*LoadedCells\[cellDescription\]\s*~=\s*nil.*for\s+loadedCellDescription,\s*_\s+in\s+pairs\(LoadedCells\).*loadedGridX\s*==\s*gridX\s+and\s+loadedGridY\s*==\s*gridY.*eventHandler\.OnGenericObjectEvent.*local\s+canonicalCellDescription,\s*isCellLoaded\s*=\s*getLoadedCellDescription\(cellDescription\).*cellDescription\s*=\s*canonicalCellDescription.*eventHandler\.OnContainer.*local\s+canonicalCellDescription,\s*isCellLoaded\s*=\s*getLoadedCellDescription\(cellDescription\).*cellDescription\s*=\s*canonicalCellDescription.*EventHandlerAcceptsRegionNamedExteriorContainerPacketsForLoadedGrid' `
    -Missing $missing

Test-Pattern -Name "Accepted gameplay Container packets echo back to the sender" -Text $baseCell `
    -Pattern '(?=.*if\s+hasSharedContainerChanges\s+then.*subAction\s*==\s*enumerations\.containerSub\.REPLY_TO_REQUEST.*tes3mp\.SendContainer\(true,\s*true\))(?=.*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_SCRIPT_LOCAL.*tes3mp\.SendContainer\(true,\s*true\))(?=.*if\s+hasPlayerScopedContainerChanges\s+then.*tes3mp\.SendContainer\(false,\s*false\).*self:LoadContainers\(pid,\s*self\.data\.objectData,\s*sharedContainerUniqueIndexes,\s*\{.*sendToOtherPlayers\s*=\s*true.*skipAttachedPlayer\s*=\s*true.*\}\).*else\s+tes3mp\.SendContainer\(true,\s*false\))(?=.*elseif\s+hasPlayerScopedContainerChanges\s+and\s+subAction\s*~=\s*enumerations\.containerSub\.REPLY_TO_REQUEST\s+then.*else\s+tes3mp\.SendContainer\(false,\s*false\))' `
    -Missing $missing

Test-Pattern -Name "Player inventory consumes server-mirrored container transfer echoes" -Text $playerBase `
    -Pattern 'pendingContainerInventoryChangeTimeout\s*=\s*60.*function\s+BasePlayer:QueueContainerInventoryEcho\(inventoryAction,\s*item\).*pendingContainerInventoryChanges.*timestamp\s*=\s*os\.time\(\).*function\s+BasePlayer:ConsumeContainerInventoryEcho\(inventoryAction,\s*item\).*containerInventoryMirrorItemsMatch\(pendingItem,\s*mirrorItem\).*function\s+BasePlayer:SaveInventory\(playerPacket\).*ConsumeContainerInventoryEcho\(action,\s*item\).*pending gameplay container inventory echo' `
    -Missing $missing

Test-Pattern -Name "Player inventory mirrors accepted gameplay container transfers into saved state" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:ApplyContainerInventoryMirror\(inventoryAction,\s*item\).*inventoryAction\s*==\s*enumerations\.inventory\.ADD.*inventoryHelper\.addItem\(self\.data\.inventory.*inventoryAction\s*==\s*enumerations\.inventory\.REMOVE.*inventoryHelper\.removeClosestItem\(self\.data\.inventory.*pruneEquipmentMissingInventory\(self\.data,\s*mirrorItem\.refId\).*QueueContainerInventoryEcho\(inventoryAction,\s*mirrorItem\).*quicksaveCharacterState\(self\)' `
    -Missing $missing

Test-Pattern -Name "Server container saves mirror accepted gameplay takes and drops atomically" -Text $baseCell `
    -Pattern 'isGameplayContainerTake\s*=\s*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_GAMEPLAY.*enumerations\.containerSub\.TAKE_ALL.*isGameplayContainerDrop\s*=\s*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_GAMEPLAY.*enumerations\.containerSub\.DROP.*function\s+rejectGameplayContainerDrop\(reason\).*hasRejectedGameplayContainerDrop\s*=\s*true.*shouldReloadPlayerInventory\s*=\s*true.*function\s+mirrorGameplayContainerTake\(itemRefId,\s*count,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*ApplyContainerInventoryMirror\(enumerations\.inventory\.ADD,\s*mirrorItem\).*function\s+getAcceptedGameplayContainerDrop\(itemRefId,\s*count,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*CanApplyContainerInventoryMirror\(enumerations\.inventory\.REMOVE,\s*mirrorItem\).*function\s+mirrorGameplayContainerDrop\(mirrorItem\).*ApplyContainerInventoryMirror\(enumerations\.inventory\.REMOVE,\s*mirrorItem\).*hasRejectedGameplayContainerDrop.*skipAttachedPlayer\s*=\s*false.*if\s+shouldReloadPlayerInventory.*Players\[pid\]:LoadInventory\(\).*Players\[pid\]:LoadEquipment\(\)' `
    -Missing $missing

Test-Pattern -Name "Barter commits use dialogue container deltas mirrored into saved player inventory" -Text ($tradeWindow + "`n" + $baseCell + "`n" + $componentTests + "`n" + $packetContainer) `
    -Pattern 'sendTes3mpBarterContainerDelta\(.*packetOrigin\s*=\s*mwmp::CLIENT_DIALOGUE.*containerSubAction\s*=\s*mwmp::BaseObjectList::BARTER.*sendContainer\(\).*syncedPlayerBought.*syncedMerchantBought.*sendTes3mpBarterContainerDelta\(mPtr,\s*mwmp::BaseObjectList::REMOVE,\s*syncedPlayerBought\).*sendTes3mpBarterContainerDelta\(mPtr,\s*mwmp::BaseObjectList::ADD,\s*syncedMerchantBought\).*isDialogueBarterContainerTransfer\s*=\s*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_DIALOGUE.*subAction\s*==\s*enumerations\.containerSub\.BARTER.*isGameplayContainerTake.*isDialogueBarterContainerTransfer\s+and\s+action\s*==\s*enumerations\.container\.REMOVE.*isGameplayContainerDrop.*isDialogueBarterContainerTransfer\s+and\s+action\s*==\s*enumerations\.container\.ADD.*CellBaseDialogueBarterContainersMirrorPlayerInventory' `
    -Missing $missing

Test-Pattern -Name "Failed dialogue barter transactions do not replay unsafe inventory or merchant gold updates" -Text ($baseCell + "`n" + $eventHandler + "`n" + $playerInventoryProcessor + "`n" + $componentTests) `
    -Pattern 'failedDialogueBarterTransactionTtlSeconds\s*=\s*30.*function\s+rememberFailedDialogueBarterTransaction\(reason\).*failedDialogueBarterTransaction\s*=\s*\{.*function\s+prevalidateDialogueBarterDrop\(\).*action\s*~=\s*enumerations\.container\.ADD.*CanApplyContainerInventoryMirror\(.*enumerations\.inventory\.REMOVE.*return\s+false.*failedDialogueBarterTransactionMatches\(pid,\s*cellDescription,\s*objects\).*packetType\s*==\s*"ObjectMiscellaneous".*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_DIALOGUE.*Rejected ObjectMiscellaneous.*clearFailedDialogueBarterTransaction\(pid\).*isUnsafeFullInventoryReloadGuiOpen\(\).*MWGui::GM_Barter.*MWGui::GM_Dialogue.*MWGui::GM_Container.*void\s+closeUnsafeFullInventoryReloadGui\(\).*setItemDragDropEnabled\(false\).*removeGuiMode\(MWGui::GM_Barter\).*removeGuiMode\(MWGui::GM_Container\).*inventoryAction\s*==\s*InventoryChanges::SET.*closeUnsafeFullInventoryReloadGui\(\).*EventHandlerRejectsObjectMiscellaneousAfterFailedDialogueBarter' `
    -Missing $missing

Test-Pattern -Name "Mixed shared and quest-scoped container packets split sender echo from shared observer snapshots" -Text $baseCell `
    -Pattern 'local\s+sharedContainerUniqueIndexes\s*=\s*\{\}.*local\s+playerScopedContainerUniqueIndexes\s*=\s*\{\}.*local\s+playerScopedContainerData\s*=\s*\{\}.*playerScopedContainerData\[uniqueIndex\]\s*=\s*objectData.*tableHelper\.insertValueIfMissing\(playerScopedContainerUniqueIndexes,\s*uniqueIndex\).*tableHelper\.insertValueIfMissing\(sharedContainerUniqueIndexes,\s*uniqueIndex\).*self:LoadContainers\(pid,\s*self\.data\.objectData,\s*sharedContainerUniqueIndexes,\s*\{.*sendToOtherPlayers\s*=\s*true.*skipAttachedPlayer\s*=\s*true.*\}\).*self:LoadContainers\(pid,\s*playerScopedContainerData,\s*playerScopedContainerUniqueIndexes,\s*\{.*includePlayerScoped\s*=\s*true.*\}\).*function\s+BaseCell:LoadContainers\(pid,\s*objectData,\s*uniqueIndexArray,\s*options\).*local\s+sendToOtherPlayers\s*=.*options\.sendToOtherPlayers.*local\s+skipAttachedPlayer\s*=.*options\.skipAttachedPlayer.*tes3mp\.SendContainer\(sendToOtherPlayers\s*==\s*true,\s*skipAttachedPlayer\s*==\s*true\)' `
    -Missing $missing

Test-Pattern -Name "Object packets expose whether they carry cell data" -Text ($objectPacketHeader + "`n" + $objectPacket) `
    -Pattern 'bool\s+carriesCellData\(\)\s+const;.*bool\s+ObjectPacket::carriesCellData\(\)\s+const\s*\{\s*return\s+hasCellData;\s*\}' `
    -Missing $missing

Test-Pattern -Name "Object activation packets reject truncated object and activator payloads" -Text ($packetObjectActivate + "`n" + $basePacketTests) `
    -Pattern 'PacketObjectActivate::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!PacketHeader\(newBitstream,\s*send\)\).*return;.*if\s*\(!RW\(baseObject\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(baseObject\.isPlayer\).*if\s*\(!RW\(baseObject\.guid,\s*send\)\).*objectList->isValid\s*=\s*false;.*Object\(baseObject,\s*send\);.*if\s*\(!packetValid\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.activatingActor\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(baseObject\.activatingActor\.isPlayer\).*if\s*\(!RW\(baseObject\.activatingActor\.guid,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.activatingActor\.refId,\s*send,\s*true\).*RW\(baseObject\.activatingActor\.refNum,\s*send\).*RW\(baseObject\.activatingActor\.mpNum,\s*send\).*RW\(baseObject\.activatingActor\.name,\s*send\).*objectList->isValid\s*=\s*false;.*objectActivateRejectsTruncatedObjectAndActivatorPayloads' `
    -Missing $missing

Test-Pattern -Name "Custom object packets reject truncated payloads without appending partial objects" -Text ($packetConsoleCommand + "`n" + $packetObjectHit + "`n" + $packetObjectSound + "`n" + $basePacketTests) `
    -Pattern 'PacketConsoleCommand::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!RW\(objectList->consoleCommand,\s*send,\s*true\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!packetValid\).*objectList->isValid\s*=\s*false;.*PacketObjectHit::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!RW\(baseObject\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.hittingActor\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.hitAttack\.success,\s*send\)\).*objectList->isValid\s*=\s*false;.*PacketObjectSound::Packet\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!RW\(baseObject\.isPlayer,\s*send\)\).*objectList->isValid\s*=\s*false;.*if\s*\(!RW\(baseObject\.soundId,\s*send,\s*true\).*RW\(baseObject\.volume,\s*send\).*RW\(baseObject\.pitch,\s*send\)\).*objectList->isValid\s*=\s*false;.*customObjectPacketsRejectTruncatedPayloadsWithoutAppendingObjects' `
    -Missing $missing

Test-Pattern -Name "Server object processors route cell packets only to loaded observers" -Text ($objectProcessorHeader + "`n" + $objectProcessor) `
    -Pattern 'static\s+void\s+sendToLoadedOrBroadcast\(ObjectPacket\s*&\s*packet,\s*BaseObjectList\s*&\s*objectList\).*void\s+ObjectProcessor::Do\(ObjectPacket\s*&\s*packet,\s*Player\s*&\s*player,\s*BaseObjectList\s*&\s*objectList\)\s*\{\s*sendToLoadedOrBroadcast\(packet,\s*objectList\);\s*\}.*void\s+ObjectProcessor::sendToLoadedOrBroadcast\(ObjectPacket\s*&\s*packet,\s*BaseObjectList\s*&\s*objectList\).*!\s*packet\.carriesCellData\(\).*packet\.setObjectList\(&objectList\);.*packet\.Send\(true\);.*Cell\s*\*\s*serverCell\s*=\s*CellController::get\(\)->getCell\(&objectList\.cell\);.*serverCell\s*==\s*nullptr.*return;.*serverCell->sendToLoaded\(&packet,\s*&objectList\);' `
    -Missing $missing

Test-Pattern -Name "Server object processor ignores packets from missing player sessions" -Text $objectProcessor `
    -Pattern 'Player\s*\*\s*player\s*=\s*Players::getPlayer\(packet\.guid\(\)\);.*player\s*==\s*nullptr.*Received %s from missing player session and ignored!.*return\s+true;.*ObjectPacket\s*\*\s*myPacket' `
    -Missing $missing

Test-Pattern -Name "Door state packets use loaded observer routing before script callbacks" -Text $processorDoorState `
    -Pattern 'void\s+Do\(ObjectPacket\s*&\s*packet,\s*Player\s*&\s*player,\s*BaseObjectList\s*&\s*objectList\)\s+override\s*\{\s*sendToLoadedOrBroadcast\(packet,\s*objectList\);.*Script::Call<Script::CallbackIdentity\("OnDoorState"\)>\(player\.getId\(\),\s*objectList\.cell\.getDescription\(\)\.c_str\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Server object script sends route other observers through loaded-cell helper" -Text $objectFunctions `
    -Pattern 'void\s+SendObjectPacket\(mwmp::PacketId\s+packetId,\s*bool\s+sendToOtherPlayers,\s*bool\s+skipAttachedPlayer\).*packet->setObjectList\(&writeObjectList\);.*packet->Send\(false\);.*ObjectProcessor::sendToLoadedOrBroadcast\(\*packet,\s*writeObjectList\);.*void\s+ObjectFunctions::SendObjectActivate\(.*SendObjectPacket\(ID_OBJECT_ACTIVATE,\s*sendToOtherPlayers,\s*skipAttachedPlayer\);.*void\s+ObjectFunctions::SendContainer\(.*SendObjectPacket\(ID_CONTAINER,\s*sendToOtherPlayers,\s*skipAttachedPlayer\);.*void\s+ObjectFunctions::SendClientScriptLocal\(.*SendObjectPacket\(ID_CLIENT_SCRIPT_LOCAL,\s*sendToOtherPlayers,\s*skipAttachedPlayer\);' `
    -Missing $missing

if ([regex]::IsMatch($objectFunctions, 'if\s*\(\s*sendToOtherPlayers\s*\)\s*packet->Send\(true\);', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    $missing.Add("Server object script sends avoid direct global broadcasts")
}

Test-Pattern -Name "Container packets accept object identities without refId" -Text ($packetContainer + "`n" + $basePacketTests) `
    -Pattern 'bool\s+hasObjectIdentity\(const\s+BaseObject&\s+object\).*return\s+!object\.refId\.empty\(\)\s*\|\|\s*object\.refNum\s*!=\s*0\s*\|\|\s*object\.mpNum\s*!=\s*0;.*!isRequest\s*&&\s*!hasObjectIdentity\(baseObject\).*containerPacketRoundTripsServerMpNumWithOriginalRefNum.*container\.refNum\s*=\s*243;.*container\.mpNum\s*=\s*63;.*ASSERT_TRUE\(received\.isValid\);.*containerPacketAcceptsServerMpNumWithoutRefId.*container\.mpNum\s*=\s*63;.*ASSERT_TRUE\(received\.isValid\);.*containerPacketAcceptsLocalRefNumWithoutRefId.*container\.refNum\s*=\s*297461;.*ASSERT_TRUE\(received\.isValid\);' `
    -Missing $missing

Test-Pattern -Name "Server ClearObjectList resets object packet state" -Text $objectFunctions `
    -Pattern 'void\s+ObjectFunctions::ClearObjectList\(\)\s+noexcept\s*\{.*writeObjectList\.baseObjects\.clear\(\);.*writeObjectList\.baseObjectCount\s*=\s*0;.*writeObjectList\.consoleCommand\.clear\(\);.*writeObjectList\.packetOrigin\s*=\s*mwmp::PACKET_ORIGIN::SERVER_SCRIPT;.*writeObjectList\.originClientScript\.clear\(\);.*writeObjectList\.action\s*=\s*mwmp::BaseObjectList::SET;.*writeObjectList\.containerSubAction\s*=\s*mwmp::BaseObjectList::NONE;.*writeObjectList\.isValid\s*=\s*true;.*tempObject\s*=\s*emptyObject;.*tempContainerItem\s*=\s*emptyContainerItem;' `
    -Missing $missing

Test-Pattern -Name "Server object and container script getters reject stale indexes without terminating" -Text $objectFunctions `
    -Pattern '(?=.*const\s+BaseObjectList\*\s+getReadObjectList\(const\s+char\*\s+functionName\)\s+noexcept.*readObjectList\s*==\s*nullptr.*return\s+readObjectList;)(?=.*const\s+BaseObject\*\s+getReadObject\(unsigned\s+int\s+index,\s*const\s+char\*\s+functionName\)\s+noexcept.*const\s+BaseObjectList\*\s+objectList\s*=\s*getReadObjectList\(functionName\);.*index\s*>=\s*objectList->baseObjects\.size\(\).*return\s+&objectList->baseObjects\[index\];)(?=.*const\s+ClientVariable\*\s+getReadClientLocal\(.*variableIndex\s*>=\s*object->clientLocals\.size\(\).*return\s+&object->clientLocals\[variableIndex\];)(?=.*const\s+ContainerItem\*\s+getReadContainerItem\(.*itemIndex\s*>=\s*object->containerItems\.size\(\).*return\s+&object->containerItems\[itemIndex\];)(?=.*unsigned\s+int\s+ObjectFunctions::GetObjectListSize\(\)\s+noexcept.*getReadObjectList\(__func__\).*baseObjects\.size\(\))(?=.*unsigned\s+int\s+ObjectFunctions::GetContainerChangesSize\(unsigned\s+int\s+objectIndex\)\s+noexcept.*containerItems\.size\(\))(?=.*const\s+char\s+\*ObjectFunctions::GetObjectRefId\(unsigned\s+int\s+index\)\s+noexcept.*getReadObject\(index,\s*__func__\))(?=.*const\s+char\s+\*ObjectFunctions::GetContainerItemRefId\(unsigned\s+int\s+objectIndex,\s*unsigned\s+int\s+itemIndex\)\s+noexcept.*getReadContainerItem\(objectIndex,\s*itemIndex,\s*__func__\))(?=.*int\s+ObjectFunctions::GetContainerItemActionCount\(unsigned\s+int\s+objectIndex,\s*unsigned\s+int\s+itemIndex\)\s+noexcept.*getReadContainerItem\(objectIndex,\s*itemIndex,\s*__func__\);)' `
    -Missing $missing

if ([regex]::IsMatch($objectFunctions, 'baseObjects\.at|containerItems\.at|clientLocals\.at', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    $missing.Add("Server object and container script getters avoid throwing vector access")
}

Test-Pattern -Name "Server object script store and send paths normalize decoded vector counts" -Text $objectFunctions `
    -Pattern 'void\s+syncObjectListCounts\(BaseObjectList&\s+objectList\)\s+noexcept.*objectList\.baseObjectCount\s*=\s*static_cast<unsigned\s+int>\(objectList\.baseObjects\.size\(\)\);.*for\s*\(BaseObject&\s+object\s*:\s*objectList\.baseObjects\).*object\.containerItemCount\s*=\s*static_cast<unsigned\s+int>\(object\.containerItems\.size\(\)\);.*void\s+SendObjectPacket\(mwmp::PacketId\s+packetId,\s*bool\s+sendToOtherPlayers,\s*bool\s+skipAttachedPlayer\).*syncObjectListCounts\(writeObjectList\);.*void\s+ObjectFunctions::CopyReceivedObjectListToStore\(\)\s+noexcept.*readObjectList\s*==\s*nullptr.*ClearObjectList\(\);.*writeObjectList\s*=\s*\*readObjectList;.*syncObjectListCounts\(writeObjectList\);.*void\s+ObjectFunctions::AddObject\(\)\s+noexcept.*tempObject\.containerItemCount\s*=\s*static_cast<unsigned\s+int>\(tempObject\.containerItems\.size\(\)\);.*writeObjectList\.baseObjects\.push_back\(tempObject\);.*writeObjectList\.baseObjectCount\s*=\s*static_cast<unsigned\s+int>\(writeObjectList\.baseObjects\.size\(\)\);.*void\s+ObjectFunctions::AddContainerItem\(\)\s+noexcept.*tempObject\.containerItems\.push_back\(tempContainerItem\);.*tempObject\.containerItemCount\s*=\s*static_cast<unsigned\s+int>\(tempObject\.containerItems\.size\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Server container action count setter rejects stale script indexes without terminating" -Text $objectFunctions `
    -Pattern 'void\s+ObjectFunctions::SetContainerItemActionCountByIndex\(unsigned\s+int\s+objectIndex,\s*unsigned\s+int\s+itemIndex,\s*int\s+actionCount\)\s+noexcept\s*\{.*objectIndex\s*>=\s*writeObjectList\.baseObjects\.size\(\).*Ignoring SetContainerItemActionCountByIndex with invalid object index.*return;.*auto&\s+containerItems\s*=\s*writeObjectList\.baseObjects\[objectIndex\]\.containerItems;.*itemIndex\s*>=\s*containerItems\.size\(\).*Ignoring SetContainerItemActionCountByIndex with invalid item index.*return;.*containerItems\[itemIndex\]\.actionCount\s*=\s*actionCount;' `
    -Missing $missing

Test-Pattern -Name "Container REQUEST packets can target objects without refId" -Text ($packetContainer + "`n" + $basePacketTests) `
    -Pattern '!isRequest\s*&&\s*!hasObjectIdentity\(baseObject\).*containerRequestAllowsObjectTargetWithoutRefId.*sent\.action\s*=\s*mwmp::BaseObjectList::REQUEST;.*container\.refNum\s*=\s*297461;.*ASSERT_TRUE\(received\.isValid\);.*EXPECT_EQ\(received\.baseObjects\[0\]\.refId,\s*""\);.*nonRequestContainerRejectsObjectTargetWithoutAnyIdentity.*sent\.action\s*=\s*mwmp::BaseObjectList::SET;.*EXPECT_FALSE\(received\.isValid\);' `
    -Missing $missing

Test-Pattern -Name "Server container SET loads reset subaction to NONE" -Text $baseCell `
    -Pattern 'function\s+BaseCell:LoadContainers.*tes3mp\.SetObjectListAction\(0\)\s*tes3mp\.SetObjectListContainerSubAction\(enumerations\.containerSub\.NONE\).*tes3mp\.SendContainer\(\)' `
    -Missing $missing

Test-Pattern -Name "Client normalizes malformed container SET subactions before replay" -Text $containerProcessor `
    -Pattern 'objectList\.action\s*==\s*mwmp::BaseObjectList::SET\s*&&\s*objectList\.containerSubAction\s*!=\s*mwmp::BaseObjectList::NONE\s*&&\s*objectList\.containerSubAction\s*!=\s*mwmp::BaseObjectList::REPLY_TO_REQUEST.*objectList\.containerSubAction\s*=\s*mwmp::BaseObjectList::NONE;' `
    -Missing $missing

Test-Pattern -Name "Server rejects untrusted client container snapshots and non-lock request packets" -Text ($packetContainerHeader + "`n" + $packetContainer + "`n" + $serverContainerProcessor + "`n" + $containerProcessor + "`n" + $basePacketTests) `
    -Pattern '(?=.*bool\s+isContainerPacketAllowedFromClient\(const\s+BaseObjectList&\s+objectList\);)(?=.*objectList\.packetOrigin\s*==\s*SERVER_SCRIPT.*return\s+false;)(?=.*objectList\.action\s*==\s*BaseObjectList::SET.*objectList\.containerSubAction\s*==\s*BaseObjectList::REPLY_TO_REQUEST;)(?=.*objectList\.action\s*==\s*BaseObjectList::REQUEST.*!isContainerLockSubAction\(objectList\.containerSubAction\).*return\s+false;)(?=.*objectList\.containerSubAction\s*=\s*mwmp::BaseObjectList::LOCK_REQUEST;.*EXPECT_TRUE\(mwmp::isContainerPacketAllowedFromClient\(objectList\)\);)(?=.*objectList\.containerSubAction\s*=\s*mwmp::BaseObjectList::LOCK_RELEASE;.*EXPECT_TRUE\(mwmp::isContainerPacketAllowedFromClient\(objectList\)\);)(?=.*objectList\.baseObjects\[0\]\.containerItemCount\s*=\s*1;.*EXPECT_FALSE\(mwmp::isContainerPacketAllowedFromClient\(objectList\)\);)(?=.*isContainerPacketAllowedFromClient\(objectList\).*Rejected untrusted container packet)(?=.*objectList\.packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;.*objectList\.originClientScript\.clear\(\);)(?=.*containerAuthorityRejectsServerOriginSnapshotsAndRequestsFromClients)' `
    -Missing $missing

Test-Pattern -Name "Server accepts container request replies only from requested players" -Text ($eventHandler + "`n" + $baseCell + "`n" + $componentTests) `
    -Pattern 'subAction\s*==\s*enumerations\.containerSub\.REPLY_TO_REQUEST.*local\s+loadedCell\s*=\s*LoadedCells\[cellDescription\].*loadedCell\s*==\s*nil\s+or\s+loadedCell\.isRequestingContainerData\s*~=\s*true\s+or\s+loadedCell\.containerRequestPid\s*~=\s*pid.*Rejected Container:.*sent unsolicited request reply.*return.*self\.isRequestingContainerData\s*=\s*false\s*self\.containerRequestPid\s*=\s*nil\s*self\.data\.loadState\.hasFullContainerData\s*=\s*true.*EventHandlerRejectsUnsolicitedContainerRequestReplies' `
    -Missing $missing

Test-Pattern -Name "Requested containers resolve by refnum/mpnum without refId" -Text $objectList `
    -Pattern 'MWWorld::Ptr\s+found\s*=\s*baseObject\.refId\.empty\(\)\s*\?\s*searchByRefNum\(cellStore,\s*\*localRefNum\).*if\s*\(\s*baseObject\.refId\.empty\(\)\s*\).*baseObject\.refNum\s*==\s*0.*return\s+MWWorld::Ptr\(\);.*searchByRefNum\(cellStore,\s*baseObject\.refNum\).*registerServerObjectId\(found,\s*baseObject\.mpNum\);' `
    -Missing $missing

Test-Pattern -Name "Client container replay skips invalid saved item ids without aborting frame" -Text $objectList `
    -Pattern 'constexpr\s+int\s+maxContainerItemStackCount\s*=\s*1000000;.*bool\s+canAddContainerItem\(const\s+ContainerItem&\s+item\).*item\.count\s*>\s*0.*item\.count\s*<=\s*maxContainerItemStackCount.*std::isfinite\(item\.enchantmentCharge\);.*bool\s+canRemoveContainerItem\(const\s+ContainerItem&\s+item\).*item\.actionCount\s*>\s*0.*item\.actionCount\s*<=\s*maxContainerItemStackCount.*std::isfinite\(item\.enchantmentCharge\);.*if\s*\(!canAddContainerItem\(containerItem\)\).*Skipping invalid container item.*continue;.*try\s*\{.*MWWorld::ManualRef ref.*containerStore\.add\(newPtr,\s*containerItem\.count\);.*catch\s*\(const std::exception& e\).*Skipping invalid container item' `
    -Missing $missing

Test-Pattern -Name "Client container replay skips bad saved containers without aborting packet" -Text $objectList `
    -Pattern 'if\s*\(baseObjectCount\s*!=\s*baseObjects\.size\(\)\).*Container packet object count mismatch.*for\s*\(const\s+auto&\s+baseObject\s*:\s*baseObjects\).*try\s*\{\s*MWWorld::Ptr\s+ptrFound\s*=\s*searchExact\(cellStore,\s*baseObject\);.*catch\s*\(const std::exception& e\).*Skipping container replay for %s %u-%u after error' `
    -Missing $missing

Test-Pattern -Name "Client container snapshots skip bad items and requested lookup failures" -Text $objectList `
    -Pattern 'bool\s+canSerializeContainerItem\(const\s+MWWorld::Ptr&\s+itemPtr\).*return\s+!isInvalidContainerRefId\(itemPtr\.getCellRef\(\)\.getRefId\(\)\.serializeText\(\)\).*itemPtr\.getCellRef\(\)\.getCount\(\)\s*>\s*0.*std::isfinite\(itemPtr\.getCellRef\(\)\.getEnchantmentCharge\(\)\);.*void\s+ObjectList::addEntireContainer\(const\s+MWWorld::Ptr&\s+ptr\).*try\s*\{.*if\s*\(!canSerializeContainerItem\(itemPtr\)\).*Skipping invalid item while snapshotting container.*catch\s*\(const std::exception& e\).*Skipping container snapshot.*void\s+ObjectList::addRequestedContainers.*try\s*\{.*MWWorld::Ptr\s+ptrFound\s*=\s*searchExact\(cellStore,\s*baseObject\);.*catch\s*\(const std::exception& e\).*Skipping requested container' `
    -Missing $missing

Test-Pattern -Name "Client and server keep live actor container replays out of barter inventory" -Text ($objectList + "`n" + $baseCell + "`n" + $componentTests) `
    -Pattern 'bool\s+isLiveActor\(const\s+MWWorld::Ptr&\s+ptr\).*ptr\.isEmpty\(\)\s*\|\|\s*!ptr\.getClass\(\)\.isActor\(\).*return\s+!ptr\.getClass\(\)\.getCreatureStats\(ptr\)\.isDead\(\);.*bool\s+shouldSerializeContainerSnapshot\(const\s+MWWorld::Ptr&\s+ptr\).*if\s*\(!hasContainerStore\(ptr\)\)\s*return\s+false;.*return\s+!isLiveActor\(ptr\);.*ptrFoundIsLiveActor\s*=\s*isLiveActor\(ptrFound\).*isNetworkLiveActorContainerMutation\s*=.*ptrFoundIsLiveActor.*BaseObjectList::SET.*BaseObjectList::ADD.*BaseObjectList::REMOVE.*Skipping network container mutation for live actor.*continue;.*void\s+ObjectList::addAllContainers\(MWWorld::CellStore\*\s*cellStore\).*shouldSerializeContainerSnapshot\(ptr\).*Skipping live actor container snapshot.*void\s+ObjectList::addRequestedContainers.*shouldSerializeContainerSnapshot\(ptrFound\).*Skipping requested live actor container snapshot.*local\s+function\s+isLiveActorObjectData\(objectData\).*objectData\.stats\s*~=\s*nil\s+or\s+objectData\.equipment\s*~=\s*nil.*function\s+BaseCell:LoadObjectInteractionLockSnapshots.*objectData\s*~=\s*nil\s+and\s+objectData\.inventory\s*~=\s*nil\s+and\s+not\s+isLiveActorObjectData\(objectData\).*Skipping live actor container snapshot.*function\s+BaseCell:LoadContainerTombstones.*currentObjectData\.containerTombstones\s*~=\s*nil\s+and\s+not\s+isLiveActorObjectData\(currentObjectData\).*Skipping live actor container tombstones.*function\s+BaseCell:LoadContainers.*currentObjectData\.inventory\s*~=\s*nil\s+and\s+not\s+isLiveActorObjectData\(currentObjectData\).*Skipping live actor container load.*cell\.data\.objectData\["331473-0"\].*refId\s*=\s*"arrille".*containerTombstones.*assert\(hasCall\("SetObjectRefId:arrille"\)\s*==\s*false\)' `
    -Missing $missing

Test-Pattern -Name "Server container load skips invalid saved items before packet send" -Text $baseCell `
    -Pattern 'local\s+function\s+isValidContainerLoadItem\(item\).*type\(item\.refId\)\s*~=\s*"string".*string\.find\(item\.refId,\s*"\$dynamic",\s*1,\s*true\).*item\.count\s*=\s*tonumber\(item\.count\).*item\.count\s*==\s*nil\s+or\s+item\.count\s*<=\s*0.*not\s+isFiniteNumber\(item\.enchantmentCharge\).*for\s+itemIndex,\s*item\s+in\s+pairs\(currentObjectData\.inventory\)\s+do.*if\s+not\s+isValidContainerLoadItem\(item\)\s+then.*Skipping invalid saved container item.*else\s+tes3mp\.SetContainerItemRefId\(item\.refId\).*tes3mp\.AddContainerItem\(\)' `
    -Missing $missing

Test-Pattern -Name "Container packets skip impossible item counts before client replay" -Text ($packetContainer + "`n" + $objectList + "`n" + $basePacketTests) `
    -Pattern 'constexpr\s+int\s+maxContainerItemStackCount\s*=\s*1000000;.*isValidContainerItem\(unsigned\s+char\s+action,\s*const\s+ContainerItem&\s+item\).*action\s*==\s*BaseObjectList::SET\s*\|\|\s*action\s*==\s*BaseObjectList::ADD.*item\.count\s*>\s*0\s*&&\s*item\.count\s*<=\s*maxContainerItemStackCount.*action\s*==\s*BaseObjectList::REMOVE.*item\.actionCount\s*>\s*0\s*&&\s*item\.actionCount\s*<=\s*maxContainerItemStackCount.*canAddContainerItem\(const\s+ContainerItem&\s+item\).*item\.count\s*<=\s*maxContainerItemStackCount.*canRemoveContainerItem\(const\s+ContainerItem&\s+item\).*item\.actionCount\s*<=\s*maxContainerItemStackCount.*containerPacketSkipsImpossibleItemCountsWithoutRejectingValidItems' `
    -Missing $missing

Test-Pattern -Name "Server accepts first-touch container removes before full snapshot without repeat removals" -Text ($baseCell + "`n" + $componentTests) `
    -Pattern 'getContainerItemKey\(refId,\s*charge,\s*enchantmentCharge,\s*soul\).*containerTombstones.*isGameplayContainerTake\s*=\s*packetOrigin\s*==\s*enumerations\.packetOrigin\.CLIENT_GAMEPLAY\s+and\s+\(\s*subAction\s*==\s*enumerations\.containerSub\.DRAG\s+or\s+subAction\s*==\s*enumerations\.containerSub\.TAKE_ALL\s*\).*allowUnknownContainerRemove\s*=\s*actionCount\s*>\s*0\s+and\s+\(\s*not\s+self:HasFullContainerData\(\)\s+or\s+isGameplayContainerTake\s*\)\s+and\s+not\s+hasContainerItemTombstone\(objectData,\s*itemRefId,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*Accepting first-touch removal.*inventoryHelper\.addItem\(inventory,\s*itemRefId,\s*remainingCount,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*addContainerItemTombstone\(objectData,\s*itemRefId,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*CellBaseAcceptsFirstTouchContainerRemovesBeforeFullSnapshot.*staleFullCell\.data\.loadState\.hasFullContainerData\s*=\s*true.*SetContainerItemActionCountByIndex:0:0:0' `
    -Missing $missing

Test-Pattern -Name "Server keeps visible gameplay remove counts when stale full snapshots undercount containers" -Text ($baseCell + "`n" + $componentTests) `
    -Pattern 'newCount\s*=\s*item\.count\s*-\s*actionCount.*if\s+isGameplayContainerTake\s+and\s+not\s+hasContainerItemTombstone\(objectData,\s*itemRefId,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\)\s+then.*Accepting visible gameplay removal.*addContainerItemTombstone\(objectData,\s*itemRefId,\s*itemCharge,\s*itemEnchantmentCharge,\s*itemSoul\).*stalePartialCell\.data\.loadState\.hasFullContainerData\s*=\s*true.*actionCount\s*=\s*5.*assert\(currentItems\[1\]\.actionCount\s*==\s*5\)' `
    -Missing $missing

Test-Pattern -Name "Quest-critical containers are scoped to character saves instead of shared cell state" -Text ($configScript + "`n" + $baseCell + "`n" + $componentTests) `
    -Pattern 'config\.playerScopedContainers\s*=\s*\{.*refIdPrefix\s*=\s*"flora_".*Seyda Neen \(-2, -9\).*479183-0.*flora_treestump_unique.*getGridMatchedPlayerScopedCellConfig\(cellDescription\).*configuredGridX\s*==\s*gridX\s+and\s+configuredGridY\s*==\s*gridY.*function\s+BaseCell:IsPlayerScopedContainer\(uniqueIndex,\s*objectData\).*function\s+BaseCell:GetPlayerScopedContainerData\(pid,\s*uniqueIndex,\s*refId\).*Players\[pid\]\.data\.playerScopedContainers.*function\s+BaseCell:GetPlayerScopedContainerPackets\(pid\).*mergeCellContainers.*function\s+BaseCell:SaveContainers\(pid\).*hasPlayerScopedContainerChanges.*Players\[pid\]:QuicksaveToDrive\(\).*function\s+BaseCell:LoadContainers\(pid,\s*objectData,\s*uniqueIndexArray,\s*options\).*includePlayerScoped.*not\s+self:IsPlayerScopedContainer\(uniqueIndex,\s*currentObjectData\).*function\s+BaseCell:LoadPlayerScopedContainers\(pid\).*CellBasePlayerScopedContainersBypassSharedWorldState.*BaseCell\("Wilderness \(-2, -9\)"\).*ring_keley.*flora_bc_mushroom_03' `
    -Missing $missing

Test-Pattern -Name "Quest-critical item refIds force player-scoped container saves" -Text ($configScript + "`n" + $baseCell) `
    -Pattern 'config\.playerScopedContainerItemRefIds\s*=\s*\{.*ring_keley\s*=\s*true.*ring_healing_01\s*=\s*true.*local\s+function\s+isPlayerScopedContainerItem\(refId\).*config\.playerScopedContainerItemRefIds.*string\.lower\(configuredRefId\)\s*==\s*normalizedRefId.*local\s+function\s+containerPacketTouchesPlayerScopedItem\(objectIndex\).*GetContainerChangesSize\(objectIndex\).*isPlayerScopedContainerItem\(tes3mp\.GetContainerItemRefId\(objectIndex,\s*itemIndex\)\).*local\s+isScopedContainer\s*=\s*self:IsPlayerScopedContainer\(uniqueIndex,\s*objectData\)\s+or\s+containerPacketTouchesPlayerScopedItem\(objectIndex\)' `
    -Missing $missing

Test-Pattern -Name "Cell visit tracking uses selected character identity under shared accounts" -Text ($baseCell + "`n" + $playerBase + "`n" + $componentTests) `
    -Pattern 'local\s+function\s+getPlayerCellVisitKey\(pid\).*Players\[pid\]:GetCellVisitKey\(\).*local\s+visitKey\s*=\s*getPlayerCellVisitKey\(pid\).*self\.data\.lastVisit\[visitKey\].*self:SaveLastVisit\(getPlayerCellVisitKey\(pid\)\).*function\s+BasePlayer:GetCharacterStorageKey\(\).*not\s+self\.creatingNewCharacter.*accountName\s*\.\.\s*"#character:".*function\s+BasePlayer:GetCellVisitKey\(\).*self:GetCharacterStorageKey\(\).*CellBaseUsesCharacterScopedVisitKeys.*Account#character:1' `
    -Missing $missing

Test-Pattern -Name "Local echoed container removes add items and persist player inventory" -Text $objectList `
    -Pattern 'bool\s+isLocalEvent\s*=\s*guid\s*==\s*Main::get\(\)\.getLocalPlayer\(\)->guid;.*isAcceptedLocalGameplayRemoval\s*=\s*isLocalEvent\s*&&\s*packetOrigin\s*==\s*CLIENT_GAMEPLAY\s*&&\s*action\s*==\s*BaseObjectList::REMOVE;.*bool\s+isLocalDrag\s*=\s*isAcceptedLocalGameplayRemoval\s*&&\s*containerSubAction\s*==\s*BaseObjectList::DRAG;.*bool\s+isLocalTakeAll\s*=\s*isAcceptedLocalGameplayRemoval\s*&&\s*containerSubAction\s*==\s*BaseObjectList::TAKE_ALL;.*const\s+int\s+removeCount\s*=\s*std::min\(containerItem\.actionCount,\s*itemPtr\.getCellRef\(\)\.getCount\(\)\);.*MWWorld::ManualRef\s+localItemCopy\(.*itemPtr,\s*removeCount\);.*containerStore\.remove\(itemPtr,\s*removeCount\);.*MWWorld::Ptr\s+addedItem\s*=\s*\*playerStore\.add\(localItemCopy\.getPtr\(\),\s*removeCount\);.*localPlayer->sendItemChange\(.*addedItem,\s*removeCount,\s*mwmp::InventoryChanges::ADD\);.*localPlayer->updateInventoryWindow\(\);' `
    -Missing $missing

Test-Pattern -Name "Current container tracking preserves server object ids" -Text ($localPlayer + "`n" + $objectList) `
    -Pattern 'void\s+LocalPlayer::storeCurrentContainer\(const\s+MWWorld::Ptr\s+&container\).*currentContainer\.mpNum\s*=\s*getNetworking\(\)->getObjectList\(\)->getServerMpNum\(container\);.*const\s+unsigned\s+int\s+serverMpNum\s*=\s*mwmp::Main::get\(\)\.getNetworking\(\)->getObjectList\(\)->getServerMpNum\(ptrFound\);.*currentContainer->refId\s*==\s*refIdToString\(ptrFound\.getCellRef\(\)\.getRefId\(\)\).*currentContainer->refNum\s*==\s*ptrFound\.getCellRef\(\)\.getRefNum\(\)\.mIndex.*currentContainer->mpNum\s*==\s*serverMpNum' `
    -Missing $missing

Write-Host "TES3MP container looting sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 52"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
