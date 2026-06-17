StateHelper = class("StateHelper")

local maxJournalChangesPerPacket = 3000
local maxTopicChangesPerPacket = 3000
local maxFactionChangesPerPacket = 3000
local maxClientGlobalsPerPacket = 3000
local maxMapTileChangesPerPacket = 3000
local maxJournalRevisionLogEntries = 3000
local maxTopicRevisionLogEntries = 3000

local function GetJournalQuestKey(journalItem)
    if type(journalItem) ~= "table" then
        return ""
    end

    return string.lower(tostring(journalItem.quest or ""))
end

local function GetJournalEntryKey(journalItem)
    if type(journalItem) ~= "table" then
        return nil
    end

    return GetJournalQuestKey(journalItem) .. ":" .. tostring(journalItem.index or "")
end

local function GetJournalStateKey(journalItem)
    if type(journalItem) ~= "table" or
        (journalItem.type ~= enumerations.journal.INDEX and journalItem.type ~= enumerations.journal.FINISHED) then
        return nil
    end

    return tostring(journalItem.type) .. ":" .. GetJournalQuestKey(journalItem)
end

local function EnsureRevisionTable(stateObject, key)
    if type(stateObject.data[key]) ~= "table" then
        stateObject.data[key] = {}
    end

    if type(stateObject.data[key].revision) ~= "number" then
        stateObject.data[key].revision = tonumber(stateObject.data[key].revision) or 0
    end

    return stateObject.data[key]
end

local function AppendRevisionLogEntries(stateObject, metadataKey, logKey, entries, maxEntries)
    if type(stateObject) ~= "table" or type(stateObject.data) ~= "table" or
        type(entries) ~= "table" then
        return 0
    end

    local metadata = EnsureRevisionTable(stateObject, metadataKey)

    if type(stateObject.data[logKey]) ~= "table" then
        stateObject.data[logKey] = {}
    end

    local changeLog = stateObject.data[logKey]
    local acceptedCount = 0

    for _, entry in ipairs(entries) do
        if type(entry) == "table" then
            metadata.revision = metadata.revision + 1

            local logEntry = tableHelper.deepCopy(entry)
            logEntry.revision = metadata.revision
            table.insert(changeLog, logEntry)

            acceptedCount = acceptedCount + 1
        end
    end

    if acceptedCount > 0 then
        metadata.lastUpdated = os.time()

        while #changeLog > maxEntries do
            table.remove(changeLog, 1)
        end
    end

    return acceptedCount
end

local function RevisionLogCoversRevision(stateObject, metadataKey, logKey, revision)
    if type(stateObject) ~= "table" or type(stateObject.data) ~= "table" then
        return false
    end

    local metadata = EnsureRevisionTable(stateObject, metadataKey)
    local requestedRevision = tonumber(revision) or 0

    if requestedRevision >= metadata.revision then
        return true
    end

    local changeLog = stateObject.data[logKey]
    if type(changeLog) ~= "table" or #changeLog == 0 then
        return false
    end

    local earliestRevision = tonumber(changeLog[1].revision)
    return earliestRevision ~= nil and requestedRevision >= earliestRevision - 1
end

local function NormalizeJournalItem(journalItem)
    if type(journalItem) ~= "table" then
        return
    end

    if type(journalItem.quest) == "string" then
        journalItem.quest = string.lower(journalItem.quest)
    end

    if journalItem.type == enumerations.journal.ENTRY and (journalItem.actorRefId == nil or journalItem.actorRefId == "") then
        journalItem.actorRefId = "player"
    end
end

function StateHelper:RecordJournalChanges(stateObject, journalItems)
    local entries = {}

    if type(journalItems) == "table" then
        for _, journalItem in ipairs(journalItems) do
            if type(journalItem) == "table" then
                table.insert(entries, {
                    journalItem = tableHelper.deepCopy(journalItem)
                })
            end
        end
    end

    return AppendRevisionLogEntries(stateObject, "journalMetadata", "journalChangeLog", entries,
        maxJournalRevisionLogEntries)
end

function StateHelper:GetJournalChangesSince(stateObject, revision)
    local result = {
        complete = RevisionLogCoversRevision(stateObject, "journalMetadata", "journalChangeLog", revision),
        revision = 0,
        journal = {}
    }

    if type(stateObject) ~= "table" or type(stateObject.data) ~= "table" then
        return result
    end

    local metadata = EnsureRevisionTable(stateObject, "journalMetadata")
    local requestedRevision = tonumber(revision) or 0
    result.revision = metadata.revision

    if type(stateObject.data.journalChangeLog) ~= "table" then
        return result
    end

    for _, logEntry in ipairs(stateObject.data.journalChangeLog) do
        if type(logEntry) == "table" and tonumber(logEntry.revision) ~= nil and
            logEntry.revision > requestedRevision and type(logEntry.journalItem) == "table" then
            table.insert(result.journal, tableHelper.deepCopy(logEntry.journalItem))
        end
    end

    return result
