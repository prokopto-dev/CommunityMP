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

$objectBindings = Get-SourceText "apps\openmw\mwlua\objectbindings.cpp"
$actorBindings = Get-SourceText "apps\openmw\mwlua\types\actor.cpp"
$magicBindings = Get-SourceText "apps\openmw\mwlua\magicbindings.cpp"
$objectList = Get-SourceText "apps\openmw\mwmp\ObjectList.cpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$inventoryProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerInventory.hpp"
$basePlayer = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$playerMiscellaneousPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerMiscellaneous.cpp"
$miscellaneousProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerMiscellaneous.hpp"
$mechanicsHeader = Get-SourceText "apps\openmw-mp\Script\Functions\Mechanics.hpp"
$mechanicsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Mechanics.cpp"
$enumerations = Get-SourceText "files\tes3mp\server\scripts\enumerations.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "TES3MP Lua object packets require global context, initialized client, in-cell object, and logged-in player" -Text $objectBindings `
    -Pattern 'bool\s+canSendTes3mpLuaObjectPacket\(const\s+MWWorld::Ptr&\s+ptr,\s+Context::Type\s+contextType\)\s*\{.*contextType\s*!=\s*Context::Global\s*\|\|\s*!mwmp::Main::isInitialized\(\)\s*\|\|\s*!ptr\.isInCell\(\).*localPlayer\s*!=\s*nullptr\s*&&\s*localPlayer->isLoggedIn\(\);' `
    -Missing $missing
Test-Pattern -Name "TES3MP Lua object packets are marked as OpenMW Lua global origin" -Text $objectBindings `
    -Pattern 'objectList->packetOrigin\s*=\s*mwmp::CLIENT_SCRIPT_GLOBAL;\s*objectList->originClientScript\s*=\s*"openmw-lua:global";' `
    -Missing $missing
Test-Pattern -Name "Object delete helper sends ObjectDelete packets" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaObjectDeletePacket\(.*?\)\s*\{.*objectList->addObjectGeneric\(ptr\);\s*objectList->sendObjectDelete\(\);\s*return\s+true;' `
    -Missing $missing
