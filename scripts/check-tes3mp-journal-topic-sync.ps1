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

$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$journalProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerJournal.hpp"
$topicProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerTopic.hpp"
$dialogueExtensions = Get-SourceText "apps\openmw\mwscript\dialogueextensions.cpp"
$luaPlayer = Get-SourceText "apps\openmw\mwlua\types\player.cpp"
$dialogueManager = Get-SourceText "apps\openmw\mwdialogue\dialoguemanagerimp.cpp"
$basePlayerHeader = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$topicPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerTopic.cpp"
$dialogueFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Dialogue.cpp"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$packetBuilder = Get-SourceText "files\tes3mp\server\scripts\packetBuilder.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$defaultCommands = Get-SourceText "files\tes3mp\server\scripts\defaultCommands.lua"
$stateHelper = Get-SourceText "files\tes3mp\server\scripts\stateHelper.lua"
$serverConfig = Get-SourceText "files\tes3mp\server\scripts\config.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Client applies server journal ENTRY, INDEX, and FINISHED packets to local journal state" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::addJournalItems\(\)\s*\{.*const\s+bool\s+showJournalMessage\s*=\s*!journalChangesAreLoad;.*journalChangesAreLoad\s*&&\s*!mApplyingServerJournalLoad.*getJournal\(\)->clear\(\).*journalItem\.type\s*==\s*JournalItem::ENTRY.*getJournal\(\)->addEntry\(\s*stringRefId\(journalItem\.quest\),\s*journalItem\.index,\s*ptrFound,\s*showJournalMessage\).*journalItem\.type\s*==\s*JournalItem::INDEX.*getJournal\(\)->setJournalIndex\(stringRefId\(journalItem\.quest\),\s*journalItem\.index\).*journalItem\.type\s*==\s*JournalItem::FINISHED.*getOrStartQuest\(stringRefId\(journalItem\.quest\)\)\.setFinished\(\s*journalItem\.isFinished\).*journalChangesAreLoad\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Client sends journal entries with original in-game timestamp through PlayerJournal" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendJournalEntry\(const\s+std::string&\s+quest,\s*int\s+index,\s*const\s+MWWorld::Ptr&\s+actor\)\s*\{.*journalItem\.type\s*=\s*JournalItem::ENTRY;.*journalItem\.actorRefId\s*=.*journalItem\.hasTimestamp\s*=\s*true;.*timestamp\.daysPassed\s*=\s*world->getGlobalInt\(MWWorld::Globals::sDaysPassed\);.*timestamp\.month\s*=\s*world->getGlobalInt\(MWWorld::Globals::sMonth\);.*timestamp\.day\s*=\s*world->getGlobalInt\(MWWorld::Globals::sDay\);.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Client sends journal index and finished changes through PlayerJournal" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendJournalIndex\(const\s+std::string&\s+quest,\s*int\s+index\)\s*\{.*journalItem\.type\s*=\s*JournalItem::INDEX;.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->Send\(\);.*void\s+LocalPlayer::sendJournalFinished\(const\s+std::string&\s+quest,\s*bool\s+isFinished\)\s*\{.*journalItem\.type\s*=\s*JournalItem::FINISHED;.*journalItem\.isFinished\s*=\s*isFinished;.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_JOURNAL\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "Client applies server topics under a guard and translates topic keywords for local state" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::addTopics\(\)\s*\{.*topicChangesAreLoad\s*&&\s*!mApplyingServerTopicLoad.*getDialogueManager\(\)->clear\(\).*ServerTopicChangeGuard.*guard\(mApplyingServerTopicChanges\);.*topicId\s*=\s*std::string\(env\.getWindowManager\(\)->getTranslationDataStorage\(\)\.topicKeyword\(topicId\)\);.*env\.getDialogueManager\(\)->addTopic\(stringRefId\(topicId\)\);.*topicChangesAreLoad\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Client sends topics in standard form through PlayerTopic" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::sendTopic\(const\s+std::string&\s+topicId\)\s*\{.*topic\.topicId\s*=\s*std::string\(MWBase::Environment::get\(\)\.getWindowManager\(\)->getTranslationDataStorage\(\)\.topicStandardForm\(topicId\)\);.*getPlayerPacket\(ID_PLAYER_TOPIC\)->setPlayer\(this\);.*getPlayerPacket\(ID_PLAYER_TOPIC\)->Send\(\);' `
    -Missing $missing

Test-Pattern -Name "PlayerJournal processor ignores whole-journal requests and applies server deltas" -Text $journalProcessor `
    -Pattern 'if\s*\(isRequest\(\)\)\s*\{.*Entire journal cannot currently be requested from players.*\}\s*else\s+if\s*\(player\s*!=\s*0\)\s*\{.*static_cast<LocalPlayer\*>\(player\)->addJournalItems\(\);' `
    -Missing $missing

Test-Pattern -Name "PlayerTopic processor ignores whole-topic requests and applies server deltas" -Text $topicProcessor `
    -Pattern 'if\s*\(isRequest\(\)\)\s*\{.*Entire list of topics cannot currently be requested from players.*\}\s*else\s+if\s*\(player\s*!=\s*0\)\s*\{.*static_cast<LocalPlayer\*>\(player\)->addTopics\(\);' `
    -Missing $missing

Test-Pattern -Name "MWScript Journal and SetJournalIndex route through TES3MP packets before local fallback" -Text $dialogueExtensions `
    -Pattern 'if\s*\(sendTes3mpJournalEntry\(quest,\s*index,\s*ptr\)\)\s*return;.*MWBase::Environment::get\(\)\.getJournal\(\)->addEntry\(quest,\s*index,\s*ptr\);.*if\s*\(sendTes3mpJournalIndex\(quest,\s*index\)\)\s*return;.*MWBase::Environment::get\(\)\.getJournal\(\)->setJournalIndex\(quest,\s*index\);' `
    -Missing $missing

Test-Pattern -Name "OpenMW Lua quest mutations route through TES3MP journal packets before local fallback" -Text $luaPlayer `
    -Pattern 'if\s*\(sendTes3mpLuaJournalIndexPacket\(self\.mQuestId,\s*stage\)\)\s*return;.*getJournal\(\)->setJournalIndex\(self\.mQuestId,\s*stage\);.*if\s*\(sendTes3mpLuaJournalFinishedPacket\(q\.mQuestId,\s*finished\)\)\s*return;.*getOrStartQuest\(q\.mQuestId\)\.setFinished\(finished\);.*if\s*\(sendTes3mpLuaJournalEntryPacket\(q\.mQuestId,\s*stage,\s*actorPtr\)\)\s*return;.*getJournal\(\)->addEntry\(q\.mQuestId,\s*stage,\s*actorPtr\);' `
    -Missing $missing

Test-Pattern -Name "Dialogue topic discovery sends new local topics only when not applying server topic changes" -Text $dialogueManager `
    -Pattern 'void\s+DialogueManager::addTopic\(const\s+ESM::RefId&\s+topic\)\s*\{.*const\s+bool\s+inserted\s*=\s*mKnownTopics\.insert\(topic\)\.second;.*if\s*\(inserted\s*&&\s*mwmp::Main::isInitialized\(\)\).*localPlayer\s*!=\s*nullptr\s*&&\s*!localPlayer->isApplyingServerTopicChanges\(\)\s*&&\s*localPlayer->canSendJournalChanges\(\).*localPlayer->sendTopic\(topic\.serializeText\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Server journal handler preserves shared echo and personal sender echo semantics" -Text $eventHandler `
    -Pattern 'eventHandler\.OnPlayerJournal\s*=\s*function\(pid\).*local\s+playerPacket\s*=\s*packetReader\.GetPlayerPacketTables\(pid,\s*"PlayerJournal"\).*if\s+config\.shareJournal\s*==\s*true\s+then\s+local\s+acceptedJournalItems\s*=\s*WorldInstance:SaveJournal\(playerPacket\)\s*or\s*\{\}.*sendJournalDeltaToPlayer\(pid,\s*acceptedJournalItems,\s*true,\s*false\).*else\s+local\s+acceptedJournalItems\s*=\s*Players\[pid\]:SaveJournal\(playerPacket\)\s*or\s*\{\}.*sendJournalDeltaToPlayer\(pid,\s*acceptedJournalItems,\s*false,\s*false\).*shareJournalWithOnlineAllies\(Players\[pid\],\s*acceptedJournalItems\)' `
    -Missing $missing

Test-Pattern -Name "Server shares accepted journal deltas and quest globals with online allies" -Text ($serverConfig + "`n" + $packetBuilder + "`n" + $eventHandler) `
    -Pattern 'config\.shareJournalWithAllies\s*=\s*true.*packetBuilder\.AddPlayerJournalItem\s*=\s*function\(pid,\s*journalItem\).*tes3mp\.AddJournalEntryWithTimestamp\(pid,\s*journalItem\.quest,\s*journalItem\.index,\s*actorRefId,.*tes3mp\.AddJournalIndex\(pid,\s*journalItem\.quest,\s*journalItem\.index\).*tes3mp\.AddJournalFinished\(pid,\s*journalItem\.quest,\s*journalItem\.isFinished\s*==\s*true\).*local\s+function\s+getOnlineAllies\(player\).*logicHandler\.GetLoggedInPlayerByStorageKey\(allyKey\).*local\s+function\s+sendJournalDeltaToPlayer\(pid,\s*journalItems,\s*sendToOtherPlayers,\s*skipAttachedPlayer\).*packetBuilder\.AddPlayerJournalItem\(pid,\s*journalItem\).*tes3mp\.SendJournalChanges\(pid,\s*sendToOtherPlayers\s*==\s*true,\s*skipAttachedPlayer\s*==\s*true\).*local\s+function\s+shareJournalWithOnlineAllies\(player,\s*journalItems\).*config\.shareJournalWithAllies\s*~=\s*true.*local\s+acceptedJournalItems\s*=\s*ally:SaveJournal\(\{\s*journal\s*=\s*journalItems\s*\}\)\s*or\s*\{\}.*sendJournalDeltaToPlayer\(ally\.pid,\s*acceptedJournalItems,\s*false,\s*false\).*local\s+function\s+shareQuestGlobalsWithOnlineAllies\(player,\s*variables\).*ally:SaveClientScriptGlobal\(variables\).*sendClientScriptGlobalsToPlayer\(ally\.pid,\s*variables\).*isAllyQuestSync\s*=\s*config\.shareJournal\s*~=\s*true\s+and\s+config\.shareJournalWithAllies\s*==\s*true\s+and\s*isQuestVariable.*shareQuestGlobalsWithOnlineAllies\(Players\[pid\],\s*allyQuestVariables\)' `
    -Missing $missing

Test-Pattern -Name "Online allies reconcile missed quest state on login and alliance acceptance" -Text ($playerBase + "`n" + $defaultCommands) `
    -Pattern 'local\s+function\s+advanceJournalRevision\(player,\s*changeCount\).*local\s+function\s+recordJournalChanges\(player,\s*journalItems\).*stateHelper\.RecordJournalChanges.*advanceJournalRevision\(player,\s*#journalItems\).*local\s+function\s+mergeJournalData\(targetPlayer,\s*sourcePlayer\).*local\s+targetJournal\s*=\s*targetPlayer\.data\.journal.*local\s+entryKeys\s*=\s*\{\}.*local\s+currentIndexByQuest\s*=\s*\{\}.*local\s+currentFinishedByQuest\s*=\s*\{\}.*local\s+stateReplacementKeys\s*=\s*\{\}.*local\s+pendingStateByKey\s*=\s*\{\}.*sourceIndex\s*>\s*currentIndexByQuest\[quest\].*currentFinished\s*==\s*nil\s+or\s*\(currentFinished\s*~=\s*true\s+and\s+journalItem\.isFinished\s*==\s*true\).*targetPlayer\.data\.journal\s*=\s*compactedJournal.*recordJournalChanges\(targetPlayer,\s*acceptedJournalItems\).*local\s+function\s+mergeQuestClientGlobals\(targetPlayer,\s*sourcePlayer\).*isQuestClientGlobal\(variableId\).*sourceValue\s*>\s*targetValue.*function\s+BasePlayer:FinishLogin\(\).*self:SyncQuestStateWithOnlineAllies\(\).*function\s+BasePlayer:SyncQuestStateWithOnlineAllies\(\).*config\.shareJournalWithAllies\s*~=\s*true\s+or\s+config\.shareJournal\s*==\s*true.*local\s+onlineAllies\s*=\s*\{\}.*logicHandler\.GetLoggedInPlayerByStorageKey\(otherAllyKey\).*mergeJournalData\(self,\s*otherPlayer\).*mergeQuestClientGlobals\(self,\s*otherPlayer\).*for\s+_,\s*otherPlayer\s+in\s+ipairs\(onlineAllies\)\s+do.*mergeJournalData\(otherPlayer,\s*self\).*mergeQuestClientGlobals\(otherPlayer,\s*self\).*quicksaveCharacterState\(player\).*player:LoadJournal\(\).*player:LoadClientScriptVariables\(\).*defaultCommands\.joinTeam\s*=\s*function\(pid,\s*cmd\).*Players\[pid\]:SyncQuestStateWithOnlineAllies\(\)' `
    -Missing $missing

Test-Pattern -Name "Server topic handler preserves shared broadcast skip and personal save semantics" -Text $eventHandler `
    -Pattern 'local\s+function\s+sendTopicDeltaToPlayer\(pid,\s*topics,\s*sendToOtherPlayers,\s*skipAttachedPlayer\).*tes3mp\.ClearTopicChanges\(pid\).*tes3mp\.AddTopic\(pid,\s*topicId\).*tes3mp\.SendTopicChanges\(pid,\s*sendToOtherPlayers\s*==\s*true,\s*skipAttachedPlayer\s*==\s*true\).*eventHandler\.OnPlayerTopic\s*=\s*function\(pid\).*if\s+config\.shareTopics\s*==\s*true\s+then\s+local\s+acceptedTopics\s*=\s*WorldInstance:SaveTopics\(pid\)\s*or\s*\{\}.*sendTopicDeltaToPlayer\(pid,\s*acceptedTopics,\s*true,\s*true\).*else\s+Players\[pid\]:SaveTopics\(\)' `
    -Missing $missing

Test-Pattern -Name "Default server config keeps server-wide journals and topics off while ally journal sharing is on" -Text $serverConfig `
    -Pattern 'config\.shareJournal\s*=\s*false.*config\.shareJournalWithAllies\s*=\s*true.*config\.shareTopics\s*=\s*false' `
    -Missing $missing

Test-Pattern -Name "State helper reloads timestamped journal entries and saves journal deltas through materialized indexes" -Text $stateHelper `
    -Pattern 'local\s+function\s+GetJournalEntryKey\(journalItem\).*local\s+function\s+GetJournalStateKey\(journalItem\).*local\s+function\s+AppendRevisionLogEntries\(stateObject,\s*metadataKey,\s*logKey,\s*entries,\s*maxEntries\).*function\s+StateHelper:RecordJournalChanges\(stateObject,\s*journalItems\).*journalItem\s*=\s*tableHelper\.deepCopy\(journalItem\).*"journalMetadata",\s*"journalChangeLog".*function\s+StateHelper:GetJournalChangesSince\(stateObject,\s*revision\).*journalChangeLog.*tes3mp\.AddJournalEntryWithTimestamp\(pid,\s*journalItem\.quest,\s*journalItem\.index,\s*journalItem\.actorRefId,\s*journalItem\.timestamp\.daysPassed,\s*journalItem\.timestamp\.month,\s*journalItem\.timestamp\.day\).*function\s+StateHelper:SaveJournal\(stateObject,\s*playerPacket\).*local\s+entryKeys\s*=\s*\{\}.*local\s+stateReplacementKeys\s*=\s*\{\}.*local\s+pendingStateByKey\s*=\s*\{\}.*NormalizeJournalItem\(journalItem\).*entryKeys\[entryKey\]\s*~=\s*true.*stateReplacementKeys\[stateKey\]\s*=\s*true.*pendingStateByKey\[stateKey\]\.active\s*=\s*false.*stateObject\.data\.journal\s*=\s*compactedItems.*self:RecordJournalChanges\(stateObject,\s*acceptedJournalItems\).*return\s+acceptedJournalItems' `
    -Missing $missing

Test-Pattern -Name "State helper reloads and deduplicates topic state through materialized indexes" -Text $stateHelper `
    -Pattern 'function\s+StateHelper:RecordTopicChanges\(stateObject,\s*topics\).*"topicMetadata",\s*"topicChangeLog".*function\s+StateHelper:GetTopicChangesSince\(stateObject,\s*revision\).*topicChangeLog.*function\s+StateHelper:LoadTopics\(pid,\s*stateObject\).*tes3mp\.AddTopic\(pid,\s*topicId\).*function\s+StateHelper:SaveTopics\(pid,\s*stateObject\).*local\s+knownTopics\s*=\s*\{\}.*knownTopics\[topicId\]\s*=\s*true.*local\s+topicId\s*=\s*tes3mp\.GetTopicId\(pid,\s*i\).*knownTopics\[topicId\]\s*~=\s*true.*table\.insert\(stateObject\.data\.topics,\s*topicId\).*self:RecordTopicChanges\(stateObject,\s*acceptedTopics\).*return\s+acceptedTopics' `
    -Missing $missing

Test-Pattern -Name "Journal and topic snapshot loads are explicit, clearing, and packet-capped" -Text ($basePlayerHeader + "`n" + $topicPacket + "`n" + $dialogueFunctions + "`n" + $stateHelper + "`n" + $localPlayer) `
    -Pattern 'std::vector<Topic>\s+topicChanges;\s*bool\s+topicChangesAreLoad\s*=\s*false;.*RW\(player->topicChangesAreLoad,\s*send\).*void\s+DialogueFunctions::SetTopicChangesAreLoad\(unsigned\s+short\s+pid,\s*bool\s+value\).*player->topicChangesAreLoad\s*=\s*value;.*local\s+maxJournalChangesPerPacket\s*=\s*3000.*local\s+maxTopicChangesPerPacket\s*=\s*3000.*function\s+BeginJournalLoadBatch\(pid\).*tes3mp\.SetJournalChangesAreLoad\(pid,\s*true\).*function\s+FinishJournalLoad\(pid\).*tes3mp\.SetJournalChangesAreLoad\(pid,\s*false\).*function\s+BeginTopicLoadBatch\(pid\).*tes3mp\.SetTopicChangesAreLoad\(pid,\s*true\).*function\s+FinishTopicLoad\(pid\).*tes3mp\.SetTopicChangesAreLoad\(pid,\s*false\).*pendingChanges\s*>=\s*maxJournalChangesPerPacket.*pendingChanges\s*>=\s*maxTopicChangesPerPacket' `
    -Missing $missing

Write-Host "TES3MP journal/topic sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 18"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