end

function StateHelper:RecordTopicChanges(stateObject, topics)
    local entries = {}

    if type(topics) == "table" then
        for _, topicId in ipairs(topics) do
            if topicId ~= nil then
                table.insert(entries, {
                    topicId = tostring(topicId)
                })
            end
        end
    end

    return AppendRevisionLogEntries(stateObject, "topicMetadata", "topicChangeLog", entries,
        maxTopicRevisionLogEntries)
end

function StateHelper:GetTopicChangesSince(stateObject, revision)
    local result = {
        complete = RevisionLogCoversRevision(stateObject, "topicMetadata", "topicChangeLog", revision),
        revision = 0,
        topics = {}
    }

    if type(stateObject) ~= "table" or type(stateObject.data) ~= "table" then
        return result
    end

    local metadata = EnsureRevisionTable(stateObject, "topicMetadata")
    local requestedRevision = tonumber(revision) or 0
    result.revision = metadata.revision

    if type(stateObject.data.topicChangeLog) ~= "table" then
        return result
    end

    for _, logEntry in ipairs(stateObject.data.topicChangeLog) do
        if type(logEntry) == "table" and tonumber(logEntry.revision) ~= nil and
            logEntry.revision > requestedRevision and logEntry.topicId ~= nil then
            table.insert(result.topics, tostring(logEntry.topicId))
        end
    end

    return result
end

local function BeginJournalLoadBatch(pid)
    tes3mp.ClearJournalChanges(pid)
    tes3mp.SetJournalChangesAreLoad(pid, true)
end

local function FinishJournalLoad(pid)
    tes3mp.SendJournalChanges(pid)
    tes3mp.ClearJournalChanges(pid)
    tes3mp.SetJournalChangesAreLoad(pid, false)
    tes3mp.SendJournalChanges(pid)
    tes3mp.ClearJournalChanges(pid)
end

local function BeginTopicLoadBatch(pid)
    tes3mp.ClearTopicChanges(pid)
    tes3mp.SetTopicChangesAreLoad(pid, true)
end

local function FinishTopicLoad(pid)
    tes3mp.SendTopicChanges(pid)
    tes3mp.ClearTopicChanges(pid)
    tes3mp.SetTopicChangesAreLoad(pid, false)
    tes3mp.SendTopicChanges(pid)
    tes3mp.ClearTopicChanges(pid)
end

local function BeginFactionLoadBatch(pid, action)
    tes3mp.ClearFactionChanges(pid)
    tes3mp.SetFactionChangesAction(pid, action)
end

local function SendFactionLoadBatchIfFull(pid, action, pendingChanges)
    if pendingChanges < maxFactionChangesPerPacket then
        return pendingChanges
    end

    tes3mp.SendFactionChanges(pid)
    BeginFactionLoadBatch(pid, action)
    return 0
end

function StateHelper:LoadJournal(pid, stateObject)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    BeginJournalLoadBatch(pid)

    local pendingChanges = 0
    for index, journalItem in pairs(stateObject.data.journal) do

        NormalizeJournalItem(journalItem)

        if pendingChanges >= maxJournalChangesPerPacket then
            tes3mp.SendJournalChanges(pid)
            BeginJournalLoadBatch(pid)
            pendingChanges = 0
        end

        if journalItem.type == enumerations.journal.ENTRY then

            if journalItem.timestamp ~= nil then
                tes3mp.AddJournalEntryWithTimestamp(pid, journalItem.quest, journalItem.index, journalItem.actorRefId,
                    journalItem.timestamp.daysPassed, journalItem.timestamp.month, journalItem.timestamp.day)
            else
                tes3mp.AddJournalEntry(pid, journalItem.quest, journalItem.index, journalItem.actorRefId)
            end
        elseif journalItem.type == enumerations.journal.INDEX then
            tes3mp.AddJournalIndex(pid, journalItem.quest, journalItem.index)
        elseif journalItem.type == enumerations.journal.FINISHED then
            tes3mp.AddJournalFinished(pid, journalItem.quest, journalItem.isFinished == true)
        end

        pendingChanges = pendingChanges + 1
    end

    FinishJournalLoad(pid)
end