Test-Pattern -Name "Container add helper sends Container ADD packets" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaContainerAddPacket\(.*?\)\s*\{.*objectList->action\s*=\s*mwmp::BaseObjectList::ADD;.*objectList->addContainerItem\(baseObject,\s*item,\s*itemCount,\s*0\);.*objectList->sendContainer\(\);' `
    -Missing $missing
Test-Pattern -Name "Container remove helper sends Container REMOVE packets with action count" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaContainerRemovePacket\(.*?\)\s*\{.*objectList->action\s*=\s*mwmp::BaseObjectList::REMOVE;.*objectList->addContainerItem\(baseObject,\s*item,\s*itemCount,\s*itemCount\);.*objectList->sendContainer\(\);' `
    -Missing $missing
Test-Pattern -Name "Local-player inventory helper only syncs logged-in local player inventory and skips dynamic IDs" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaLocalPlayerInventoryPacket\(.*?\)\s*\{.*contextType\s*!=\s*Context::Global\s*\|\|\s*!mwmp::Main::isInitialized\(\).*localPlayer\s*==\s*nullptr\s*\|\|\s*!localPlayer->isLoggedIn\(\).*container\s*!=\s*MWBase::Environment::get\(\)\.getWorld\(\)->getPlayerPtr\(\).*itemRefId\.find\("\$dynamic"\)\s*!=\s*std::string::npos.*localPlayer->sendItemChange\(item,\s*itemCount,\s*action\);' `
    -Missing $missing
Test-Pattern -Name "Object remove helper routes local player inventory removes to PlayerInventory REMOVE" -Text $objectBindings `
    -Pattern 'if\s*\(sourceContainerPtr\s*==\s*world->getPlayerPtr\(\)\)\s*return\s+sendTes3mpLuaLocalPlayerInventoryPacket\(\s*sourceContainerPtr,\s*ptr,\s*itemCount,\s*mwmp::InventoryChanges::REMOVE,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "Object remove helper routes synced world-container removes to Container REMOVE" -Text $objectBindings `
    -Pattern 'if\s*\(sourceContainerPtr\.isInCell\(\)\s*&&\s*!mwmp::PlayerList::isDedicatedPlayer\(sourceContainerPtr\)\)\s*return\s+sendTes3mpLuaContainerRemovePacket\(sourceContainerPtr,\s*ptr,\s*itemCount,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "Object place helper sends ObjectPlace only when a base object was produced" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaObjectPlacePacket\(.*?\)\s*\{.*objectList->addObjectPlace\(ptr\);\s*if\s*\(objectList->baseObjects\.empty\(\)\)\s*return\s+false;\s*objectList->sendObjectPlace\(\);' `
    -Missing $missing
Test-Pattern -Name "Same-cell object teleport sends ObjectMove then ObjectRotate" -Text $objectBindings `
    -Pattern 'bool\s+sendTes3mpLuaObjectTeleportPacket\(.*?\)\s*\{.*objectList->addObjectMove\(ptr,\s*position\);\s*objectList->sendObjectMove\(\);.*objectList->addObjectRotate\(ptr,\s*position\);\s*objectList->sendObjectRotate\(\);' `
    -Missing $missing
Test-Pattern -Name "Same-cell Lua object teleport applies local teleport before move/rotate sync" -Text $objectBindings `
    -Pattern 'syncTes3mpSameCellTeleport\s*=\s*canSendTes3mpLuaObjectPacket\(ptr,\s*context\.mType\)\s*&&\s*ptr\.getCell\(\)\s*==\s*cell\s*&&\s*!placeOnGround.*MWWorld::Ptr\s+newPtr\s*=\s*teleportNotPlayer\(oldPtr,\s*cell,\s*pos,\s*rot,\s*placeOnGround\);.*else\s+if\s*\(syncTes3mpSameCellTeleport\).*finalPosition.*sendTes3mpLuaObjectTeleportPacket\(newPtr,\s*newPtr\.getCell\(\),\s*finalPosition\.asVec3\(\),\s*finalPosition\.asRotationVec3\(\),\s*false,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "Same-cell door teleport sync is allowed and replay refreshes door original transform" -Text ($objectBindings + "`n" + $objectList) `
    -Pattern 'sendTes3mpLuaObjectTeleportPacket\(.*?ptr\.getCell\(\)\s*!=\s*destCell\s*\|\|\s*placeOnGround\)\s*return\s+false;.*canSendTes3mpLuaObjectGroundedSameCellTeleportPacket\(.*?ptr\.getClass\(\)\.isActor\(\)\).*void\s+updateDoorOriginalPosition\(const\s+MWWorld::Ptr&\s+ptr\).*ptr\.getClass\(\)\.isDoor\(\).*ptr\.getCellRef\(\)\.setPosition\(ptr\.getRefData\(\)\.getPosition\(\)\);.*void\s+ObjectList::moveObjects\(MWWorld::CellStore\*\s+cellStore\).*moveObject\(ptrFound,\s*baseObject\.position\.asVec3\(\)\);.*updateDoorOriginalPosition\(ptrFound\);.*void\s+ObjectList::rotateObjects\(MWWorld::CellStore\*\s+cellStore\).*rotateObject\(.*?ptrFound,\s*baseObject\.position\.asRotationVec3\(\),\s*MWBase::RotationFlag_none\);.*updateDoorOriginalPosition\(ptrFound\);' `
    -Missing $missing
Test-Pattern -Name "Cross-cell teleport sync excludes doors, actors, and draft-cell sources" -Text $objectBindings `
    -Pattern 'bool\s+canSendTes3mpLuaObjectCrossCellTeleportPacket\(.*?\)\s*\{.*ptr\.getCell\(\)\s*==\s*destCell\s*\|\|\s*ptr\.getClass\(\)\.isDoor\(\)\s*\|\|\s*ptr\.getClass\(\)\.isActor\(\).*return\s+ptr\.getCell\(\)\s*!=\s*&MWBase::Environment::get\(\)\.getWorldModel\(\)->getDraftCell\(\);' `
    -Missing $missing
Test-Pattern -Name "Same-cell grounded teleport resyncs adjusted position through move/rotate packets" -Text $objectBindings `
    -Pattern 'syncTes3mpGroundedSameCellTeleport.*sendTes3mpLuaObjectTeleportPacket\(newPtr,\s*newPtr\.getCell\(\),\s*adjustedPosition\.asVec3\(\),\s*adjustedPosition\.asRotationVec3\(\),\s*false,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "Global object:remove routes contained items through delayed TES3MP remove packets" -Text $objectBindings `
    -Pattern 'objectT\["remove"\].*context\.mType\s*==\s*Context::Global\s*&&\s*ptr\.getContainerStore\(\).*sendTes3mpLuaObjectRemovePacket\(actionPtr,\s*countToRemove,\s*contextType\)' `
    -Missing $missing
Test-Pattern -Name "Global object:remove routes full in-cell world object removal through ObjectDelete" -Text $objectBindings `
    -Pattern 'objectT\["remove"\].*context\.mType\s*==\s*Context::Global\s*&&\s*ptr\.isInCell\(\)\s*&&\s*!ptr\.getContainerStore\(\)\s*&&\s*countToRemove\s*==\s*currentCount.*sendTes3mpLuaObjectDeletePacket\(actionPtr,\s*contextType\)' `
    -Missing $missing
Test-Pattern -Name "Global object:split creates a disabled draft object and delays TES3MP removal" -Text $objectBindings `
    -Pattern 'objectT\["split"\].*context\.mType\s*==\s*Context::Global\s*&&\s*ptr\.getContainerStore\(\).*copyToCell\(ptr,\s*\*cell,\s*count\);\s*splitted\.getRefData\(\)\.disable\(\);.*sendTes3mpLuaObjectRemovePacket\(actionPtr,\s*count,\s*contextType\)' `
    -Missing $missing
Test-Pattern -Name "moveInto detects world, container, and local-player inventory sync cases" -Text $objectBindings `
    -Pattern 'syncTes3mpWorldObjectMove.*syncTes3mpWorldObjectToLocalPlayerInventory.*syncTes3mpContainerObjectMove.*syncTes3mpLocalPlayerInventoryAdd.*syncTes3mpLocalPlayerInventoryRemove' `
    -Missing $missing
Test-Pattern -Name "moveInto sends container remove/add and PlayerInventory add/remove packets before local add" -Text $objectBindings `
    -Pattern 'if\s*\(syncTes3mpWorldObjectMove\)\s*sendTes3mpLuaContainerAddPacket\(cont\.ptr\(\),\s*oldPtr,\s*count,\s*contextType\);.*syncTes3mpContainerObjectMove.*sendTes3mpLuaContainerRemovePacket\(sourceContainerPtr,\s*oldPtr,\s*count,\s*contextType\);.*sendTes3mpLuaContainerAddPacket\(cont\.ptr\(\),\s*oldPtr,\s*count,\s*contextType\);.*syncTes3mpLocalPlayerInventoryRemove.*mwmp::InventoryChanges::REMOVE.*syncTes3mpLocalPlayerInventoryAdd.*mwmp::InventoryChanges::ADD' `
    -Missing $missing
Test-Pattern -Name "moveInto deletes source world object after world-object-to-container/player transfer" -Text $objectBindings `
    -Pattern 'if\s*\(\(syncTes3mpWorldObjectMove\s*\|\|\s*syncTes3mpWorldObjectToLocalPlayerInventory\)\s*&&\s*!syncTes3mpDraftObjectMove\)\s*sendTes3mpLuaObjectDeletePacket\(oldPtr,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "teleport from container places destination object then removes source inventory/container item" -Text $objectBindings `
    -Pattern 'if\s*\(ptr\.getContainerStore\(\)\).*sendTes3mpLuaObjectPlacePacket\(placedPtr,\s*contextType\)\)\s*sendTes3mpLuaObjectRemovePacket\(oldPtr,\s*count,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "cross-cell world teleport sends delete before local teleport and place after materialization" -Text $objectBindings `
    -Pattern 'if\s*\(syncTes3mpCrossCellTeleport\)\s*sendTes3mpLuaObjectDeletePacket\(oldPtr,\s*contextType\);.*if\s*\(materializesDraftObject\s*\|\|\s*syncTes3mpCrossCellTeleport\)\s*sendTes3mpLuaObjectPlacePacket\(newPtr,\s*contextType\);' `
    -Missing $missing
Test-Pattern -Name "ObjectList refuses ObjectPlace for unsynchronized dynamic refIds" -Text $objectList `
    -Pattern 'void\s+ObjectList::addObjectPlace\(.*?\)\s*\{.*refIdToString\(ptr\.getCellRef\(\)\.getRefId\(\)\)\.find\("\$dynamic"\).*You cannot place unsynchronized custom items in multiplayer\.' `
    -Missing $missing
Test-Pattern -Name "ObjectPlace sends charge, enchantment, soul, container, dropped-by-player, visible position, count, and gold value" -Text $objectList `
    -Pattern 'baseObject\.charge\s*=.*baseObject\.enchantmentCharge\s*=.*baseObject\.soul\s*=.*baseObject\.droppedByPlayer\s*=.*baseObject\.hasContainer\s*=.*baseObject\.position\s*=.*baseObject\.count\s*=.*baseObject\.goldValue\s*=' `
    -Missing $missing
Test-Pattern -Name "ObjectPlace packet send skips empty base-object lists" -Text $objectList `
    -Pattern 'void\s+ObjectList::sendObjectPlace\(\)\s*\{\s*if\s*\(baseObjects\.size\(\)\s*==\s*0\)\s*return;' `
    -Missing $missing
Test-Pattern -Name "Object activation replay skips unresolved activators before dereferencing them" -Text $objectList `
    -Pattern 'void\s+ObjectList::activateObjects\(MWWorld::CellStore\*\s+cellStore\).*activatingActorPtr\s*=\s*MechanicsHelper::getPlayerPtr\(baseObject\.activatingActor\);.*if\s*\(activatingActorPtr\.isEmpty\(\)\).*Ignoring activation with unresolved player activator.*else.*activatingActorPtr\.getClass\(\)\.getName\(activatingActorPtr\).*activatingActorPtr\s*=\s*searchExact\(cellStore,\s*baseObject\.activatingActor\.refNum,\s*baseObject\.activatingActor\.mpNum,\s*baseObject\.activatingActor\.refId\);.*if\s*\(activatingActorPtr\.isEmpty\(\)\).*Ignoring activation with unresolved actor activator.*else.*activatingActorPtr\.getCellRef\(\)\.getRefId\(\).*if\s*\(!activatingActorPtr\.isEmpty\(\)\).*ptrFound\.getClass\(\)\.activate\(ptrFound,\s*activatingActorPtr\)' `
    -Missing $missing
Test-Pattern -Name "Object activation replay skips action construction and execution failures" -Text $objectList `
    -Pattern 'if\s*\(!activatingActorPtr\.isEmpty\(\)\)\s*\{\s*try\s*\{.*pickUpObject\(ptrFound\).*std::unique_ptr<MWWorld::Action>\s+activation\s*=.*ptrFound\.getClass\(\)\.activate\(ptrFound,\s*activatingActorPtr\);.*if\s*\(activation\)\s*activation->execute\(activatingActorPtr\);.*else\s*LOG_MESSAGE_SIMPLE\(TimedLog::LOG_ERROR,\s*"Skipping object activation.*because no action was created".*catch\s*\(const std::exception& e\).*LOG_MESSAGE_SIMPLE\(TimedLog::LOG_ERROR,\s*"Skipping object activation.*after error: %s"' `
    -Missing $missing
Test-Pattern -Name "LocalPlayer item change sends PlayerInventory packet with requested action" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendItemChange\(const\s+MWWorld::Ptr&\s+itemPtr,\s*int\s+count,\s*unsigned\s+int\s+action\)\s*\{.*MechanicsHelper::getItem\(itemPtr,\s*count\);\s*sendItemChange\(item,\s*action\);' `
    -Missing $missing
Test-Pattern -Name "Incoming PlayerInventory packets suppress echo while applying ADD/REMOVE/SET" -Text $inventoryProcessor `
    -Pattern 'localPlayer\.avoidSendingInventoryPackets\s*=\s*true;.*if\s*\(inventoryAction\s*==\s*InventoryChanges::ADD\).*localPlayer\.addItems\(\);.*else\s+if\s*\(inventoryAction\s*==\s*InventoryChanges::REMOVE\).*localPlayer\.removeItems\(\);.*else.*localPlayer\.setInventory\(\);.*localPlayer\.avoidSendingInventoryPackets\s*=\s*false;' `
    -Missing $missing
Test-Pattern -Name "PlayerMiscellaneous carries selected enchanted item identity and validates it" -Text ($basePlayer + "`n" + $playerMiscellaneousPacket + "`n" + $miscellaneousProcessor) `
    -Pattern 'SELECTED_ENCHANTED_ITEM.*mwmp::Item\s+selectedEnchantedItem.*RW\(player->selectedEnchantedItem\.refId,\s*send,\s*true\).*RW\(player->selectedEnchantedItem\.count,\s*send\).*RW\(player->selectedEnchantedItem\.charge,\s*send\).*RW\(player->selectedEnchantedItem\.enchantmentCharge,\s*send\).*RW\(player->selectedEnchantedItem\.soul,\s*send,\s*true\).*isValidEquipmentItem\(player->selectedEnchantedItem\).*localPlayer\.setSelectedEnchantedItem\(\)' `
    -Missing $missing
Test-Pattern -Name "Lua selected enchanted item sends exact local-player item and spell selection clears it" -Text ($actorBindings + "`n" + $magicBindings + "`n" + $localPlayer) `
    -Pattern 'MechanicsHelper::getItem\(selectedItem,\s*selectedItem\.getCellRef\(\)\.getCount\(\)\).*setSelectedEnchantedItem\(obj\.ptr\(\),\s*ei\).*sendTes3mpLuaSelectedEnchantedItemPacket\(obj\.ptr\(\),\s*selectedItem\).*sendTes3mpLuaSelectedEnchantedItemClearPacket.*sendSelectedEnchantedItem\(mwmp::Item\(\)\).*void\s+LocalPlayer::sendSelectedEnchantedItem\(const\s+mwmp::Item&' `
    -Missing $missing
Test-Pattern -Name "Server persists and replays selected enchanted item as mutually exclusive castable state" -Text ($mechanicsHeader + "`n" + $mechanicsFunctions + "`n" + $enumerations + "`n" + $eventHandler + "`n" + $playerBase) `
    -Pattern 'GetSelectedEnchantedItemRefId.*GetSelectedEnchantedItemCount.*GetSelectedEnchantedItemCharge.*GetSelectedEnchantedItemEnchantmentCharge.*GetSelectedEnchantedItemSoul.*SetSelectedEnchantedItem.*SendSelectedEnchantedItem.*SELECTED_ENCHANTED_ITEM\s*=\s*2.*OnPlayerSelectedEnchantedItem.*SaveSelectedEnchantedItem.*LoadSelectedEnchantedItem.*SetSelectedEnchantedItem\(self\.pid.*SendSelectedEnchantedItem\(self\.pid\).*SaveSelectedEnchantedItem.*selectedSpell\s*=\s*""' `
    -Missing $missing

Write-Host "TES3MP Lua object mutation sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 32"
Write-Host "Missing guards: $($missing.Count)"

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing or changed object mutation sync patterns:"
    foreach ($item in $missing) {
        Write-Host "  $item"
    }

    if ($FailOnMissingGuard) {
        exit 1
    }
}
