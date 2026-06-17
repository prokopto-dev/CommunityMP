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

$dragAndDrop = Get-SourceText "apps\openmw\mwgui\draganddrop.cpp"
$worldItemModel = Get-SourceText "apps\openmw\mwgui\worlditemmodel.hpp"
$objectList = Get-SourceText "apps\openmw\mwmp\ObjectList.cpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Drag-and-drop preflights target onDropItem before moving source stack" -Text $dragAndDrop `
    -Pattern 'if\s*\(targetModel\s*!=\s*mSourceModel\)\s*\{\s*if\s*\(!targetModel->onDropItem\(mItem\.mBase,\s*static_cast<int>\(mDraggedCount\)\)\)\s*return;\s*mSourceModel->moveItem\(mItem,\s*mDraggedCount,\s*targetModel\);' `
    -Missing $missing

Test-Pattern -Name "World drops reject unsynchronized dynamic items before local placement in logged-in TES3MP sessions" -Text $worldItemModel `
    -Pattern 'bool\s+canSyncTes3mpDrop\(const\s+MWWorld::Ptr&\s+item\)\s+const\s*\{.*mwmp::Main::isInitialized\(\).*localPlayer\s*==\s*nullptr\s*\|\|\s*!localPlayer->isLoggedIn\(\).*item\.getCellRef\(\)\.getRefId\(\)\.serializeText\(\)\.find\("\$dynamic"\).*You cannot place unsynchronized custom items in multiplayer\..*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "WorldItemModel onDropItem uses the TES3MP drop preflight" -Text $worldItemModel `
    -Pattern 'bool\s+onDropItem\(const\s+MWWorld::Ptr&\s+item,\s*int\s*/\*count\*/\)\s+override\s*\{.*return\s+canSyncTes3mpDrop\(item\);.*return\s+true;' `
    -Missing $missing

Test-Pattern -Name "World drop sync marks ObjectPlace packets as client gameplay and dropped-by-player" -Text $worldItemModel `
    -Pattern 'objectList->reset\(\);\s*objectList->packetOrigin\s*=\s*mwmp::CLIENT_GAMEPLAY;\s*objectList->addObjectPlace\(dropped,\s*true\);' `
    -Missing $missing

Test-Pattern -Name "World drop sync sends PlayerInventory REMOVE before ObjectPlace" -Text $worldItemModel `
    -Pattern 'if\s*\(objectList->baseObjects\.empty\(\)\)\s*return;\s*localPlayer->sendItemChange\(item\.mBase,\s*count,\s*mwmp::InventoryChanges::REMOVE\);\s*objectList->sendObjectPlace\(\);' `
    -Missing $missing

Test-Pattern -Name "ObjectPlace keeps dropped-by-player state, count, gold value, charge, enchantment, and soul" -Text $objectList `
    -Pattern 'void\s+ObjectList::addObjectPlace\(.*?\)\s*\{.*baseObject\.charge\s*=.*baseObject\.enchantmentCharge\s*=.*baseObject\.soul\s*=.*baseObject\.droppedByPlayer\s*=\s*droppedByPlayer;.*baseObject\.count\s*=.*baseObject\.goldValue\s*=' `
    -Missing $missing

Test-Pattern -Name "PlayerInventory item-change packets preserve item metadata through MechanicsHelper" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendItemChange\(const\s+MWWorld::Ptr&\s+itemPtr,\s*int\s+count,\s*unsigned\s+int\s+action\)\s*\{\s*mwmp::Item\s+item\s*=\s*MechanicsHelper::getItem\(itemPtr,\s*count\);\s*sendItemChange\(item,\s*action\);' `
    -Missing $missing

Write-Host "TES3MP GUI inventory drop sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 7"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