function StateHelper:LoadFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    local action = enumerations.faction.RANK
    BeginFactionLoadBatch(pid, action)

    local pendingChanges = 0
    for factionId, rank in pairs(stateObject.data.factionRanks) do
        pendingChanges = SendFactionLoadBatchIfFull(pid, action, pendingChanges)

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionRank(rank)
        tes3mp.AddFaction(pid)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendFactionChanges(pid)
end

function StateHelper:LoadFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    local action = enumerations.faction.EXPULSION
    BeginFactionLoadBatch(pid, action)

    local pendingChanges = 0
    for factionId, state in pairs(stateObject.data.factionExpulsion) do
        pendingChanges = SendFactionLoadBatchIfFull(pid, action, pendingChanges)

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionExpulsionState(state)
        tes3mp.AddFaction(pid)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendFactionChanges(pid)
end

function StateHelper:LoadFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    local action = enumerations.faction.REPUTATION
    BeginFactionLoadBatch(pid, action)

    local pendingChanges = 0
    for factionId, reputation in pairs(stateObject.data.factionReputation) do
        pendingChanges = SendFactionLoadBatchIfFull(pid, action, pendingChanges)

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionReputation(reputation)
        tes3mp.AddFaction(pid)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendFactionChanges(pid)
end

function StateHelper:LoadTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    BeginTopicLoadBatch(pid)

    local pendingChanges = 0
    for index, topicId in pairs(stateObject.data.topics) do

        if pendingChanges >= maxTopicChangesPerPacket then
            tes3mp.SendTopicChanges(pid)
            BeginTopicLoadBatch(pid)
            pendingChanges = 0
        end

        tes3mp.AddTopic(pid, topicId)
        pendingChanges = pendingChanges + 1
    end

    FinishTopicLoad(pid)
end

function StateHelper:LoadBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.bounty == nil then
        stateObject.data.fame.bounty = 0
    end

    -- Update old player files to the new format
    if stateObject.data.stats ~= nil and stateObject.data.stats.bounty ~= nil then
        stateObject.data.fame.bounty = stateObject.data.stats.bounty
        stateObject.data.stats.bounty = nil
    end

    tes3mp.SetBounty(pid, stateObject.data.fame.bounty)
    tes3mp.SendBounty(pid)
end

function StateHelper:LoadReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.reputation == nil then
        stateObject.data.fame.reputation = 0
    end

    tes3mp.SetReputation(pid, stateObject.data.fame.reputation)
    tes3mp.SendReputation(pid)
end

function StateHelper:LoadClientScriptVariables(pid, stateObject)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    local variableCount = 0
    local pendingChanges = 0

    tes3mp.ClearClientGlobals()

    for variableId, variableTable in pairs(stateObject.data.clientVariables.globals) do

        if type(variableTable) == "table" then
            if pendingChanges >= maxClientGlobalsPerPacket then
                tes3mp.SendClientScriptGlobal(pid)
                tes3mp.ClearClientGlobals()
                pendingChanges = 0
            end

            if variableTable.variableType == enumerations.variableType.SHORT then
                tes3mp.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.SHORT)
            elseif variableTable.variableType == enumerations.variableType.LONG then
                tes3mp.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.LONG)
            elseif variableTable.variableType == enumerations.variableType.FLOAT then
                tes3mp.AddClientGlobalFloat(variableId, variableTable.floatValue)
            end

            variableCount = variableCount + 1
            pendingChanges = pendingChanges + 1
        end
    end

    if variableCount > 0 then
        tes3mp.SendClientScriptGlobal(pid)
    end
end

function StateHelper:LoadDestinationOverrides(pid, stateObject)

    if stateObject.data.destinationOverrides == nil then
        stateObject.data.destinationOverrides = {}
    end

    local destinationCount = 0

    tes3mp.ClearDestinationOverrides()

    for oldCellDescription, newCellDescription in pairs(stateObject.data.destinationOverrides) do

        tes3mp.AddDestinationOverride(oldCellDescription, newCellDescription)
        destinationCount = destinationCount + 1
    end

    if destinationCount > 0 then
        tes3mp.SendWorldDestinationOverride(pid)
    end
end

function StateHelper:LoadMap(pid, stateObject)

    if stateObject.data.mapExplored == nil then
        stateObject.data.mapExplored = {}
    end

    local tileCount = 0
    local pendingChanges = 0
    tes3mp.ClearMapChanges()

    for index, cellDescription in pairs(stateObject.data.mapExplored) do

        local filePath = config.dataPath .. "/map/" .. cellDescription .. ".png"

        if tes3mp.DoesFilePathExist(filePath) then

            local cellX, cellY
            _, _, cellX, cellY = string.find(cellDescription, patterns.exteriorCell)
            cellX = tonumber(cellX)
            cellY = tonumber(cellY)

            if type(cellX) == "number" and type(cellY) == "number" then
                if pendingChanges >= maxMapTileChangesPerPacket then
                    tes3mp.SendWorldMap(pid)
                    tes3mp.ClearMapChanges()
                    pendingChanges = 0
                end

                tes3mp.LoadMapTileImageFile(cellX, cellY, filePath)
                tileCount = tileCount + 1
                pendingChanges = pendingChanges + 1
            end
        end
    end

    if tileCount > 0 then
        tes3mp.SendWorldMap(pid)
    end
