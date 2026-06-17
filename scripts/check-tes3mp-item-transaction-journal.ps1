param(
    [switch]$FailOnMissingGuard
)

$ErrorActionPreference = "Stop"

$SourceRoot = Split-Path -Parent $PSScriptRoot

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if ($Text -notmatch "(?s)$Pattern") {
        $Missing.Add($Name)
    }
}

$missing = [System.Collections.Generic.List[string]]::new()

$config = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$journal = Get-SourceText "files\tes3mp\server\scripts\communitymp\saves\itemTransactionJournal.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$cellBase = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"

Test-Pattern -Name "Item transaction journal is enabled and has a stable save root" -Text ($config + "`n" + $journal) `
    -Pattern 'config\.enableItemTransactionJournal\s*=\s*true.*config\.itemTransactionJournalRoot\s*=\s*"saves/transactions/items".*local\s+function\s+getJournalRoot\(\).*config\.itemTransactionJournalRoot.*saves/transactions/items' `
    -Missing $missing

Test-Pattern -Name "Item transaction journal writes atomic XML transaction documents" -Text $journal `
    -Pattern 'local\s+saveCodec\s*=\s*require\("communitympSaveCodec"\).*local\s+function\s+makeEventPath\(event\).*os\.date\("!%Y%m%d".*saveCodec\.exists\(relativePath\).*function\s+itemTransactionJournal\.record\(event\).*config\.enableItemTransactionJournal\s*==\s*false.*saveCodec\.save\(relativePath,\s*"item-transaction",\s*event' `
    -Missing $missing

Test-Pattern -Name "Player inventory snapshots and deltas are journaled before mutation" -Text $playerBase `
    -Pattern 'local\s+itemTransactionJournal\s*=\s*require\("communitymp\.saves\.itemTransactionJournal"\).*function\s+BasePlayer:SaveInventory\(playerPacket\).*action\s*==\s*enumerations\.inventory\.SET.*recordPlayerInventoryChange\(self,\s*action,\s*playerPacket\.inventory,\s*\{.*fullSnapshot\s*=\s*true.*self\.data\.inventory\s*=\s*\{\}.*recordPlayerInventoryChange\(self,\s*action,\s*\{\s*item\s*\}.*inventoryHelper\.addItem\(self\.data\.inventory.*recordPlayerInventoryChange\(self,\s*action,\s*\{\s*journalItem\s*\}.*inventoryHelper\.removeClosestItem\(self\.data\.inventory' `
    -Missing $missing

Test-Pattern -Name "Container mirrors journal player inventory effects before applying them" -Text $playerBase `
    -Pattern 'function\s+BasePlayer:ApplyContainerInventoryMirror\(inventoryAction,\s*item\).*recordPlayerInventoryChange\(self,\s*inventoryAction,\s*\{\s*mirrorItem\s*\},\s*\{.*source\s*=\s*"containerMirror".*inventoryAction\s*==\s*enumerations\.inventory\.ADD.*inventoryHelper\.addItem\(self\.data\.inventory.*inventoryAction\s*==\s*enumerations\.inventory\.REMOVE.*inventoryHelper\.removeClosestItem\(self\.data\.inventory' `
    -Missing $missing

Test-Pattern -Name "Container packets journal accepted gameplay changes before cell mutation" -Text $cellBase `
    -Pattern 'local\s+itemTransactionJournal\s*=\s*require\("communitymp\.saves\.itemTransactionJournal"\).*function\s+BaseCell:SaveContainers\(pid\).*local\s+function\s+buildContainerTransactionObjects\(\).*GetObjectListSize\(\).*GetContainerChangesSize\(objectIndex\).*GetContainerItemRefId\(objectIndex,\s*itemIndex\).*subAction\s*~=\s*enumerations\.containerSub\.REPLY_TO_REQUEST.*recordContainerPacket\(self,\s*pid,\s*action,\s*subAction,\s*packetOrigin,\s*buildContainerTransactionObjects\(\)' `
    -Missing $missing

Test-Pattern -Name "Ground object place/delete packets journal and fail closed on journal failure" -Text ($cellBase + "`n" + $eventHandler) `
    -Pattern 'function\s+BaseCell:SaveObjectsByPacketType\(packetType,\s*objects,\s*pid\).*packetType\s*==\s*"ObjectPlace"\s+or\s+packetType\s*==\s*"ObjectDelete".*recordObjectPacket\(self,\s*pid,\s*packetType,\s*objects.*return\s+false.*return\s+true.*SaveObjectsByPacketType\(packetType,\s*objects,\s*pid\).*if\s+savedObjects\s*~=\s*false\s+then.*LoadObjectsByPacketType\(packetType,\s*pid,\s*objects.*item transaction journal could not be saved' `
    -Missing $missing

Write-Host "TES3MP item transaction journal check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 6"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