end

function StateHelper:SaveJournal(stateObject, playerPacket)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    if stateObject.data.customVariables == nil then
        stateObject.data.customVariables = {}
    end

    local journalItems = stateObject.data.journal
    local entryKeys = {}
    local stateReplacementKeys = {}
    local pendingStateByKey = {}
    local pendingItems = {}

    for _, storedItem in ipairs(journalItems) do
        if type(storedItem) == "table" and storedItem.type == enumerations.journal.ENTRY then
            entryKeys[GetJournalEntryKey(storedItem)] = true
        end
    end

    for _, journalItem in ipairs(playerPacket.journal or {}) do

        NormalizeJournalItem(journalItem)

        if journalItem.type == enumerations.journal.ENTRY then
            local entryKey = GetJournalEntryKey(journalItem)

            if entryKeys[entryKey] ~= true then
                entryKeys[entryKey] = true
                table.insert(pendingItems, { active = true, item = tableHelper.deepCopy(journalItem) })
            end
        elseif journalItem.type == enumerations.journal.INDEX or journalItem.type == enumerations.journal.FINISHED then
            local stateKey = GetJournalStateKey(journalItem)
            stateReplacementKeys[stateKey] = true

            if pendingStateByKey[stateKey] ~= nil then
                pendingStateByKey[stateKey].active = false
            end

            local pendingItem = { active = true, item = tableHelper.deepCopy(journalItem) }
            pendingStateByKey[stateKey] = pendingItem
            table.insert(pendingItems, pendingItem)
        else
            table.insert(pendingItems, { active = true, item = tableHelper.deepCopy(journalItem) })
        end

        if GetJournalQuestKey(journalItem) == "a1_1_findspymaster" and journalItem.index ~= nil and journalItem.index >= 14 then
            stateObject.data.customVariables.deliveredCaiusPackage = true
        end
    end

    if next(stateReplacementKeys) ~= nil then
        local compactedItems = {}

        for _, storedItem in ipairs(journalItems) do
            local stateKey = GetJournalStateKey(storedItem)

            if stateKey == nil or stateReplacementKeys[stateKey] ~= true then
                table.insert(compactedItems, storedItem)
            end
        end

        stateObject.data.journal = compactedItems
        journalItems = stateObject.data.journal
    end

    local acceptedJournalItems = {}

    for _, pendingItem in ipairs(pendingItems) do
        if pendingItem.active then
            table.insert(journalItems, pendingItem.item)
            table.insert(acceptedJournalItems, pendingItem.item)
        end
    end

    self:RecordJournalChanges(stateObject, acceptedJournalItems)
    stateObject:QuicksaveToDrive()

    return acceptedJournalItems
end

function StateHelper:SaveFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionRanks[factionId] = tes3mp.GetFactionRank(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionExpulsion[factionId] = tes3mp.GetFactionExpulsionState(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionReputation[factionId] = tes3mp.GetFactionReputation(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    local knownTopics = {}
    local acceptedTopics = {}

    for _, topicId in ipairs(stateObject.data.topics) do
        knownTopics[topicId] = true
    end

    for i = 0, tes3mp.GetTopicChangesSize(pid) - 1 do

        local topicId = tes3mp.GetTopicId(pid, i)

        if knownTopics[topicId] ~= true then
            table.insert(stateObject.data.topics, topicId)
            knownTopics[topicId] = true
            table.insert(acceptedTopics, topicId)
        end
    end

    self:RecordTopicChanges(stateObject, acceptedTopics)
    stateObject:QuicksaveToDrive()

    return acceptedTopics
end

function StateHelper:SaveBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.bounty = tes3mp.GetBounty(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.reputation = tes3mp.GetReputation(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveClientScriptGlobal(stateObject, variables)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    for id, variable in pairs (variables) do
        stateObject.data.clientVariables.globals[id] = variable
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveMapExploration(pid, stateObject)

    local cell = tes3mp.GetCell(pid)

    if tes3mp.IsInExterior(pid) == true then
        if not tableHelper.containsValue(stateObject.data.mapExplored, cell) then
            table.insert(stateObject.data.mapExplored, cell)
        end
    end
end

return StateHelper
