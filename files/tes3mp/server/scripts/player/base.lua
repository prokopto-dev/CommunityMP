require("config")
require("patterns")
inventoryHelper = require("inventoryHelper")
stateHelper = require("stateHelper")
tableHelper = require("tableHelper")
local itemTransactionJournal = require("communitymp.saves.itemTransactionJournal")
local BasePlayer = class("BasePlayer")

local sharedAccountDataKeys = {
    login = true,
    settings = true,
    timestamps = true,
    ipAddresses = true,
    characters = true,
    accountMetadata = true,
    clientMetadata = true,
    password = true,
    passwordHash = true,
    passwordSalt = true
}

local startingOfficeReleaseJournal = {
    quest = "a1_1_findspymaster",
    index = 1,
    actorRefId = "chargen captain"
}
local startingOfficeReleaseItems = {
    { refId = "bk_A1_1_DirectionsCaiusCosades", count = 1, charge = -1, enchantmentCharge = -1, soul = "" },
    { refId = "bk_a1_1_caiuspackage", count = 1, charge = -1, enchantmentCharge = -1, soul = "" },
    { refId = "Gold_001", count = 87, charge = -1, enchantmentCharge = -1, soul = "" }
}
local startingOfficeReleaseTopics = {
    "duties",
    "Caius Cosades",
    "South Wall",
    "specific place",
    "someone in particular",
    "services",
    "my trade",
    "little secret",
    "latest rumors",
    "little advice"
}

local clientLocationChangeReasonTimeout = 10
local maxInventoryChangesPerPacket = 3000
local pendingContainerInventoryChangeTimeout = 60
local maxCooldownChangesPerPacket = 3000
local maxBookChangesPerPacket = 3000
local maxSpellbookChangesPerPacket = 3000
local maxKillChangesPerPacket = 3000
local maxActiveSpellsPerPacket = 3000
local serverLocationChangeReasons = {
    chargenSpawn = enumerations.cellChangeReason.SERVER,
    clientLocationCorrection = enumerations.cellChangeReason.SERVER,
    fallbackSpawn = enumerations.cellChangeReason.SERVER,
    loadCell = enumerations.cellChangeReason.SERVER,
    rejectClientDoorDestination = enumerations.cellChangeReason.SERVER,
    respawn = enumerations.cellChangeReason.RESPAWN,
    script = enumerations.cellChangeReason.SCRIPT,
    quest = enumerations.cellChangeReason.SCRIPT,
    questMove = enumerations.cellChangeReason.SCRIPT,
    sendLocation = enumerations.cellChangeReason.SERVER,
    teleportToPlayer = enumerations.cellChangeReason.SERVER
}

local function getServerLocationChangeReason(reason, options)
    if type(options) == "table" and type(options.cellChangeReason) == "number" then
        return options.cellChangeReason
    end

    return serverLocationChangeReasons[reason] or enumerations.cellChangeReason.SERVER
end

local function setCellChangeReason(pid, reason)
    if type(tes3mp.SetCellChangeReason) == "function" then
        tes3mp.SetCellChangeReason(pid, reason)
    end
end

local function getExteriorCellGrid(cellDescription)
    if type(cellDescription) ~= "string" then
        return nil, nil
    end

    local _, _, gridX, gridY = string.find(cellDescription, "^%s*(%-?%d+),%s*(%-?%d+)%s*$")

    if gridX ~= nil and gridY ~= nil then
        return tonumber(gridX), tonumber(gridY)
    end

    _, _, gridX, gridY = string.find(cellDescription, "%((%-?%d+),%s*(%-?%d+)%)%s*$")

    if gridX == nil or gridY == nil then
        return nil, nil
    end

    return tonumber(gridX), tonumber(gridY)
end

local function cellDescriptionsReferToSameLocation(leftDescription, rightDescription)
    if leftDescription == rightDescription then
        return true
    end

    local leftGridX, leftGridY = getExteriorCellGrid(leftDescription)
    local rightGridX, rightGridY = getExteriorCellGrid(rightDescription)

    return leftGridX ~= nil and rightGridX ~= nil and leftGridX == rightGridX and leftGridY == rightGridY
end

local function hasNonEmptyValue(inputTable, keys)
    if type(inputTable) ~= "table" then
        return false
    end

    for _, key in ipairs(keys) do
        if inputTable[key] ~= nil and inputTable[key] ~= "" then
            return true
        end
    end

    return false
end

local function hasSavedCharacterData(data)
    if type(data) ~= "table" then
        return false
    end

    if hasNonEmptyValue(data.character, { "name", "race", "head", "hair", "class", "birthsign" }) then
        return true
    end

    if hasNonEmptyValue(data, { "name", "race", "head", "hair", "class", "birthsign" }) then
        return true
    end

    if data.location ~= nil and data.location.cell ~= nil and data.location.cell ~= "" then
        return true
    end

    return type(data.inventory) == "table" and not tableHelper.isEmpty(data.inventory)
end

local function compactCharacterEntries(entries)
    local numericEntries = {}
    local namedEntries = {}

    for key, value in pairs(entries) do
        if type(key) == "number" then
            table.insert(numericEntries, {
                key = key,
                value = value
            })
        else
            namedEntries[key] = value
        end
    end

    table.sort(numericEntries, function(left, right)
        return left.key < right.key
    end)

    for key in pairs(entries) do
        entries[key] = nil
    end

    for key, value in pairs(namedEntries) do
        entries[key] = value
    end

    for _, entry in ipairs(numericEntries) do
        table.insert(entries, entry.value)
    end
end

local function getJournalQuestKey(journalItem)
    if type(journalItem) ~= "table" then
        return nil
    end

    return string.lower(tostring(journalItem.quest or ""))
end

local function hasJournalEntryAtLeast(journal, target)
    if type(journal) ~= "table" then
        return false
    end

    local targetQuest = getJournalQuestKey(target)

    for _, journalItem in pairs(journal) do
        if type(journalItem) == "table" and getJournalQuestKey(journalItem) == targetQuest and
            (journalItem.type == enumerations.journal.ENTRY or journalItem.type == enumerations.journal.INDEX) and
            (journalItem.index or 0) >= target.index then
            return true
        end
    end

    return false
end

local function sameJournalEntry(left, right)
    return left.type == enumerations.journal.ENTRY and right.type == enumerations.journal.ENTRY and
        getJournalQuestKey(left) == getJournalQuestKey(right) and left.index == right.index
end

local function getJournalEntryKey(journalItem)
    if type(journalItem) ~= "table" then
        return nil
    end

    return getJournalQuestKey(journalItem) .. ":" .. tostring(journalItem.index or "")
end

local function getJournalStateKey(journalItem)
    if type(journalItem) ~= "table" or
        (journalItem.type ~= enumerations.journal.INDEX and journalItem.type ~= enumerations.journal.FINISHED) then
        return nil
    end

    return tostring(journalItem.type) .. ":" .. getJournalQuestKey(journalItem)
end

local function advanceJournalRevision(player, changeCount)
    if changeCount <= 0 or player == nil or player.data == nil then
        return
    end

    if type(player.data.journalMetadata) ~= "table" then
        player.data.journalMetadata = {}
    end

    if type(player.data.journalMetadata.revision) ~= "number" then
        player.data.journalMetadata.revision = tonumber(player.data.journalMetadata.revision) or 0
    end

    player.data.journalMetadata.revision = player.data.journalMetadata.revision + changeCount
    player.data.journalMetadata.lastUpdated = os.time()
end

local function advanceTopicRevision(player, changeCount)
    if changeCount <= 0 or player == nil or player.data == nil then
        return
    end

    if type(player.data.topicMetadata) ~= "table" then
        player.data.topicMetadata = {}
    end

    if type(player.data.topicMetadata.revision) ~= "number" then
        player.data.topicMetadata.revision = tonumber(player.data.topicMetadata.revision) or 0
    end

    player.data.topicMetadata.revision = player.data.topicMetadata.revision + changeCount
    player.data.topicMetadata.lastUpdated = os.time()
end

local function recordJournalChanges(player, journalItems)
    if type(journalItems) ~= "table" or #journalItems == 0 then
        return
    end

    if stateHelper ~= nil and type(stateHelper.RecordJournalChanges) == "function" then
        local recordedCount = stateHelper:RecordJournalChanges(player, journalItems)

        if recordedCount ~= nil and recordedCount > 0 then
            return
        end
    end

    advanceJournalRevision(player, #journalItems)
end

local function recordTopicChanges(player, topics)
    if type(topics) ~= "table" or #topics == 0 then
        return
    end

    if stateHelper ~= nil and type(stateHelper.RecordTopicChanges) == "function" then
        local recordedCount = stateHelper:RecordTopicChanges(player, topics)

        if recordedCount ~= nil and recordedCount > 0 then
            return
        end
    end

    advanceTopicRevision(player, #topics)
end

local function mergeJournalItem(targetJournal, sourceItem)
    if type(sourceItem) ~= "table" then
        return false
    end

    if sourceItem.type == enumerations.journal.ENTRY then
        for _, storedItem in ipairs(targetJournal) do
            if sameJournalEntry(storedItem, sourceItem) then
                return false
            end
        end

        table.insert(targetJournal, tableHelper.deepCopy(sourceItem))
        return true
    end

    if sourceItem.type == enumerations.journal.INDEX or sourceItem.type == enumerations.journal.FINISHED then
        local sourceQuest = getJournalQuestKey(sourceItem)

        for index = #targetJournal, 1, -1 do
            local storedItem = targetJournal[index]

            if type(storedItem) == "table" and storedItem.type == sourceItem.type and
                getJournalQuestKey(storedItem) == sourceQuest then
                if sourceItem.type == enumerations.journal.INDEX then
                    if (sourceItem.index or 0) <= (storedItem.index or 0) then
                        return false
                    end
                elseif storedItem.isFinished == true or sourceItem.isFinished ~= true then
                    return false
                end

                table.remove(targetJournal, index)
            end
        end

        table.insert(targetJournal, tableHelper.deepCopy(sourceItem))
        return true
    end

    table.insert(targetJournal, tableHelper.deepCopy(sourceItem))
    return true
end

local function mergeJournalData(targetPlayer, sourcePlayer)
    if targetPlayer.data.journal == nil then targetPlayer.data.journal = {} end
    if sourcePlayer.data.journal == nil then return false end

    local targetJournal = targetPlayer.data.journal
    local entryKeys = {}
    local currentIndexByQuest = {}
    local currentFinishedByQuest = {}
    local stateReplacementKeys = {}
    local pendingStateByKey = {}
    local pendingItems = {}

    for _, storedItem in ipairs(targetJournal) do
        if type(storedItem) == "table" then
            if storedItem.type == enumerations.journal.ENTRY then
                entryKeys[getJournalEntryKey(storedItem)] = true
            elseif storedItem.type == enumerations.journal.INDEX then
                local quest = getJournalQuestKey(storedItem)
                local storedIndex = storedItem.index or 0

                if currentIndexByQuest[quest] == nil or storedIndex > currentIndexByQuest[quest] then
                    currentIndexByQuest[quest] = storedIndex
                end
            elseif storedItem.type == enumerations.journal.FINISHED then
                local quest = getJournalQuestKey(storedItem)

                if currentFinishedByQuest[quest] ~= true then
                    currentFinishedByQuest[quest] = storedItem.isFinished == true
                end
            end
        end
    end

    for _, journalItem in ipairs(sourcePlayer.data.journal) do
        if type(journalItem) == "table" then
            if journalItem.type == enumerations.journal.ENTRY then
                local entryKey = getJournalEntryKey(journalItem)

                if entryKeys[entryKey] ~= true then
                    entryKeys[entryKey] = true
                    table.insert(pendingItems, { active = true, item = tableHelper.deepCopy(journalItem) })
                end
            elseif journalItem.type == enumerations.journal.INDEX then
                local quest = getJournalQuestKey(journalItem)
                local sourceIndex = journalItem.index or 0

                if currentIndexByQuest[quest] == nil or sourceIndex > currentIndexByQuest[quest] then
                    currentIndexByQuest[quest] = sourceIndex

                    local stateKey = getJournalStateKey(journalItem)
                    stateReplacementKeys[stateKey] = true

                    if pendingStateByKey[stateKey] ~= nil then
                        pendingStateByKey[stateKey].active = false
                    end

                    local pendingItem = { active = true, item = tableHelper.deepCopy(journalItem) }
                    pendingStateByKey[stateKey] = pendingItem
                    table.insert(pendingItems, pendingItem)
                end
            elseif journalItem.type == enumerations.journal.FINISHED then
                local quest = getJournalQuestKey(journalItem)
                local currentFinished = currentFinishedByQuest[quest]

                if currentFinished == nil or (currentFinished ~= true and journalItem.isFinished == true) then
                    currentFinishedByQuest[quest] = journalItem.isFinished == true

                    local stateKey = getJournalStateKey(journalItem)
                    stateReplacementKeys[stateKey] = true

                    if pendingStateByKey[stateKey] ~= nil then
                        pendingStateByKey[stateKey].active = false
                    end

                    local pendingItem = { active = true, item = tableHelper.deepCopy(journalItem) }
                    pendingStateByKey[stateKey] = pendingItem
                    table.insert(pendingItems, pendingItem)
                end
            else
                table.insert(pendingItems, { active = true, item = tableHelper.deepCopy(journalItem) })
            end
        end
    end

    if next(stateReplacementKeys) ~= nil then
        local compactedJournal = {}

        for _, storedItem in ipairs(targetJournal) do
            local stateKey = getJournalStateKey(storedItem)

            if stateKey == nil or stateReplacementKeys[stateKey] ~= true then
                table.insert(compactedJournal, storedItem)
            end
        end

        targetPlayer.data.journal = compactedJournal
        targetJournal = targetPlayer.data.journal
    end

    local acceptedJournalItems = {}

    for _, pendingItem in ipairs(pendingItems) do
        if pendingItem.active then
            table.insert(targetJournal, pendingItem.item)
            table.insert(acceptedJournalItems, pendingItem.item)
        end
    end

    recordJournalChanges(targetPlayer, acceptedJournalItems)
    return #acceptedJournalItems > 0
end

local function getClientGlobalValue(variable)
    if type(variable) ~= "table" then
        return nil
    elseif variable.variableType == enumerations.variableType.SHORT or variable.variableType == enumerations.variableType.LONG then
        return variable.intValue
    elseif variable.variableType == enumerations.variableType.FLOAT then
        return variable.floatValue
    end

    return nil
end

local function isQuestClientGlobal(variableId)
    return clientVariableScopes ~= nil and clientVariableScopes.globals ~= nil and
        clientVariableScopes.globals.quest ~= nil and
        tableHelper.containsCaseInsensitiveString(clientVariableScopes.globals.quest, variableId)
end

local function mergeQuestClientGlobals(targetPlayer, sourcePlayer)
    if sourcePlayer.data.clientVariables == nil or sourcePlayer.data.clientVariables.globals == nil then
        return false
    end

    if targetPlayer.data.clientVariables == nil then targetPlayer.data.clientVariables = {} end
    if targetPlayer.data.clientVariables.globals == nil then targetPlayer.data.clientVariables.globals = {} end

    local changed = false

    for variableId, sourceVariable in pairs(sourcePlayer.data.clientVariables.globals) do
        if isQuestClientGlobal(variableId) then
            local targetVariable = targetPlayer.data.clientVariables.globals[variableId]
            local sourceValue = getClientGlobalValue(sourceVariable)
            local targetValue = getClientGlobalValue(targetVariable)

            if targetVariable == nil or (sourceValue ~= nil and targetValue ~= nil and sourceValue > targetValue) then
                targetPlayer.data.clientVariables.globals[variableId] = tableHelper.deepCopy(sourceVariable)
                changed = true
            end
        end
    end

    return changed
end

local function getCharacterSlotDisplayText(value, fallback, maxLength)
    if value == nil or value == "" then
        return fallback
    end

    local text = tostring(value):gsub("[\r\n|]", " ")

    if maxLength ~= nil and maxLength > 3 and string.len(text) > maxLength then
        text = string.sub(text, 1, maxLength - 3) .. "..."
    end

    return text
end

local function normalizeItemCharge(value)
    if value == nil or value < -1 then
        return -1
    end

    return value
end

local function isSuppressedTutorialInventoryItem(refId)
    if type(refId) ~= "string" or config == nil or type(config.suppressedTutorialInventoryItems) ~= "table" then
        return false
    end

    for _, suppressedRefId in pairs(config.suppressedTutorialInventoryItems) do
        if type(suppressedRefId) == "string" and string.lower(refId) == string.lower(suppressedRefId) then
            return true
        end
    end

    return false
end

local function removeSuppressedTutorialInventoryItems(data)
    if type(data) ~= "table" or type(data.inventory) ~= "table" then
        return false
    end

    local removedItems = false

    for index, currentItem in pairs(data.inventory) do
        if type(currentItem) == "table" and type(currentItem.refId) == "string" and
            isSuppressedTutorialInventoryItem(currentItem.refId) then
            data.inventory[index] = nil
            removedItems = true
        end
    end

    if removedItems then
        tableHelper.cleanNils(data.inventory)
    end

    return removedItems
end

local function getInventoryMatchCount(inventory, targetItem)
    local count = 0

    if type(inventory) ~= "table" then
        return count
    end

    for _, currentItem in pairs(inventory) do
        if type(currentItem) == "table" and currentItem.refId == targetItem.refId and
            normalizeItemCharge(currentItem.charge) == targetItem.charge and
            normalizeItemCharge(currentItem.enchantmentCharge) == targetItem.enchantmentCharge and
            (currentItem.soul or "") == targetItem.soul then
            count = count + (currentItem.count or 0)
        end
    end

    return count
end

local function addInventoryItem(inventory, targetItem)
    for _, currentItem in pairs(inventory) do
        if type(currentItem) == "table" and currentItem.refId == targetItem.refId and
            normalizeItemCharge(currentItem.charge) == targetItem.charge and
            normalizeItemCharge(currentItem.enchantmentCharge) == targetItem.enchantmentCharge and
            (currentItem.soul or "") == targetItem.soul then
            currentItem.count = (currentItem.count or 0) + targetItem.count
            return
        end
    end

    table.insert(inventory, {
        refId = targetItem.refId,
        count = targetItem.count,
        charge = targetItem.charge,
        enchantmentCharge = targetItem.enchantmentCharge,
        soul = targetItem.soul
    })
end

local function ensureEquippedItemsInInventory(data)
    if type(data) ~= "table" or type(data.equipment) ~= "table" then
        return
    end

    if type(data.inventory) ~= "table" then
        data.inventory = {}
    end

    local requiredItems = {}
    local requiredOrder = {}

    for _, equipmentItem in pairs(data.equipment) do
        if type(equipmentItem) == "table" and equipmentItem.refId ~= nil and equipmentItem.refId ~= "" and
            equipmentItem.count ~= nil and equipmentItem.count > 0 then
            local normalizedItem = {
                refId = equipmentItem.refId,
                count = equipmentItem.count,
                charge = normalizeItemCharge(equipmentItem.charge),
                enchantmentCharge = normalizeItemCharge(equipmentItem.enchantmentCharge),
                soul = equipmentItem.soul or ""
            }
            local key = normalizedItem.refId .. "\0" .. tostring(normalizedItem.charge) .. "\0" ..
                tostring(normalizedItem.enchantmentCharge) .. "\0" .. normalizedItem.soul

            if requiredItems[key] == nil then
                requiredItems[key] = normalizedItem
                table.insert(requiredOrder, key)
            else
                requiredItems[key].count = requiredItems[key].count + normalizedItem.count
            end
        end
    end

    for _, key in ipairs(requiredOrder) do
        local requiredItem = requiredItems[key]
        local missingCount = requiredItem.count - getInventoryMatchCount(data.inventory, requiredItem)

        if missingCount > 0 then
            addInventoryItem(data.inventory, {
                refId = requiredItem.refId,
                count = missingCount,
                charge = requiredItem.charge,
                enchantmentCharge = requiredItem.enchantmentCharge,
                soul = requiredItem.soul
            })
        end
    end
end

local function inventorySnapshotIsMissingSavedEquipment(data, inventorySnapshot)
    if type(data) ~= "table" or type(data.equipment) ~= "table" or
        type(data.inventory) ~= "table" or tableHelper.isEmpty(data.inventory) or
        type(inventorySnapshot) ~= "table" or tableHelper.isEmpty(inventorySnapshot) then
        return false
    end

    local incomingRefIds = {}

    for _, item in pairs(inventorySnapshot) do
        if type(item) == "table" and item.refId ~= nil and item.refId ~= "" and
            item.count ~= nil and item.count > 0 then
            incomingRefIds[item.refId] = true
        end
    end

    for _, equipmentItem in pairs(data.equipment) do
        if type(equipmentItem) == "table" and equipmentItem.refId ~= nil and equipmentItem.refId ~= "" and
            equipmentItem.count ~= nil and equipmentItem.count > 0 and incomingRefIds[equipmentItem.refId] ~= true then
            return true
        end
    end

    return false
end

local function getInventoryRefIdCount(inventory, refId)
    local count = 0

    if type(inventory) ~= "table" or type(refId) ~= "string" then
        return count
    end

    local normalizedRefId = string.lower(refId)

    for _, item in pairs(inventory) do
        if type(item) == "table" and type(item.refId) == "string" and
            string.lower(item.refId) == normalizedRefId and item.count ~= nil and item.count > 0 then
            count = count + item.count
        end
    end

    return count
end

local function hasInventoryForEquipmentItem(data, equipmentItem)
    if type(data) ~= "table" or type(data.inventory) ~= "table" or type(equipmentItem) ~= "table" or
        equipmentItem.refId == nil or equipmentItem.refId == "" or equipmentItem.count == nil or
        equipmentItem.count <= 0 then
        return false
    end

    return getInventoryRefIdCount(data.inventory, equipmentItem.refId) >= equipmentItem.count
end

local function normalizeContainerInventoryMirrorItem(item, countOverride)
    if type(item) ~= "table" or type(item.refId) ~= "string" or item.refId == "" then
        return nil
    end

    local count = tonumber(countOverride or item.count)

    if count == nil or count <= 0 then
        return nil
    end

    return {
        refId = item.refId,
        count = math.floor(count),
        charge = normalizeItemCharge(tonumber(item.charge) or -1),
        enchantmentCharge = normalizeItemCharge(tonumber(item.enchantmentCharge) or -1),
        soul = item.soul or ""
    }
end

local function containerInventoryMirrorItemsMatch(left, right)
    if type(left) ~= "table" or type(right) ~= "table" then
        return false
    end

    return type(left.refId) == "string" and type(right.refId) == "string" and
        string.lower(left.refId) == string.lower(right.refId) and
        normalizeItemCharge(left.charge) == normalizeItemCharge(right.charge) and
        normalizeItemCharge(left.enchantmentCharge) == normalizeItemCharge(right.enchantmentCharge) and
        (left.soul or "") == (right.soul or "")
end

local function pruneEquipmentMissingInventory(data, refId)
    local removedSlots = {}

    if type(data) ~= "table" or type(data.equipment) ~= "table" or type(refId) ~= "string" then
        return removedSlots
    end

    local normalizedRefId = string.lower(refId)

    for slot, equipmentItem in pairs(data.equipment) do
        if type(equipmentItem) == "table" and type(equipmentItem.refId) == "string" and
            string.lower(equipmentItem.refId) == normalizedRefId and
            not hasInventoryForEquipmentItem(data, equipmentItem) then
            data.equipment[slot] = nil
            removedSlots[slot] = true
        end
    end

    return removedSlots
end

local function quicksaveCharacterState(player)
    if type(player.QuicksaveToDrive) == "function" then
        player:QuicksaveToDrive()
    end
end

function BasePlayer:__init(pid, playerName)
    self.dbPid = nil

    self.data =
    {
        login = {
            name = "",
            passwordSalt = "",
            passwordHash = ""
        },
        timestamps = {
            creation = os.time(),
            lastLogin = os.time(),
            lastDisconnect = 0,
            lastFixMe = 0,
            lastSessionDuration = 0
        },
        settings = {
            staffRank = 0,
            difficulty = "default",
            consoleAllowed = "default",
            bedRestAllowed = "default",
            wildernessRestAllowed = "default",
            waitAllowed = "default",
            enforcedLogLevel = "default",
            physicsFramerate = "default"
        },
        character = {
            name = "",
            race = "",
            head = "",
            hair = "",
            gender = 1,
            class = "",
            birthsign = ""
        },
        location = {
            cell = "",
            regionName = "",
            posX = 0,
            posY = 0,
            posZ = 0,
            rotX = 0,
            rotZ = 0
        },
        stats = {
            level = 1,
            levelProgress = 0,
            healthBase = 1,
            healthCurrent = 1,
            magickaBase = 1,
            magickaCurrent = 1,
            fatigueBase = 1,
            fatigueCurrent = 1
        },
        fame = {
            bounty = 0,
            reputation = 0
        },
        miscellaneous = {
            markLocation = {
                cell = "",
                posX = 0,
                posY = 0,
                posZ = 0,
                rotX = 0,
                rotZ = 0
            },
            selectedSpell = ""
        },
        customClass = {},
        attributes = {},
        skills = {},
        equipment = {},
        inventory = {},
        spellbook = {},
        spellsActive = {},
        cooldowns = {},
        quickKeys = {},
        shapeshift = {},
        death = {
            isDead = false,
            timestamp = 0
        },
        journal = {},
        factionRanks = {},
        factionExpulsion = {},
        factionReputation = {},
        topics = {},
        books = {},
        mapExplored = {},
        ipAddresses = {},
        recordLinks = {},
        alliedPlayers = {},
        destinationOverrides = {},
        customVariables = {}
    }

    for index = 0, (tes3mp.GetAttributeCount() - 1) do
        local attributeName = tes3mp.GetAttributeName(index)
        self.data.attributes[attributeName] = {
            base = 1,
            damage = 0,
            skillIncrease = 0
        }
    end

    for index = 0, (tes3mp.GetSkillCount() - 1) do
        local skillName = tes3mp.GetSkillName(index)
        self.data.skills[skillName] = {
            base = 1,
            damage = 0,
            progress = 0
        }
    end

    if playerName == nil then
        self.accountName = tes3mp.GetName(pid)
    else
        self.accountName = playerName
    end

    self.pid = pid
    self.loggedIn = false
    self.isNewlyRegistered = false
    self.accountAuthenticated = false
    self.activeCharacterIndex = nil
    self.creatingNewCharacter = false
    self.loginTimerId = nil
    self.resurrectTimerId = nil
    self.hasAccount = nil

    self.cellsLoaded = {}
    self.summons = {}
    self.generatedRecordsReceived = {}
    self.unresolvedEnchantments = {}
    self.previousEquipment = {}
    self.consoleCommandsQueued = {}

    self.hasFinishedInitialTeleportation = false
    self.pendingServerLocationChange = nil
    self.pendingClientLocationChange = nil
    self.pendingStartingOfficeReleaseStateChanges = nil
    self.pendingContainerInventoryChanges = nil
end

function BasePlayer:Destroy()
    self:StopLoginTimer()

    if self.resurrectTimerId ~= nil then
        tes3mp.StopTimer(self.resurrectTimerId)
        self.resurrectTimerId = nil
    end

    self.loggedIn = false
    self.hasAccount = nil
    self.pendingServerLocationChange = nil
    self.pendingClientLocationChange = nil
    self.pendingStartingOfficeReleaseStateChanges = nil
    self.pendingContainerInventoryChanges = nil
end

function BasePlayer:Kick()
    self:Destroy()
    tes3mp.Kick(self.pid)
end

function BasePlayer:StopLoginTimer()
    if self.loginTimerId ~= nil then
        tes3mp.StopTimer(self.loginTimerId)
        self.loginTimerId = nil
    end
end

function BasePlayer:BeginServerLocationChange(reason, cellDescription, options)
    options = options or {}

    self.pendingServerLocationChange = {
        reason = reason or "server",
        cell = cellDescription,
        previousCell = self.data.location and self.data.location.cell or nil,
        cellChangeReason = getServerLocationChangeReason(reason, options),
        saveOnAck = options.saveOnAck == true,
        quicksaveOnAck = options.quicksaveOnAck == true,
        saveMapOnAck = options.saveMapOnAck == true
    }

    return self.pendingServerLocationChange
end

function BasePlayer:GetPendingServerLocationChange()
    return self.pendingServerLocationChange
end

function BasePlayer:ConsumeServerLocationChange(cellDescription)
    local pendingLocationChange = self.pendingServerLocationChange

    if pendingLocationChange == nil then
        return nil
    end

    if pendingLocationChange.cell ~= nil and cellDescription ~= nil and
        not cellDescriptionsReferToSameLocation(pendingLocationChange.cell, cellDescription) then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " acknowledged cell " .. cellDescription .. " while a server location change to " ..
            pendingLocationChange.cell .. " was pending; treating packet as a normal client cell change")
        self.pendingServerLocationChange = nil
        return nil
    end

    self.pendingServerLocationChange = nil
    return pendingLocationChange
end

function BasePlayer:BeginClientLocationChange(reason, sourceCellDescription, options)
    options = options or {}

    self.pendingClientLocationChange = {
        reason = reason or "client",
        sourceCell = sourceCellDescription,
        objectRefId = options.objectRefId,
        objectUniqueIndex = options.objectUniqueIndex,
        expectedCell = options.expectedCell,
        expectedPosition = tableHelper.deepCopy(options.expectedPosition),
        timestamp = os.time()
    }
end

function BasePlayer:GetPendingClientLocationChange()
    return self.pendingClientLocationChange
end

function BasePlayer:ConsumeClientLocationChange(cellDescription, previousCellDescription)
    local pendingLocationChange = self.pendingClientLocationChange

    if pendingLocationChange == nil then
        return nil
    end

    self.pendingClientLocationChange = nil

    if pendingLocationChange.timestamp ~= nil and
        os.time() - pendingLocationChange.timestamp > clientLocationChangeReasonTimeout then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " had expired client location change reason " .. pendingLocationChange.reason ..
            "; treating packet as an unreasoned client cell change")
        return nil
    end

    if pendingLocationChange.sourceCell ~= nil and previousCellDescription ~= nil and
        not cellDescriptionsReferToSameLocation(pendingLocationChange.sourceCell, previousCellDescription) then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " used client location change reason " .. pendingLocationChange.reason ..
            " from source cell " .. pendingLocationChange.sourceCell ..
            " while previous cell was " .. previousCellDescription ..
            "; treating packet as an unreasoned client cell change")
        return nil
    end

    if pendingLocationChange.expectedCell ~= nil and cellDescription ~= nil and
        not cellDescriptionsReferToSameLocation(pendingLocationChange.expectedCell, cellDescription) then
        pendingLocationChange.destinationCell = cellDescription
        pendingLocationChange.rejectionReason = "expectedDestinationMismatch"

        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " used client location change reason " .. pendingLocationChange.reason ..
            " toward " .. cellDescription .. " while expected destination was " ..
            pendingLocationChange.expectedCell .. "; rejecting client cell change")
        return nil, pendingLocationChange
    end

    pendingLocationChange.destinationCell = cellDescription
    return pendingLocationChange
end

function BasePlayer:GenerateSaltedHash(inputString)
    self.data.login.passwordSalt = tes3mp.GenerateRandomString(64)
    self.data.login.passwordHash = tes3mp.GetSHA256Hash(inputString .. self.data.login.passwordSalt)
end

-- Replace any plaintext passwords with an unpredictable serverside
-- salted hash of a predictable clientside salted hash
function BasePlayer:ConvertPlaintextPassword()
    local inputHash = tes3mp.GetSHA256Hash(self.data.login.password)
    inputHash = tes3mp.GetSHA256Hash(inputHash .. tes3mp.GetSHA256Hash(tes3mp.GetSHA256Hash(inputHash)))
    self:GenerateSaltedHash(inputHash)
    self.data.login.password = nil
end

function BasePlayer:Register(clientPasswordHash)
    self:EnsureCharacterSlots(true)

    self.loggedIn = false
    self.isNewlyRegistered = true
    self.accountAuthenticated = true
    self.creatingNewCharacter = false
    self.activeCharacterIndex = nil
    self:StopLoginTimer()
    self:GenerateSaltedHash(clientPasswordHash)
    self.data.settings.consoleAllowed = "default"
end

function BasePlayer:GetSharedAccountData()
    local sharedData = {}

    for key in pairs(sharedAccountDataKeys) do
        if self.data[key] ~= nil then
            sharedData[key] = tableHelper.deepCopy(self.data[key])
        end
    end

    return sharedData
end

function BasePlayer:RestoreSharedAccountData(sharedData)
    for key in pairs(sharedAccountDataKeys) do
        self.data[key] = nil
    end

    for key, value in pairs(sharedData) do
        self.data[key] = tableHelper.deepCopy(value)
    end

    if self.data.login == nil then
        self.data.login = {}
    end

    self.data.login.name = self.accountName or ""
end

function BasePlayer:CreateCharacterSnapshot()
    ensureEquippedItemsInInventory(self.data)
    removeSuppressedTutorialInventoryItems(self.data)

    local snapshot = tableHelper.deepCopy(self.data)

    for key in pairs(sharedAccountDataKeys) do
        snapshot[key] = nil
    end

    return snapshot
end

function BasePlayer:EnsureCharacterSlots(skipLegacySnapshot)
    if type(self.data.characters) ~= "table" then
        self.data.characters = {
            selectedIndex = nil,
            entries = {}
        }
    elseif type(self.data.characters.entries) ~= "table" then
        local entries = {}

        for key, value in pairs(self.data.characters) do
            if type(key) == "number" and type(value) == "table" then
                entries[key] = value
            end
        end

        self.data.characters = {
            selectedIndex = self.data.characters.selectedIndex,
            entries = entries
        }
    end

    compactCharacterEntries(self.data.characters.entries)

    for _, characterData in pairs(self.data.characters.entries) do
        removeSuppressedTutorialInventoryItems(characterData)
    end

    if self.data.characters.selectedIndex ~= nil and
        self.data.characters.entries[self.data.characters.selectedIndex] == nil then
        self.data.characters.selectedIndex = nil
    end

    if not skipLegacySnapshot and #self.data.characters.entries == 0 and hasSavedCharacterData(self.data) then
        table.insert(self.data.characters.entries, self:CreateCharacterSnapshot())
        self.data.characters.selectedIndex = 1
    end

    return self.data.characters.entries
end

function BasePlayer:GetCharacterSlotCount()
    return #self:EnsureCharacterSlots(false)
end

function BasePlayer:GetCharacterSlotName(characterIndex)
    local entries = self:EnsureCharacterSlots(false)
    local characterData = entries[characterIndex]

    if characterData ~= nil and characterData.character ~= nil and
        characterData.character.name ~= nil and characterData.character.name ~= "" then
        return characterData.character.name
    end

    return "Character " .. tostring(characterIndex)
end

function BasePlayer:GetCharacterStorageKey()
    local accountName = self.accountName or self.name or tostring(self.pid)
    local characterName = nil
    local characterIndex = self.activeCharacterIndex

    if self.data ~= nil then
        if self.data.character ~= nil then
            characterName = self.data.character.name
        end

        if characterIndex == nil and not self.creatingNewCharacter and self.data.characters ~= nil then
            characterIndex = self.data.characters.selectedIndex
        end
    end

    if characterIndex ~= nil then
        return accountName .. "#character:" .. tostring(characterIndex)
    elseif characterName ~= nil and characterName ~= "" and characterName ~= accountName then
        return accountName .. "#character:" .. characterName
    end

    return accountName
end

function BasePlayer:GetCellVisitKey()
    return self:GetCharacterStorageKey()
end

function BasePlayer:GetRecordLinkKey()
    return self:GetCharacterStorageKey()
end

function BasePlayer:GetCharacterSlotSummary(characterIndex)
    local entries = self:EnsureCharacterSlots(false)
    local characterData = entries[characterIndex] or {}
    local character = {}
    local stats = {}
    local location = {}
    local customClass = {}

    if type(characterData.character) == "table" then character = characterData.character end
    if type(characterData.stats) == "table" then stats = characterData.stats end
    if type(characterData.location) == "table" then location = characterData.location end
    if type(characterData.customClass) == "table" then customClass = characterData.customClass end

    local className = getCharacterSlotDisplayText(character.class, "unknown class", 22)
    if character.class == "custom" then
        className = getCharacterSlotDisplayText(customClass.name, "custom class", 22)
    end

    local cellName = location.cell
    if cellName == nil or cellName == "" then cellName = location.cellDescription end
    if cellName == nil or cellName == "" then cellName = location.regionName end

    return {
        name = getCharacterSlotDisplayText(self:GetCharacterSlotName(characterIndex), "Character " .. tostring(characterIndex), 32),
        level = getCharacterSlotDisplayText(stats.level or 1, "1", 6),
        race = getCharacterSlotDisplayText(character.race, "unknown race", 22),
        class = className,
        cell = getCharacterSlotDisplayText(cellName, "unknown cell", 42)
    }
end

function BasePlayer:GetCharacterSlotListLabel(characterIndex)
    local summary = self:GetCharacterSlotSummary(characterIndex)
    local selectedMarker = "  "

    if self.data.characters ~= nil and self.data.characters.selectedIndex == characterIndex then
        selectedMarker = "* "
    end

    return selectedMarker .. summary.name .. " | Level " .. summary.level .. " | " ..
        summary.race .. " " .. summary.class .. " | " .. summary.cell
end

function BasePlayer:GetCharacterSlotPreviewMetadata(characterIndex)
    local entries = self:EnsureCharacterSlots(false)
    local characterData = entries[characterIndex] or {}
    local character = {}

    if type(characterData.character) == "table" then character = characterData.character end

    local SanitizePreviewField = function(value)
        if value == nil then return "" end
        local sanitized = tostring(value):gsub("[\r\n\t]", " ")
        return sanitized
    end

    return table.concat({
        SanitizePreviewField(character.race),
        SanitizePreviewField(character.gender or 1),
        SanitizePreviewField(character.head),
        SanitizePreviewField(character.hair)
    }, "\t")
end

function BasePlayer:SelectCharacterSlot(characterIndex)
    local entries = self:EnsureCharacterSlots(false)
    local characterData = entries[characterIndex]

    if characterData == nil then
        return false
    end

    local sharedData = self:GetSharedAccountData()
    local defaultPlayer = BasePlayer(self.pid, self.accountName)
    self.data = defaultPlayer.data
    tableHelper.merge(self.data, tableHelper.deepCopy(characterData))
    self:RestoreSharedAccountData(sharedData)

    self.activeCharacterIndex = characterIndex
    self.creatingNewCharacter = false
    self.data.characters.selectedIndex = characterIndex
    self:NormalizeCharacterData()

    return true
end

function BasePlayer:DeleteCharacterSlot(characterIndex)
    characterIndex = tonumber(characterIndex)

    if characterIndex == nil then
        return false
    end

    characterIndex = math.floor(characterIndex)

    local entries = self:EnsureCharacterSlots(false)
    if entries[characterIndex] == nil then
        return false
    end

    local previousSelectedIndex = self.data.characters.selectedIndex or self.activeCharacterIndex

    entries[characterIndex] = nil
    compactCharacterEntries(entries)

    local nextSelectedIndex = previousSelectedIndex
    if #entries == 0 then
        nextSelectedIndex = nil
    elseif nextSelectedIndex == characterIndex then
        nextSelectedIndex = math.min(characterIndex, #entries)
    elseif nextSelectedIndex ~= nil and nextSelectedIndex > characterIndex then
        nextSelectedIndex = nextSelectedIndex - 1
    end

    self.data.characters.selectedIndex = nextSelectedIndex

    local sharedData = self:GetSharedAccountData()
    local wasAuthenticated = self.accountAuthenticated
    local wasLoggedIn = self.loggedIn
    local wasNewlyRegistered = self.isNewlyRegistered
    local wasHasAccount = self.hasAccount
    local loginTimerId = self.loginTimerId

    local defaultPlayer = BasePlayer(self.pid, self.accountName)
    self.data = defaultPlayer.data
    self:RestoreSharedAccountData(sharedData)

    self.accountAuthenticated = wasAuthenticated
    self.loggedIn = false
    self.isNewlyRegistered = wasNewlyRegistered
    self.hasAccount = wasHasAccount
    self.loginTimerId = loginTimerId
    self.activeCharacterIndex = nil
    self.creatingNewCharacter = false

    if nextSelectedIndex ~= nil then
        self:SelectCharacterSlot(nextSelectedIndex)
        self.loggedIn = wasLoggedIn
    end

    return true
end

function BasePlayer:StartNewCharacter()
    local entries = self:EnsureCharacterSlots(true)

    local sharedData = self:GetSharedAccountData()
    local defaultPlayer = BasePlayer(self.pid, self.accountName)
    self.data = defaultPlayer.data
    self:RestoreSharedAccountData(sharedData)

    self.activeCharacterIndex = #entries + 1
    self.creatingNewCharacter = true
    self.loggedIn = true
    self.isNewlyRegistered = false
    self.accountAuthenticated = true
    self:StopLoginTimer()

    tes3mp.SetCharGenStage(self.pid, 0, 4)
end

function BasePlayer:SaveActiveCharacterSlot(preserveCreatingNewCharacter)
    local entries = self:EnsureCharacterSlots(self.creatingNewCharacter)
    local wasCreatingNewCharacter = self.creatingNewCharacter == true

    if not self.creatingNewCharacter and self.activeCharacterIndex == nil and
        self.data.characters.selectedIndex ~= nil then
        self.activeCharacterIndex = self.data.characters.selectedIndex
    end

    if not self.creatingNewCharacter and self.activeCharacterIndex == nil then
        return
    end

    local targetIndex = self.activeCharacterIndex

    if targetIndex == nil or entries[targetIndex] == nil then
        targetIndex = #entries + 1
        self.activeCharacterIndex = targetIndex
    end

    self.data.characters.selectedIndex = targetIndex

    if preserveCreatingNewCharacter ~= true then
        self.creatingNewCharacter = false
    else
        self.creatingNewCharacter = wasCreatingNewCharacter
    end

    entries[targetIndex] = self:CreateCharacterSnapshot()
end

function BasePlayer:NormalizeCharacterData()
    local legacyLoginName = nil

    if self.data.login ~= nil and self.data.login.name ~= nil and self.data.login.name ~= "" then
        legacyLoginName = self.data.login.name
    end

    if self.data.login == nil then
        self.data.login = {}
    end

    if self.data.character == nil then
        self.data.character = {}
    end

    for _, key in ipairs({ "name", "race", "head", "hair", "gender", "class", "birthsign", "modelOverride" }) do
        if (self.data.character[key] == nil or self.data.character[key] == "") and
            self.data[key] ~= nil and self.data[key] ~= "" then
            self.data.character[key] = self.data[key]
        end
    end

    if (self.data.character.name == nil or self.data.character.name == "") and
        legacyLoginName ~= nil then
        self.data.character.name = legacyLoginName
    end

    if (self.data.character.name == nil or self.data.character.name == "") and
        self.accountName ~= nil and self.accountName ~= "" then
        self.data.character.name = self.accountName
    end

    self.data.login.name = self.accountName or ""

    for _, key in ipairs({ "name", "race", "head", "hair", "class", "birthsign" }) do
        if self.data.character[key] == nil then
            self.data.character[key] = ""
        end
    end

    if self.data.character.gender == nil then
        self.data.character.gender = 1
    end

    if self.data.customClass == nil then
        self.data.customClass = {}
    end

    if self.data.customClass.description == nil then
        self.data.customClass.description = ""
    end

    if self.data.customClass.specialization == nil then
        self.data.customClass.specialization = 0
    end
end

function BasePlayer:NormalizeDeathState()
    if self.data.stats == nil then
        self.data.stats = {}
    end

    if self.data.stats.healthBase == nil then self.data.stats.healthBase = 1 end
    if self.data.stats.healthCurrent == nil then self.data.stats.healthCurrent = self.data.stats.healthBase end
    if self.data.stats.fatigueBase == nil then self.data.stats.fatigueBase = 1 end
    if self.data.stats.fatigueCurrent == nil then self.data.stats.fatigueCurrent = self.data.stats.fatigueBase end

    if self.data.spellsActive == nil then
        self.data.spellsActive = {}
    end

    if self.data.shapeshift == nil then
        self.data.shapeshift = {}
    end

    if self.data.shapeshift.isWerewolf == nil then
        self.data.shapeshift.isWerewolf = false
    end

    if self.data.death == nil then
        self.data.death = {}
    end

    if self.data.death.isDead == nil then
        self.data.death.isDead = false
    elseif self.data.death.isDead == true or self.data.death.isDead == "true" or
        self.data.death.isDead == "1" or self.data.death.isDead == 1 then
        self.data.death.isDead = true
    else
        self.data.death.isDead = false
    end

    if self.data.death.timestamp == nil then
        self.data.death.timestamp = 0
    end
end

function BasePlayer:NormalizeSettings()
    if self.data.settings == nil then
        self.data.settings = {}
    end

    if self.data.settings.staffRank == nil then
        if self.data.settings.admin ~= nil then
            self.data.settings.staffRank = self.data.settings.admin
            self.data.settings.admin = nil
        else
            self.data.settings.staffRank = 0
        end
    end

    for _, key in ipairs({
        "difficulty",
        "consoleAllowed",
        "bedRestAllowed",
        "wildernessRestAllowed",
        "waitAllowed",
        "enforcedLogLevel",
        "physicsFramerate"
    }) do
        if self.data.settings[key] == nil then
            self.data.settings[key] = "default"
        end
    end
end

function BasePlayer:HasCompleteCharacter()
    self:NormalizeCharacterData()

    local hasBaseCharacter = self.data.character ~= nil and
        self.data.character.name ~= nil and self.data.character.name ~= "" and
        self.data.character.race ~= nil and self.data.character.race ~= "" and
        self.data.character.head ~= nil and self.data.character.head ~= "" and
        self.data.character.hair ~= nil and self.data.character.hair ~= "" and
        self.data.character.gender ~= nil and
        self.data.character.class ~= nil and self.data.character.class ~= "" and
        self.data.character.birthsign ~= nil and self.data.character.birthsign ~= ""

    if not hasBaseCharacter then
        return false
    end

    if self.data.character.class == "custom" then
        return self.data.customClass ~= nil and
            self.data.customClass.name ~= nil and self.data.customClass.name ~= "" and
            self.data.customClass.majorAttributes ~= nil and self.data.customClass.majorAttributes ~= "" and
            self.data.customClass.majorSkills ~= nil and self.data.customClass.majorSkills ~= "" and
            self.data.customClass.minorSkills ~= nil and self.data.customClass.minorSkills ~= ""
    end

    return true
end

function BasePlayer:GetInitialSpawn()
    if config.useInstancedSpawn == true and config.instancedSpawn ~= nil then
        local spawnUsed = tableHelper.shallowCopy(config.instancedSpawn)
        local originalCellDescription = spawnUsed.cellDescription

        if originalCellDescription ~= nil then
            spawnUsed.cellDescription = originalCellDescription .. " - Instance for " ..
                (self.name or self.accountName)
        end

        return spawnUsed
    elseif config.noninstancedSpawn ~= nil then
        return config.noninstancedSpawn
    end

    return nil
end

function BasePlayer:SendInstancedSpawnCellRecord(location)
    if config.useInstancedSpawn ~= true or config.instancedSpawn == nil or location == nil then
        return
    end

    local originalCellDescription = config.instancedSpawn.cellDescription

    if originalCellDescription == nil or originalCellDescription == "" or
        location.cellDescription == nil or location.cellDescription == "" then
        return
    end

    local instancedCellPrefix = originalCellDescription .. " - Instance for "

    if string.sub(location.cellDescription, 1, string.len(instancedCellPrefix)) ~= instancedCellPrefix then
        return
    end

    tes3mp.ClearRecords()
    tes3mp.SetRecordType(enumerations.recordType["CELL"])
    packetBuilder.AddCellRecord(location.cellDescription, {baseId = originalCellDescription})
    tes3mp.SendRecordDynamic(self.pid, false, false)
end

function BasePlayer:SendLocation(location, options)
    if location == nil or location.cellDescription == nil or location.cellDescription == "" then
        return false
    end

    options = options or {}
    self:SendInstancedSpawnCellRecord(location)
    local pendingLocationChange = self:BeginServerLocationChange(options.reason or "sendLocation",
        location.cellDescription, options)

    tes3mp.SetCell(self.pid, location.cellDescription)
    setCellChangeReason(self.pid, pendingLocationChange.cellChangeReason)

    if location.position ~= nil and location.rotation ~= nil then
        tes3mp.SetPos(self.pid, location.position[1], location.position[2], location.position[3])
        tes3mp.SetRot(self.pid, location.rotation[1], location.rotation[2])
    end

    tes3mp.SendCell(self.pid)

    if location.position ~= nil and location.rotation ~= nil then
        tes3mp.SendPos(self.pid)
    end

    return true
end

function BasePlayer:StoreLocation(location)
    if location == nil or location.cellDescription == nil or location.cellDescription == "" then
        return
    end

    if self.data.location == nil then
        self.data.location = {}
    end

    self.data.location.cell = location.cellDescription

    if location.position ~= nil and location.rotation ~= nil then
        self.data.location.posX = location.position[1]
        self.data.location.posY = location.position[2]
        self.data.location.posZ = location.position[3]
        self.data.location.rotX = location.rotation[1]
        self.data.location.rotZ = location.rotation[2]
    end
end

function BasePlayer:RestartCharacterGeneration()
    self.loggedIn = true
    self.isNewlyRegistered = false
    self:StopLoginTimer()

    tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
        " has incomplete saved character data; restarting character generation")
    self:Message("Your saved character data is incomplete, so character creation will restart.\n")
    tes3mp.SetCharGenStage(self.pid, 0, 4)
end

function BasePlayer:EnsureStartingOfficeReleaseState(spawnUsed)
    local stateChanges = {
        items = {},
        journal = false,
        topics = false
    }

    if self.creatingNewCharacter ~= true then
        return stateChanges
    end

    if type(self.data.inventory) ~= "table" then self.data.inventory = {} end
    if type(self.data.journal) ~= "table" then self.data.journal = {} end
    if type(self.data.topics) ~= "table" then self.data.topics = {} end

    for _, item in ipairs(startingOfficeReleaseItems) do
        if not inventoryHelper.containsItem(self.data.inventory, item.refId, item.charge, item.enchantmentCharge, item.soul) then
            inventoryHelper.addItem(self.data.inventory, item.refId, item.count, item.charge, item.enchantmentCharge, item.soul)
            table.insert(stateChanges.items, tableHelper.deepCopy(item))
        end
    end

    if not hasJournalEntryAtLeast(self.data.journal, startingOfficeReleaseJournal) then
        local journalItem = tableHelper.deepCopy(startingOfficeReleaseJournal)
        journalItem.type = enumerations.journal.ENTRY
        stateChanges.journal = mergeJournalItem(self.data.journal, journalItem)

        if stateChanges.journal then
            recordJournalChanges(self, { journalItem })
        end
    end

    local acceptedTopics = {}

    for _, topicId in ipairs(startingOfficeReleaseTopics) do
        if not tableHelper.containsCaseInsensitiveString(self.data.topics, topicId, false) then
            table.insert(self.data.topics, topicId)
            table.insert(acceptedTopics, topicId)
            stateChanges.topics = true
        end
    end

    recordTopicChanges(self, acceptedTopics)

    return stateChanges
end

function BasePlayer:EnsureSharedStartingOfficeReleaseState()
    local stateChanges = {
        journal = false,
        topics = false
    }

    if self.creatingNewCharacter ~= true or WorldInstance == nil or type(WorldInstance.data) ~= "table" then
        return stateChanges
    end

    if config.shareJournal == true then
        if type(WorldInstance.data.journal) ~= "table" then WorldInstance.data.journal = {} end

        if not hasJournalEntryAtLeast(WorldInstance.data.journal, startingOfficeReleaseJournal) then
            local journalItem = tableHelper.deepCopy(startingOfficeReleaseJournal)
            journalItem.type = enumerations.journal.ENTRY
            stateChanges.journal = mergeJournalItem(WorldInstance.data.journal, journalItem)

            if stateChanges.journal then
                recordJournalChanges(WorldInstance, { journalItem })
            end
        end
    end

    if config.shareTopics == true then
        if type(WorldInstance.data.topics) ~= "table" then WorldInstance.data.topics = {} end

        local acceptedTopics = {}

        for _, topicId in ipairs(startingOfficeReleaseTopics) do
            if not tableHelper.containsCaseInsensitiveString(WorldInstance.data.topics, topicId, false) then
                table.insert(WorldInstance.data.topics, topicId)
                table.insert(acceptedTopics, topicId)
                stateChanges.topics = true
            end
        end

        recordTopicChanges(WorldInstance, acceptedTopics)
    end

    return stateChanges
end

local function hasStartingOfficeReleaseStateChanges(releaseStateChanges, sharedReleaseStateChanges)
    return (releaseStateChanges ~= nil and (
        not tableHelper.isEmpty(releaseStateChanges.items) or
        releaseStateChanges.journal == true or
        releaseStateChanges.topics == true)) or
        (sharedReleaseStateChanges ~= nil and (
        sharedReleaseStateChanges.journal == true or
        sharedReleaseStateChanges.topics == true))
end

function BasePlayer:QueueStartingOfficeReleaseStateChanges(releaseStateChanges, sharedReleaseStateChanges)
    if not hasStartingOfficeReleaseStateChanges(releaseStateChanges, sharedReleaseStateChanges) then
        self.pendingStartingOfficeReleaseStateChanges = nil
        return false
    end

    releaseStateChanges = releaseStateChanges or {}
    sharedReleaseStateChanges = sharedReleaseStateChanges or {}

    self.pendingStartingOfficeReleaseStateChanges = {
        items = tableHelper.deepCopy(releaseStateChanges.items or {}),
        journal = releaseStateChanges.journal == true,
        topics = releaseStateChanges.topics == true,
        sharedJournal = sharedReleaseStateChanges.journal == true,
        sharedTopics = sharedReleaseStateChanges.topics == true
    }

    return true
end

function BasePlayer:ApplyStartingOfficeReleaseStateChanges()
    local releaseStateChanges = self.pendingStartingOfficeReleaseStateChanges

    if releaseStateChanges == nil then
        return false
    end

    self.pendingStartingOfficeReleaseStateChanges = nil

    if releaseStateChanges.sharedJournal and config.shareJournal == true and
        WorldInstance ~= nil and WorldInstance.LoadJournal ~= nil then
        WorldInstance:LoadJournal(self.pid)
    end

    if releaseStateChanges.sharedTopics and config.shareTopics == true and
        WorldInstance ~= nil and WorldInstance.LoadTopics ~= nil then
        WorldInstance:LoadTopics(self.pid)
    end

    if not tableHelper.isEmpty(releaseStateChanges.items) then
        self:LoadItemChanges(releaseStateChanges.items, enumerations.inventory.ADD)
    end

    if releaseStateChanges.journal and config.shareJournal ~= true then
        self:LoadJournal()
    end

    if releaseStateChanges.topics and config.shareTopics ~= true then
        self:LoadTopics()
    end

    return true
end

function BasePlayer:FinishLogin()

    if self.hasAccount then
        self:SaveIpAddress()

        if self.data.timestamps == nil then
            self.data.timestamps = {
                creation = os.time(),
                lastDisconnect = 0,
                lastFixMe = 0,
                lastSessionDuration = 0
            }
        end

        self.data.timestamps.lastLogin = os.time()

        self:LoadSettings()
        self:NormalizeDeathState()

        if not self:HasCompleteCharacter() then
            self:RestartCharacterGeneration()
            return false
        end

        self:LoadCharacter()
        self:LoadClass()
        self:LoadLevel()
        self:LoadAttributes()
        self:LoadSkills()
        self:LoadStatsDynamic()

        WorldInstance:LoadTime(self.pid, false)
        WorldInstance:LoadWeather(self.pid, false)

        if self.data.recordLinks == nil then self.data.recordLinks = {} end

        -- Load high priority records linked to us, then load lower priority permanent
        -- records and lower priority records linked to this player
        for priorityLevel, recordStoreTypes in ipairs(config.recordStoreLoadOrder) do
            for _, storeType in ipairs(recordStoreTypes) do
                local recordStore = RecordStores[storeType]

                if recordStore ~= nil then
                    -- Skip permanent records from high priority stores here because those
                    -- were already loaded upon first connecting to the server
                    if priorityLevel > 1 then
                        recordStore:LoadRecords(self.pid, recordStore.data.permanentRecords,
                            tableHelper.getArrayFromIndexes(recordStore.data.permanentRecords))
                    end

                    -- Load the generated records linked to us in this record store
                    if self.data.recordLinks[storeType] ~= nil then
                        recordStore:LoadGeneratedRecords(self.pid, recordStore.data.generatedRecords,
                            self.data.recordLinks[storeType])
                    end
                end
            end
        end

        self:CleanInventory()
        self:LoadInventory()
        self:LoadEquipment()
        self:CleanSpellbook()
        self:LoadSpellbook()
        self:LoadSpellsActive()
        self:LoadCooldowns()
        self:LoadQuickKeys()
        self:LoadBooks()
        self:LoadShapeshift()
        self:LoadMarkLocation()
        self:LoadSelectedSpell()
        self:LoadSelectedEnchantedItem()

        if config.shareJournal == true then
            WorldInstance:LoadJournal(self.pid)
        else
            self:LoadJournal()
        end

        if config.shareFactionRanks == true then
            WorldInstance:LoadFactionRanks(self.pid)
        else
            self:LoadFactionRanks()
        end

        if config.shareFactionExpulsion == true then
            WorldInstance:LoadFactionExpulsion(self.pid)
        else
            self:LoadFactionExpulsion()
        end

        if config.shareFactionReputation == true then
            WorldInstance:LoadFactionReputation(self.pid)
        else
            self:LoadFactionReputation()
        end

        if config.shareTopics == true then
            WorldInstance:LoadTopics(self.pid)
        else
            self:LoadTopics()
        end

        if config.shareBounty == true then
            WorldInstance:LoadBounty(self.pid)
        else
            self:LoadBounty()
        end

        if config.shareReputation == true then
            WorldInstance:LoadReputation(self.pid)
        else
            self:LoadReputation()
        end

		if config.shareKills == true then
			WorldInstance:LoadKills(self.pid)
		else
			self:LoadKills(self.pid, false)
		end

        self:LoadSpecialStates()

        if config.shareMapExploration == true then
            WorldInstance:LoadMap(self.pid)
        else
            self:LoadMap()
        end

        self:LoadClientScriptVariables()
        WorldInstance:LoadClientScriptVariables(self.pid)

        self:LoadDestinationOverrides()
        WorldInstance:LoadDestinationOverrides(self.pid)

        self:LoadAllies()

        -- Cell load callbacks require a logged-in player to initialize visitor
        -- state, authority, containers and actor lists for the saved cell.
        self.loggedIn = true
        self:StopLoginTimer()
        self:LoadCell()

        if self.data.death.isDead == true and config.playersRespawn then
            tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
                " logged in with a pending death; completing server resurrection")
            self:Resurrect()
        end

        if self.data.alliedPlayers == nil then self.data.alliedPlayers = {} end

        for _, otherAllyKey in ipairs(self.data.alliedPlayers) do
            local otherPlayer = logicHandler.GetLoggedInPlayerByStorageKey(otherAllyKey)
            if otherPlayer ~= nil then
                otherPlayer:LoadAllies()
            end
        end

        self:SyncQuestStateWithOnlineAllies()

        self:RunPlayerSpecificStartupScripts()

        customEventHooks.triggerHandlers("OnPlayerFinishLogin", customEventHooks.makeEventStatus(true, true), {self.pid})
        customEventHooks.triggerHandlers("OnPlayerAuthentified", customEventHooks.makeEventStatus(true, true), {self.pid})
        return true
    end

    return false
end

function BasePlayer:EndCharGen()
    self:SaveLogin()
    self:SaveCharacter()
    self:SaveClass(packetReader.GetPlayerPacketTables(self.pid, "PlayerClass"))
    self:SaveStatsDynamic(packetReader.GetPlayerPacketTables(self.pid, "PlayerStatsDynamic"))
    self:SaveEquipment(packetReader.GetPlayerPacketTables(self.pid, "PlayerEquipment"))
    self:SaveIpAddress()

    local spawnUsed = self:GetInitialSpawn()
    local releaseStateChanges = self:EnsureStartingOfficeReleaseState(spawnUsed)
    local sharedReleaseStateChanges = self:EnsureSharedStartingOfficeReleaseState()

    if self.hasAccount then
        self:SaveToDrive()
    else
        self:CreateAccount()
    end

    WorldInstance:LoadTime(self.pid, false)
    WorldInstance:LoadWeather(self.pid, false, true)

    -- Load lower priority permanent records
    for priorityLevel, recordStoreTypes in ipairs(config.recordStoreLoadOrder) do
        if priorityLevel > 1 then
            for _, storeType in ipairs(recordStoreTypes) do
                local recordStore = RecordStores[storeType]

                -- Load all the permanent records in this record store
                recordStore:LoadRecords(self.pid, recordStore.data.permanentRecords,
                    tableHelper.getArrayFromIndexes(recordStore.data.permanentRecords))
            end
        end
    end

    if config.shareJournal == true then
        WorldInstance:LoadJournal(self.pid)
    end

    if config.shareFactionRanks == true then
        WorldInstance:LoadFactionRanks(self.pid)
    end

    if config.shareFactionExpulsion == true then
        WorldInstance:LoadFactionExpulsion(self.pid)
    end

    if config.shareFactionReputation == true then
        WorldInstance:LoadFactionReputation(self.pid)
    end

    if config.shareTopics == true then
        WorldInstance:LoadTopics(self.pid)
    end

    if (sharedReleaseStateChanges.journal or sharedReleaseStateChanges.topics) and
        WorldInstance ~= nil and WorldInstance.QuicksaveToDrive ~= nil then
        WorldInstance:QuicksaveToDrive()
    end

	if config.shareKills == true then
		WorldInstance:LoadKills(self.pid)
	else
		self:LoadKills(self.pid, false)
	end

    local hasQueuedReleaseStateChanges = self:QueueStartingOfficeReleaseStateChanges(
        releaseStateChanges, sharedReleaseStateChanges)

    if self:SendLocation(spawnUsed, { reason = "chargenSpawn" }) then
        self:StoreLocation(spawnUsed)

        if spawnUsed.text then
            tes3mp.MessageBox(self.pid, -1, spawnUsed.text)
        end

        if spawnUsed.items then
            for _, item in pairs(spawnUsed.items) do
                inventoryHelper.addItem(self.data.inventory, item.refId, item.count, item.charge,
                    item.enchantmentCharge, item.soul)
            end
            self:LoadItemChanges(spawnUsed.items, enumerations.inventory.ADD)
        end
    elseif hasQueuedReleaseStateChanges then
        self:ApplyStartingOfficeReleaseStateChanges()
    end

    self:RunPlayerSpecificStartupScripts()
    quicksaveCharacterState(self)
end

function BasePlayer:IsLoggedIn()
    return self:HasLoadedCharacter()
end

function BasePlayer:HasAuthenticatedAccount()
    return self.accountAuthenticated == true
end

function BasePlayer:HasLoadedCharacter()
    return self.loggedIn
end

function BasePlayer:IsDead()
    self:NormalizeDeathState()
    return self.data.death.isDead == true
end

function BasePlayer:IsServerStaff()
    self:NormalizeSettings()
    return self.data.settings.staffRank > 0
end

function BasePlayer:IsServerOwner()
    self:NormalizeSettings()
    return self.data.settings.staffRank == 3
end

function BasePlayer:IsAdmin()
    self:NormalizeSettings()
    return self.data.settings.staffRank >= 2
end

function BasePlayer:IsModerator()
    self:NormalizeSettings()
    return self.data.settings.staffRank >= 1
end

function BasePlayer:AddLinkToRecord(storeType, recordId)

    if self.data.recordLinks == nil then self.data.recordLinks = {} end

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks[storeType] == nil then recordLinks[storeType] = {} end

        if not tableHelper.containsValue(recordLinks[storeType], recordId) then
            table.insert(recordLinks[storeType], recordId)
        end

        recordStore:AddLinkToPlayer(recordId, self)
        recordStore:QuicksaveToDrive()
    end
end

function BasePlayer:RemoveLinkToRecord(storeType, recordId)

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks ~= nil and recordLinks[storeType] ~= nil then

            local linkIndex = tableHelper.getIndexByValue(recordLinks[storeType], recordId)

            if linkIndex ~= nil then
                recordLinks[storeType][linkIndex] = nil
                tableHelper.cleanNils(recordLinks[storeType])
            end

            recordStore:RemoveLinkToPlayer(recordId, self)
            recordStore:QuicksaveToDrive()
        end
    end
end

function BasePlayer:GetHealthCurrent()
    self.data.stats.healthCurrent = tes3mp.GetHealthCurrent(self.pid)
    return self.data.stats.healthCurrent
end

function BasePlayer:SetHealthCurrent(health)
    self.data.stats.healthCurrent = health
    tes3mp.SetHealthCurrent(self.pid, health)
end

function BasePlayer:GetHealthBase()
    self.data.stats.healthBase = tes3mp.GetHealthBase(self.pid)
    return self.data.stats.healthBase
end

function BasePlayer:SetHealthBase(health)
    self.data.stats.healthBase = health
    tes3mp.SetHealthBase(self.pid, health)
end

function BasePlayer:HasAccount()
    return self.hasAccount
end

function BasePlayer:Message(message)
    tes3mp.SendMessage(self.pid, message, false)
end

function BasePlayer:CreateAccount()
    error("Not implemented")
end

function BasePlayer:SaveToDrive()
    error("Not implemented")
end

function BasePlayer:LoadFromDrive()
    error("Not implemented")
end

function BasePlayer:SaveLogin()
    self.data.login.name = self.accountName or ""
end

function BasePlayer:SaveIpAddress()
    if self.data.ipAddresses == nil then
        self.data.ipAddresses = {}
    end

    local ipAddress = tes3mp.GetIP(self.pid)

    if not tableHelper.containsValue(self.data.ipAddresses, ipAddress) then
        table.insert(self.data.ipAddresses, ipAddress)
    end
end

function BasePlayer:ProcessDeath()
    self:NormalizeDeathState()

    if self.data.death.isDead == true then
        return
    end

    self.data.death.isDead = true
    self.data.death.timestamp = os.time()
    self.data.stats.healthCurrent = 0

    -- Clear this player's active spell effects
    self.data.spellsActive = {}

    local deathReason = "committed suicide"

    if tes3mp.DoesPlayerHavePlayerKiller(self.pid) then
        local killerPid = tes3mp.GetPlayerKillerPid(self.pid)

        if self.pid ~= killerPid then
            deathReason = "was killed by player " .. logicHandler.GetChatName(killerPid)
        end
    else
        local killerName = tes3mp.GetPlayerKillerName(self.pid)

        if killerName ~= "" then
            deathReason = "was killed by " .. killerName
        end
    end

    local message = logicHandler.GetChatName(self.pid) .. " " .. deathReason .. ".\n"

    tes3mp.SendMessage(self.pid, message, true)

    if config.playersRespawn then
        self.resurrectTimerId = tes3mp.CreateTimerEx("OnDeathTimeExpiration",
            time.seconds(config.deathTime), "is", self.pid, self.accountName)
        tes3mp.StartTimer(self.resurrectTimerId)
    else
        tes3mp.SendMessage(self.pid, "You have died permanently.", false)
    end

    self:SaveToDrive()
end

function BasePlayer:Resurrect()
    self:NormalizeDeathState()

    local currentResurrectType = enumerations.resurrect.REGULAR
    local usedConfiguredRespawn = false

    if config.respawnAtImperialShrine == true then
        if config.respawnAtTribunalTemple == true then
            if math.random() > 0.5 then
                currentResurrectType = enumerations.resurrect.IMPERIAL_SHRINE
            else
                currentResurrectType = enumerations.resurrect.TRIBUNAL_TEMPLE
            end
        else
            currentResurrectType = enumerations.resurrect.IMPERIAL_SHRINE
        end

    elseif config.respawnAtTribunalTemple == true then
        currentResurrectType = enumerations.resurrect.TRIBUNAL_TEMPLE

    elseif config.defaultRespawn ~= nil and config.defaultRespawn.cellDescription ~= nil then
        currentResurrectType = enumerations.resurrect.REGULAR
        usedConfiguredRespawn = true

        local pendingLocationChange = self:BeginServerLocationChange("respawn", config.defaultRespawn.cellDescription)
        tes3mp.SetCell(self.pid, config.defaultRespawn.cellDescription)
        setCellChangeReason(self.pid, pendingLocationChange.cellChangeReason)

        if config.defaultRespawn.position ~= nil and config.defaultRespawn.rotation ~= nil then
            tes3mp.SetPos(self.pid, config.defaultRespawn.position[1],
                config.defaultRespawn.position[2], config.defaultRespawn.position[3])
            tes3mp.SetRot(self.pid, config.defaultRespawn.rotation[1], config.defaultRespawn.rotation[2])
        end

        tes3mp.SendCell(self.pid)

        if config.defaultRespawn.position ~= nil and config.defaultRespawn.rotation ~= nil then
            tes3mp.SendPos(self.pid)
        end
    end

    local message = "You have been revived"

    if currentResurrectType == enumerations.resurrect.IMPERIAL_SHRINE then
        message = message .. " at the nearest Imperial shrine"
    elseif currentResurrectType == enumerations.resurrect.TRIBUNAL_TEMPLE then
        message = message .. " at the nearest Tribunal temple"
    end

    message = message .. ".\n"

    -- Ensure that dying as a werewolf turns you back into your normal form
    if self.data.shapeshift.isWerewolf == true then
        self:SetWerewolfState(false)
    end

    -- Ensure that we unequip deadly items when applicable, to prevent an
    -- infinite death loop
    contentFixer.UnequipDeadlyItems(self.pid)

    tes3mp.Resurrect(self.pid, currentResurrectType)

    self.data.death.isDead = false
    self.data.death.timestamp = 0

    if self.data.stats.healthCurrent == nil or self.data.stats.healthCurrent < 1 then
        self.data.stats.healthCurrent = math.max(1, self.data.stats.healthBase or 1)
    end

    if self.data.stats.fatigueCurrent == nil or self.data.stats.fatigueCurrent < 1 then
        self.data.stats.fatigueCurrent = math.max(1, self.data.stats.fatigueBase or 1)
    end

    if self.resurrectTimerId ~= nil then
        tes3mp.StopTimer(self.resurrectTimerId)
        self.resurrectTimerId = nil
    end

    if usedConfiguredRespawn then
        self:StoreLocation({
            cellDescription = config.defaultRespawn.cellDescription,
            position = config.defaultRespawn.position,
            rotation = config.defaultRespawn.rotation
        })

        if packetReader ~= nil and type(packetReader.GetPlayerPacketTables) == "function" then
            self:SaveCell(packetReader.GetPlayerPacketTables(self.pid, "PlayerCellChange"))
        end
    end

    self:SaveToDrive()

    if config.deathPenaltyJailDays > 0 or config.bountyDeathPenalty then
        local jailTime = 0
        local resurrectionText = "You've been revived and brought back here, " ..
            "but your skills have been affected by "

        if config.bountyDeathPenalty then
            local currentBounty = tes3mp.GetBounty(self.pid)

            if currentBounty > 0 then
                jailTime = jailTime + math.floor(currentBounty / 100)
                resurrectionText = resurrectionText .. "your bounty"
            end
        end

        if config.deathPenaltyJailDays > 0 then
            if jailTime > 0 then
                resurrectionText = resurrectionText .. " and "
            end

            jailTime = jailTime + config.deathPenaltyJailDays
            resurrectionText = resurrectionText .. "your time spent incapacitated"
        end

        resurrectionText = resurrectionText .. ".\n"
        tes3mp.Jail(self.pid, jailTime, true, true, "Recovering", resurrectionText)
    end

    if config.bountyResetOnDeath then
        tes3mp.SetBounty(self.pid, 0)
        tes3mp.SendBounty(self.pid)
        self:SaveBounty()
    end

    tes3mp.SendMessage(self.pid, message, false)
end

function BasePlayer:DeleteSummons()

    if self.summons ~= nil then
        for summonUniqueIndex, summonRefId in pairs(self.summons) do
            tes3mp.LogAppend(enumerations.log.INFO, "- removing player's summon " .. summonUniqueIndex ..
                ", refId " .. summonRefId)

            local cell = logicHandler.GetCellContainingActor(summonUniqueIndex)

            if cell ~= nil then
                cell:DeleteObjectData(summonUniqueIndex)
                logicHandler.DeleteObjectForEveryone(cell.description, summonUniqueIndex)
            end
        end
    end
end

function BasePlayer:SaveDataByPacketType(packetType, playerPacket)
    if packetType == "PlayerAttribute" then
        self:SaveAttributes(playerPacket)
    elseif packetType == "PlayerSkill" then
        self:SaveSkills(playerPacket)
    elseif packetType == "PlayerLevel" then
        self:SaveLevel(playerPacket)
    elseif packetType == "PlayerShapeshift" then
        self:SaveShapeshift(playerPacket)
    elseif packetType == "PlayerStatsDynamic" then
        self:SaveStatsDynamic(playerPacket)
    elseif packetType == "PlayerEquipment" then
        self:SaveEquipment(playerPacket)
    elseif packetType == "PlayerInventory" then
        self:SaveInventory(playerPacket)
    elseif packetType == "PlayerSpellbook" then
        self:SaveSpellbook(playerPacket)
    elseif packetType == "PlayerCooldowns" then
        self:SaveCooldowns(playerPacket)
    elseif packetType == "PlayerQuickKeys" then
        self:SaveQuickKeys(playerPacket)
    end
end

function BasePlayer:LoadCharacter()
    self:NormalizeCharacterData()

    tes3mp.SetName(self.pid, self.data.character.name)
    self.name = self.data.character.name
    tes3mp.SetRace(self.pid, self.data.character.race)
    tes3mp.SetHead(self.pid, self.data.character.head)
    tes3mp.SetHair(self.pid, self.data.character.hair)
    tes3mp.SetIsMale(self.pid, self.data.character.gender)
    if self.data.character.modelOverride ~= nil then
        tes3mp.SetModel(self.pid, self.data.character.modelOverride)
    end
    tes3mp.SetBirthsign(self.pid, self.data.character.birthsign)

    tes3mp.SendBaseInfo(self.pid)
end

function BasePlayer:SaveCharacter(playerPacket)
    self:NormalizeCharacterData()

    local packetCharacter = nil

    if playerPacket ~= nil then
        packetCharacter = playerPacket.character
    end

    local race = packetCharacter ~= nil and packetCharacter.race or tes3mp.GetRace(self.pid)
    local head = packetCharacter ~= nil and packetCharacter.head or tes3mp.GetHead(self.pid)
    local hair = packetCharacter ~= nil and packetCharacter.hair or tes3mp.GetHair(self.pid)
    local birthsign = packetCharacter ~= nil and packetCharacter.birthsign or tes3mp.GetBirthsign(self.pid)
    local name = packetCharacter ~= nil and packetCharacter.name or tes3mp.GetName(self.pid)

    if name ~= nil and name ~= "" then
        self.data.character.name = name
        self.name = name
    elseif self.data.character.name ~= nil and self.data.character.name ~= "" then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent empty name data during character save; keeping saved name " .. self.data.character.name)
    else
        self.data.character.name = name
    end

    if race ~= nil and race ~= "" and head ~= nil and head ~= "" and hair ~= nil and hair ~= "" then
        self.data.character.race = race
        self.data.character.head = head
        self.data.character.hair = hair
    elseif self.data.character.race ~= nil and self.data.character.race ~= "" and
        self.data.character.head ~= nil and self.data.character.head ~= "" and
        self.data.character.hair ~= nil and self.data.character.hair ~= "" then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent incomplete race data during character save; keeping saved race " .. self.data.character.race)
    else
        self.data.character.race = race
        self.data.character.head = head
        self.data.character.hair = hair
    end

    self.data.character.gender = packetCharacter ~= nil and packetCharacter.gender or tes3mp.GetIsMale(self.pid)
    self.data.character.modelOverride = packetCharacter ~= nil and packetCharacter.modelOverride or tes3mp.GetModel(self.pid)

    if birthsign ~= nil and birthsign ~= "" then
        self.data.character.birthsign = birthsign
    elseif self.data.character.birthsign ~= nil and self.data.character.birthsign ~= "" then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent empty birthsign data during character save; keeping saved birthsign " ..
            self.data.character.birthsign)
    else
        self.data.character.birthsign = birthsign
    end
end

function BasePlayer:LoadClass()
    self:NormalizeCharacterData()

    if self.data.character.class ~= "custom" then
        tes3mp.SetDefaultClass(self.pid, self.data.character.class)
    elseif self.data.customClass ~= nil then
        tes3mp.SetClassName(self.pid, self.data.customClass.name)
        tes3mp.SetClassSpecialization(self.pid, self.data.customClass.specialization)

        if self.data.customClass.description ~= nil then
            tes3mp.SetClassDesc(self.pid, self.data.customClass.description)
        end

        local index = 0
        for value in string.gmatch(self.data.customClass.majorAttributes, patterns.commaSplit) do
            tes3mp.SetClassMajorAttribute(self.pid, index, tes3mp.GetAttributeId(value))
            index = index + 1
        end

        index = 0
        for value in string.gmatch(self.data.customClass.majorSkills, patterns.commaSplit) do
            tes3mp.SetClassMajorSkill(self.pid, index, tes3mp.GetSkillId(value))
            index = index + 1
        end

        index = 0
        for value in string.gmatch(self.data.customClass.minorSkills, patterns.commaSplit) do
            tes3mp.SetClassMinorSkill(self.pid, index, tes3mp.GetSkillId(value))
            index = index + 1
        end
    end

    tes3mp.SendClass(self.pid)
end

function BasePlayer:SaveClass(playerPacket)

    local class = playerPacket.character.class

    if playerPacket.character.defaultClassState == 0 then
        if playerPacket.customClass == nil or playerPacket.customClass.name == nil or playerPacket.customClass.name == "" then
            if self.data.character.class == "custom" and self.data.customClass ~= nil and
                self.data.customClass.name ~= nil and self.data.customClass.name ~= "" then
                tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
                    " sent an empty custom class during character save; keeping saved class " ..
                    self.data.customClass.name)
            else
                tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
                    " sent an empty custom class during character save")
            end
            return
        end

        self.data.character.class = "custom"

        for key, value in pairs(playerPacket.customClass) do
            self.data.customClass[key] = tableHelper.deepCopy(playerPacket.customClass[key])
        end
    elseif class ~= nil and class ~= "" then
        self.data.character.class = class
        self.data.customClass = {}
    elseif self.data.character.class ~= nil and self.data.character.class ~= "" then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent an empty default class during character save; keeping saved class " ..
            self.data.character.class)
    else
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent an empty default class during character save")
    end
end

function BasePlayer:LoadStatsDynamic()

    local healthBase

    if tes3mp.IsWerewolf(self.pid) then
        healthBase = self.data.shapeshift.werewolfHealthBase
    else
        healthBase = self.data.stats.healthBase
    end

    tes3mp.SetHealthBase(self.pid, healthBase)
    tes3mp.SetMagickaBase(self.pid, self.data.stats.magickaBase)
    tes3mp.SetFatigueBase(self.pid, self.data.stats.fatigueBase)
    tes3mp.SetHealthCurrent(self.pid, self.data.stats.healthCurrent)
    tes3mp.SetMagickaCurrent(self.pid, self.data.stats.magickaCurrent)
    tes3mp.SetFatigueCurrent(self.pid, self.data.stats.fatigueCurrent)

    tes3mp.SendStatsDynamic(self.pid)
end

function BasePlayer:SaveStatsDynamic(playerPacket)
    self:NormalizeDeathState()

    if self.data.death.isDead == true then
        self.data.stats.healthCurrent = 0
        return
    end

    local healthBase = playerPacket.stats.healthBase

    -- Sometimes, the player's base health gets set to 1 serverside;
    -- use this temporary fix until we figure out why
    if healthBase > 1 then

        if tes3mp.IsWerewolf(self.pid) then
            self.data.shapeshift.werewolfHealthBase = healthBase
        else
            self.data.stats.healthBase = healthBase
        end

        self.data.stats.magickaBase = playerPacket.stats.magickaBase
        self.data.stats.fatigueBase = playerPacket.stats.fatigueBase
        self.data.stats.healthCurrent = playerPacket.stats.healthCurrent
        self.data.stats.magickaCurrent = playerPacket.stats.magickaCurrent
        self.data.stats.fatigueCurrent = playerPacket.stats.fatigueCurrent

        if self.loggedIn == true and self.hasAccount == true and self.creatingNewCharacter ~= true then
            quicksaveCharacterState(self)
        end
    end
end

function BasePlayer:LoadAttributes()

    for attributeName, value in pairs(self.data.attributes) do
        local attributeId = tes3mp.GetAttributeId(attributeName)

        if type(value) == "table" then
            tes3mp.SetAttributeBase(self.pid, attributeId, value.base)
            tes3mp.SetAttributeDamage(self.pid, attributeId, value.damage)
            tes3mp.SetSkillIncrease(self.pid, attributeId, value.skillIncrease)

        -- Maintain backwards compatibility with the old way of storing skills
        elseif type(value) == "number" then
            tes3mp.SetAttributeBase(self.pid, attributeId, value)
        end
    end

    tes3mp.SendAttributes(self.pid)
end

function BasePlayer:SaveAttributes(playerPacket)

    for attributeName in pairs(self.data.attributes) do

        local attributeId = tes3mp.GetAttributeId(attributeName)
        local attribute = playerPacket.attributes[attributeName]
        local maxAttributeValue = config.maxAttributeValue

        if attributeName == "Speed" then
            maxAttributeValue = config.maxSpeedValue
        end

        if attribute.base > maxAttributeValue then
            self:LoadAttributes()

            local message = "Your base " .. attributeName .. " has exceeded the maximum allowed value " ..
                "and been reset to its last recorded one.\n"
            tes3mp.SendMessage(self.pid, message)
        elseif (attribute.base + attribute.modifier) > maxAttributeValue then
            tes3mp.ClearAttributeModifier(self.pid, attributeId)
            tes3mp.SendAttributes(self.pid)

            local message = "Your " .. attributeName .. " fortification has exceeded the maximum allowed " ..
                "value and been removed.\n"
            tes3mp.SendMessage(self.pid, message)
        else
            self.data.attributes[attributeName] = {
                base = attribute.base,
                damage = attribute.damage,
                skillIncrease = attribute.skillIncrease
            }
        end
    end
end

function BasePlayer:LoadSkills()

    for skillName, value in pairs(self.data.skills) do

        local skillId = tes3mp.GetSkillId(skillName)

        if type(value) == "table" then
            tes3mp.SetSkillBase(self.pid, skillId, value.base)
            tes3mp.SetSkillDamage(self.pid, skillId, value.damage)
            tes3mp.SetSkillProgress(self.pid, skillId, value.progress)

        -- Maintain backwards compatibility with the old way of storing skills
        elseif type(value) == "number" then
            tes3mp.SetSkillBase(self.pid, skillId, value)
        end
    end

    tes3mp.SendSkills(self.pid)
end

function BasePlayer:SaveSkills(playerPacket)

    for skillName in pairs(self.data.skills) do

        local skillId = tes3mp.GetSkillId(skillName)
        local skill = playerPacket.skills[skillName]
        local maxSkillValue = config.maxSkillValue

        if skillName == "Acrobatics" then
            maxSkillValue = config.maxAcrobaticsValue
        end

        if skill.base > maxSkillValue then
            self:LoadSkills()

            local message = "Your base " .. skillName .. " has exceeded the maximum allowed value " ..
                "and been reset to its last recorded one.\n"
            tes3mp.SendMessage(self.pid, message)
        elseif (skill.base + skill.modifier) > maxSkillValue and not config.ignoreModifierWithMaxSkill then
            tes3mp.ClearSkillModifier(self.pid, skillId)
            tes3mp.SendSkills(self.pid)

            local message = "Your " .. skillName .. " fortification has exceeded the maximum allowed " ..
                "value and been removed.\n"
            tes3mp.SendMessage(self.pid, message)
        else
            self.data.skills[skillName] = {
                base = skill.base,
                damage = skill.damage,
                progress = skill.progress
            }
        end
    end
end

function BasePlayer:LoadLevel()

    if self.data.stats.level == nil then self.data.stats.level = 1 end
    if self.data.stats.levelProgress == nil then self.data.stats.levelProgress = 0 end

    tes3mp.SetLevel(self.pid, self.data.stats.level)
    tes3mp.SetLevelProgress(self.pid, self.data.stats.levelProgress)
    tes3mp.SendLevel(self.pid)
end

function BasePlayer:SaveLevel(playerPacket)
    self.data.stats.level = playerPacket.stats.level
    self.data.stats.levelProgress = playerPacket.stats.levelProgress
end

function BasePlayer:LoadShapeshift()

    if self.data.shapeshift == nil then self.data.shapeshift = {} end
    if self.data.shapeshift.scale == nil then self.data.shapeshift.scale = 1 end
    if self.data.shapeshift.isWerewolf == nil then self.data.shapeshift.isWerewolf = false end
    if self.data.shapeshift.creatureRefId == nil then self.data.shapeshift.creatureRefId = "" end
    if self.data.shapeshift.displayCreatureName == nil then self.data.shapeshift.displayCreatureName = false end

    tes3mp.SetScale(self.pid, self.data.shapeshift.scale)
    tes3mp.SetWerewolfState(self.pid, self.data.shapeshift.isWerewolf)
    tes3mp.SetCreatureRefId(self.pid, self.data.shapeshift.creatureRefId)
    tes3mp.SetCreatureNameDisplayState(self.pid, self.data.shapeshift.displayCreatureName)
    tes3mp.SendShapeshift(self.pid)
end

function BasePlayer:SaveShapeshift(playerPacket)

    if self.data.shapeshift == nil then self.data.shapeshift = {} end

    local newScale = playerPacket.shapeshift.scale

    if newScale ~= self.data.shapeshift.scale then
        tes3mp.LogMessage(enumerations.log.INFO, "Player " .. logicHandler.GetChatName(self.pid) ..
            " has changed their scale to " .. newScale)
        self.data.shapeshift.scale = newScale
    end

    self.data.shapeshift.isWerewolf = playerPacket.shapeshift.isWerewolf
end

function BasePlayer:LoadCell()

    local newCell = nil

    if self.data.location ~= nil then
        newCell = self.data.location.cell
    end

    if newCell == nil or newCell == "" then
        local spawnUsed = self:GetInitialSpawn()

        if spawnUsed == nil and config.defaultRespawn ~= nil then
            spawnUsed = {
                cellDescription = config.defaultRespawn.cellDescription,
                position = config.defaultRespawn.position,
                rotation = config.defaultRespawn.rotation
            }
        end

        if self:SendLocation(spawnUsed, { reason = "fallbackSpawn" }) then
            self:StoreLocation(spawnUsed)
            quicksaveCharacterState(self)
            tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
                " had no saved cell; using fallback spawn location")
        end

        return
    end

    local pendingLocationChange = self:BeginServerLocationChange("loadCell", newCell)
    tes3mp.SetCell(self.pid, newCell)
    setCellChangeReason(self.pid, pendingLocationChange.cellChangeReason)

    local pos = { self.data.location.posX, self.data.location.posY, self.data.location.posZ }
    local rot = { self.data.location.rotX, self.data.location.rotZ }

    if pos[1] ~= nil and pos[2] ~= nil and pos[3] ~= nil then
        tes3mp.SetPos(self.pid, pos[1], pos[2], pos[3])
    end

    if rot[1] ~= nil and rot[2] ~= nil then
        tes3mp.SetRot(self.pid, rot[1], rot[2])
    end

    tes3mp.SendCell(self.pid)
    tes3mp.SendPos(self.pid)

    local regionName = self.data.location.regionName

    if regionName ~= nil then
        logicHandler.LoadRegionForPlayer(self.pid, regionName, true)
    end
end

function BasePlayer:SaveCell(playerPacket)

    if self.data.location == nil then self.data.location = {} end

    -- Keep this around to update old player files
    if self.data.mapExplored == nil then self.data.mapExplored = {} end

    if playerPacket == nil or playerPacket.location == nil or
        playerPacket.location.cell == nil or playerPacket.location.cell == "" then
        if self.data.location.cell ~= nil and self.data.location.cell ~= "" then
            tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
                " sent empty cell data during location save; keeping saved cell " .. self.data.location.cell)
        end
        return
    end

    self.data.location.cell = playerPacket.location.cell
    self.data.location.posX = playerPacket.location.posX
    self.data.location.posY = playerPacket.location.posY
    self.data.location.posZ = playerPacket.location.posZ
    self.data.location.rotX = playerPacket.location.rotX
    self.data.location.rotZ = playerPacket.location.rotZ

    stateHelper:SaveMapExploration(self.pid, self)
end

function BasePlayer:LoadEquipment()

    for index = 0, tes3mp.GetEquipmentSize() - 1 do

        local currentItem = self.data.equipment[index]

        if currentItem ~= nil then
            if currentItem.enchantmentCharge == nil then
                currentItem.enchantmentCharge = -1
            end

            tes3mp.EquipItem(self.pid, index, currentItem.refId, currentItem.count,
                currentItem.charge, currentItem.enchantmentCharge)
        else
            tes3mp.UnequipItem(self.pid, index)
        end
    end

    -- Store a copy of previous equipment to understand when any given
    -- equipment item's count, charge or enchantmentCharge have changed
    self.previousEquipment = tableHelper.deepCopy(self.data.equipment)

    tes3mp.SendEquipment(self.pid)
end

function BasePlayer:SaveEquipment(playerPacket)

    local reloadAtEnd = false

    if playerPacket == nil or type(playerPacket.equipment) ~= "table" then
        return
    end

    for slot, equipmentItem in pairs(playerPacket.equipment) do
        local newRefId = equipmentItem.refId

        if newRefId ~= "" and tableHelper.containsValue(config.bannedEquipmentItems, newRefId) then
            self:Message("You have tried wearing an item that isn't allowed!\n")
            reloadAtEnd = true
        else
            local newCount = equipmentItem.count
            local newCharge = equipmentItem.charge
            local newEnchantmentCharge = equipmentItem.enchantmentCharge
            local previousItem = self.previousEquipment[slot]

            if newRefId == "" or newCount == nil or newCount <= 0 then
                self.data.equipment[slot] = nil
                self.previousEquipment[slot] = nil
            else
                if self.creatingNewCharacter ~= true and (previousItem == nil or previousItem.refId ~= newRefId) and
                    not hasInventoryForEquipmentItem(self.data, equipmentItem) then
                    tes3mp.LogAppend(enumerations.log.WARN, "- Reconciled equipment for missing inventory item " ..
                        newRefId .. " in slot " .. slot)
                end

                self.data.equipment[slot] = {
                    refId = newRefId,
                    count = newCount,
                    charge = newCharge,
                    enchantmentCharge = newEnchantmentCharge
                }

                -- Is this the same item that was previously in this slot? In that case,
                -- its count, charge or enchantmentCharge must have changed, so we need
                -- to also update that in the inventory table

                if previousItem ~= nil and previousItem.refId == newRefId then
                    local inventoryIndex = inventoryHelper.getItemIndex(self.data.inventory,
                        previousItem.refId, previousItem.charge, previousItem.enchantmentCharge)

                    if inventoryIndex ~= nil then
                        self.data.inventory[inventoryIndex].count = newCount
                        self.data.inventory[inventoryIndex].charge = newCharge
                        self.data.inventory[inventoryIndex].enchantmentCharge = newEnchantmentCharge
                    end
                end

                self.previousEquipment[slot] = tableHelper.deepCopy(self.data.equipment[slot])
            end
        end
    end

    ensureEquippedItemsInInventory(self.data)

    if reloadAtEnd then
        self:LoadEquipment()
    end

    quicksaveCharacterState(self)
end

-- Iterate through inventory items and remove nil values as well as items whose
-- records no longer exist
-- Note: The check for existing records can only handle generated records for now
function BasePlayer:CleanInventory()
    removeSuppressedTutorialInventoryItems(self.data)

    for index, currentItem in pairs(self.data.inventory) do

        if logicHandler.IsGeneratedRecord(currentItem.refId) then

            local recordStore = logicHandler.GetRecordStoreByRecordId(currentItem.refId)

            if recordStore == nil or recordStore.data.generatedRecords[currentItem.refId] == nil then
                self.data.inventory[index] = nil
            end
        end
    end

    if not tableHelper.isArray(self.data.inventory) then
        tableHelper.cleanNils(self.data.inventory)
    end
end

-- Send a packet with some specific item changes to the player, to avoid having
-- to resend the entire inventory
--
-- Note: This just sends a packet, so the same item changes should be applied to
--       self.data.inventory separately
function BasePlayer:LoadItemChanges(itemArray, inventoryAction)

    tes3mp.ClearInventoryChanges(self.pid)
    tes3mp.SetInventoryChangesAction(self.pid, inventoryAction)

    for index, currentItem in pairs(itemArray) do

        if currentItem.count > 0 then
            packetBuilder.AddPlayerInventoryItemChange(self.pid, currentItem)
        end
    end

    tes3mp.SendInventoryChanges(self.pid)
end

function BasePlayer:PurgeExpiredContainerInventoryChanges()
    if type(self.pendingContainerInventoryChanges) ~= "table" then
        return
    end

    local currentTime = os.time()

    for pendingIndex, pendingItem in pairs(self.pendingContainerInventoryChanges) do
        if type(pendingItem) ~= "table" or pendingItem.count == nil or pendingItem.count <= 0 or
            (pendingItem.timestamp ~= nil and
                currentTime - pendingItem.timestamp > pendingContainerInventoryChangeTimeout) then
            self.pendingContainerInventoryChanges[pendingIndex] = nil
        end
    end

    tableHelper.cleanNils(self.pendingContainerInventoryChanges)

    if tableHelper.isEmpty(self.pendingContainerInventoryChanges) then
        self.pendingContainerInventoryChanges = nil
    end
end

function BasePlayer:HasPendingContainerInventoryChanges()
    self:PurgeExpiredContainerInventoryChanges()
    return type(self.pendingContainerInventoryChanges) == "table" and
        not tableHelper.isEmpty(self.pendingContainerInventoryChanges)
end

function BasePlayer:QueueContainerInventoryEcho(inventoryAction, item)
    local mirrorItem = normalizeContainerInventoryMirrorItem(item)

    if mirrorItem == nil then
        return false
    end

    if type(self.pendingContainerInventoryChanges) ~= "table" then
        self.pendingContainerInventoryChanges = {}
    end

    mirrorItem.action = inventoryAction
    mirrorItem.timestamp = os.time()
    table.insert(self.pendingContainerInventoryChanges, mirrorItem)

    return true
end

function BasePlayer:ConsumeContainerInventoryEcho(inventoryAction, item)
    local mirrorItem = normalizeContainerInventoryMirrorItem(item)

    if mirrorItem == nil then
        return 0
    end

    self:PurgeExpiredContainerInventoryChanges()

    if type(self.pendingContainerInventoryChanges) ~= "table" then
        return 0
    end

    local consumedCount = 0
    local remainingCount = mirrorItem.count

    for pendingIndex, pendingItem in pairs(self.pendingContainerInventoryChanges) do
        if remainingCount <= 0 then
            break
        end

        if pendingItem.action == inventoryAction and
            containerInventoryMirrorItemsMatch(pendingItem, mirrorItem) then
            local currentConsumedCount = math.min(remainingCount, pendingItem.count)
            pendingItem.count = pendingItem.count - currentConsumedCount
            remainingCount = remainingCount - currentConsumedCount
            consumedCount = consumedCount + currentConsumedCount

            if pendingItem.count <= 0 then
                self.pendingContainerInventoryChanges[pendingIndex] = nil
            end
        end
    end

    tableHelper.cleanNils(self.pendingContainerInventoryChanges)

    if tableHelper.isEmpty(self.pendingContainerInventoryChanges) then
        self.pendingContainerInventoryChanges = nil
    end

    return consumedCount
end

function BasePlayer:CanApplyContainerInventoryMirror(inventoryAction, item)
    local mirrorItem = normalizeContainerInventoryMirrorItem(item)

    if mirrorItem == nil then
        return false, 0, nil
    end

    if inventoryAction == enumerations.inventory.ADD then
        return true, mirrorItem.count, mirrorItem
    elseif inventoryAction == enumerations.inventory.REMOVE then
        local savedCount = getInventoryRefIdCount(self.data.inventory, mirrorItem.refId)
        return savedCount >= mirrorItem.count, savedCount, mirrorItem
    end

    return false, 0, nil
end

function BasePlayer:ApplyContainerInventoryMirror(inventoryAction, item)
    local canApply, savedCount, mirrorItem = self:CanApplyContainerInventoryMirror(inventoryAction, item)

    if not canApply or mirrorItem == nil then
        if mirrorItem ~= nil and inventoryAction == enumerations.inventory.REMOVE then
            tes3mp.LogAppend(enumerations.log.WARN, "- Rejected gameplay container drop of " ..
                mirrorItem.count .. " " .. mirrorItem.refId .. "; saved inventory only had " .. savedCount)
        end
        return false, 0
    end

    if type(self.data.inventory) ~= "table" then
        self.data.inventory = {}
    end

    if not itemTransactionJournal.recordPlayerInventoryChange(self, inventoryAction, { mirrorItem }, {
        source = "containerMirror"
    }) then
        return false, 0
    end

    if inventoryAction == enumerations.inventory.ADD then
        inventoryHelper.addItem(self.data.inventory, mirrorItem.refId, mirrorItem.count,
            mirrorItem.charge, mirrorItem.enchantmentCharge, mirrorItem.soul)

        if logicHandler.IsGeneratedRecord(mirrorItem.refId) then
            local recordStore = logicHandler.GetRecordStoreByRecordId(mirrorItem.refId)

            if recordStore ~= nil then
                self:AddLinkToRecord(recordStore.storeType, mirrorItem.refId)
            end
        end
    elseif inventoryAction == enumerations.inventory.REMOVE then
        inventoryHelper.removeClosestItem(self.data.inventory, mirrorItem.refId, mirrorItem.count,
            mirrorItem.charge, mirrorItem.enchantmentCharge, mirrorItem.soul)

        local removedEquipmentSlots = pruneEquipmentMissingInventory(self.data, mirrorItem.refId)

        if type(self.previousEquipment) == "table" then
            for slot in pairs(removedEquipmentSlots) do
                self.previousEquipment[slot] = nil
            end
        end

        if not inventoryHelper.containsItem(self.data.inventory, mirrorItem.refId) and
            logicHandler.IsGeneratedRecord(mirrorItem.refId) then
            local recordStore = logicHandler.GetRecordStoreByRecordId(mirrorItem.refId)

            if recordStore ~= nil then
                self:RemoveLinkToRecord(recordStore.storeType, mirrorItem.refId)
            end
        end
    else
        return false, 0
    end

    self:QueueContainerInventoryEcho(inventoryAction, mirrorItem)
    quicksaveCharacterState(self)

    return true, mirrorItem.count
end

function BasePlayer:LoadInventory()

    if self.data.inventory == nil then self.data.inventory = {} end
    local removedTutorialItems = removeSuppressedTutorialInventoryItems(self.data)
    ensureEquippedItemsInInventory(self.data)

    tes3mp.ClearInventoryChanges(self.pid)
    tes3mp.SetInventoryChangesAction(self.pid, enumerations.inventory.SET)

    local pendingChanges = 0
    for index, currentItem in pairs(self.data.inventory) do

        if currentItem.count ~= nil and currentItem.count > 0 then
            if pendingChanges >= maxInventoryChangesPerPacket then
                tes3mp.SendInventoryChanges(self.pid)
                tes3mp.ClearInventoryChanges(self.pid)
                tes3mp.SetInventoryChangesAction(self.pid, enumerations.inventory.ADD)
                pendingChanges = 0
            end

            packetBuilder.AddPlayerInventoryItemChange(self.pid, currentItem)
            pendingChanges = pendingChanges + 1
        else
            tes3mp.LogMessage(enumerations.log.INFO, "Caught nil or empty item in inventory for player " .. self.name .. " with item " .. tostring(currentItem) .. ", purging from data store.")
            self.data.inventory[index] = nil
        end
    end

    tes3mp.SendInventoryChanges(self.pid)

    if removedTutorialItems then
        self:QuicksaveToDrive()
    end
end

function BasePlayer:SaveInventory(playerPacket)

    if playerPacket == nil or type(playerPacket.inventory) ~= "table" then
        return
    end

    local action = playerPacket.action
    local reloadAtEnd = false
    removeSuppressedTutorialInventoryItems({ inventory = playerPacket.inventory })

    tes3mp.LogMessage(enumerations.log.INFO, "Saving " .. tableHelper.getCount(playerPacket.inventory) ..
        " item(s) to inventory with action " .. tableHelper.getIndexByValue(enumerations.inventory, action))

    if action == enumerations.inventory.SET and tableHelper.isEmpty(playerPacket.inventory) and
        type(self.data.inventory) == "table" and not tableHelper.isEmpty(self.data.inventory) then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent an empty inventory snapshot; keeping saved inventory")
        return
    end

    if action == enumerations.inventory.SET and
        inventorySnapshotIsMissingSavedEquipment(self.data, playerPacket.inventory) then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent an inventory snapshot missing saved equipped items; keeping saved inventory")
        return
    end

    if action == enumerations.inventory.SET and self:HasPendingContainerInventoryChanges() then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent a full inventory snapshot while gameplay container transfers were pending; keeping saved inventory")
        self:LoadInventory()
        return
    end

    if action == enumerations.inventory.SET then
        if not itemTransactionJournal.recordPlayerInventoryChange(self, action, playerPacket.inventory, {
            source = "playerInventoryPacket",
            fullSnapshot = true
        }) then
            self:LoadInventory()
            return
        end

        self.data.inventory = {}
    end

    for itemIndex, item in pairs(playerPacket.inventory) do
        if item.refId ~= "" then

            local skipItem = false

            if action == enumerations.inventory.ADD or action == enumerations.inventory.REMOVE then
                local consumedCount = self:ConsumeContainerInventoryEcho(action, item)

                if consumedCount > 0 then
                    tes3mp.LogAppend(enumerations.log.INFO, "- Consumed " .. consumedCount ..
                        " pending gameplay container inventory echo for " .. item.refId)

                    if consumedCount >= item.count then
                        skipItem = true
                    else
                        item = tableHelper.deepCopy(item)
                        item.count = item.count - consumedCount
                    end
                end
            end

            tes3mp.LogAppend(enumerations.log.INFO, "- id: " .. item.refId .. ", count: " .. item.count ..
                ", charge: " .. item.charge .. ", enchantmentCharge: " .. item.enchantmentCharge ..
                ", soul: " .. item.soul)

            if not skipItem and (action == enumerations.inventory.SET or action == enumerations.inventory.ADD) then

                if action == enumerations.inventory.SET or
                    itemTransactionJournal.recordPlayerInventoryChange(self, action, { item }, {
                        source = "playerInventoryPacket"
                    }) then
                    inventoryHelper.addItem(self.data.inventory, item.refId, item.count, item.charge,
                        item.enchantmentCharge, item.soul)

                    if logicHandler.IsGeneratedRecord(item.refId) then

                        local recordStore = logicHandler.GetRecordStoreByRecordId(item.refId)

                        if recordStore ~= nil then
                            self:AddLinkToRecord(recordStore.storeType, item.refId)
                        end
                    end
                else
                    reloadAtEnd = true
                end

            elseif not skipItem and action == enumerations.inventory.REMOVE then

                local savedCount = getInventoryRefIdCount(self.data.inventory, item.refId)
                local removeCount = item.count

                if savedCount <= 0 then
                    tes3mp.LogAppend(enumerations.log.WARN, "- Rejected inventory remove for missing item " ..
                        item.refId)
                    reloadAtEnd = true
                else
                    if removeCount > savedCount then
                        tes3mp.LogAppend(enumerations.log.WARN, "- Clamped inventory remove count for " ..
                            item.refId .. " from " .. removeCount .. " to authoritative count " .. savedCount)
                        removeCount = savedCount
                        reloadAtEnd = true
                    end

                    local journalItem = tableHelper.deepCopy(item)
                    journalItem.count = removeCount

                    if itemTransactionJournal.recordPlayerInventoryChange(self, action, { journalItem }, {
                        source = "playerInventoryPacket"
                    }) then
                        inventoryHelper.removeClosestItem(self.data.inventory, item.refId, removeCount,
                            item.charge, item.enchantmentCharge, item.soul)
                    else
                        reloadAtEnd = true
                    end
                end

                if not inventoryHelper.containsItem(self.data.inventory, item.refId) and
                    logicHandler.IsGeneratedRecord(item.refId) then

                    local recordStore = logicHandler.GetRecordStoreByRecordId(item.refId)

                    if recordStore ~= nil then
                        self:RemoveLinkToRecord(recordStore.storeType, item.refId)
                    end
                end
            end
        end
    end

    ensureEquippedItemsInInventory(self.data)
    removeSuppressedTutorialInventoryItems(self.data)
    quicksaveCharacterState(self)

    if reloadAtEnd then
        self:LoadInventory()
        self:LoadEquipment()
    end
end

-- Iterate through spells and remove nil values as well as spells whose records
-- no longer exist
-- Note: The check for existing records can only handle generated records for now
function BasePlayer:CleanSpellbook()

    local recordStore = RecordStores["spell"]

    for index, spellId in pairs(self.data.spellbook) do

        -- Make sure we skip over old spell tables from previous versions of TES3MP
        if type(spellId) ~= "table" and logicHandler.IsGeneratedRecord(spellId) then

            if recordStore.data.generatedRecords[spellId] == nil then
                self.data.spellbook[index] = nil
            end
        end
    end

    if not tableHelper.isArray(self.data.spellbook) then
        tableHelper.cleanNils(self.data.spellbook)
    end
end

function BasePlayer:LoadSpellbook()

    if self.data.spellbook == nil then self.data.spellbook = {} end

    tes3mp.ClearSpellbookChanges(self.pid)
    tes3mp.SetSpellbookChangesAction(self.pid, enumerations.spellbook.SET)

    local pendingChanges = 0
    for index, spellId in pairs(self.data.spellbook) do

        if pendingChanges >= maxSpellbookChangesPerPacket then
            tes3mp.SendSpellbookChanges(self.pid)
            tes3mp.ClearSpellbookChanges(self.pid)
            tes3mp.SetSpellbookChangesAction(self.pid, enumerations.spellbook.ADD)
            pendingChanges = 0
        end

        -- Is this an old spell table from a previous version of TES3MP?
        -- If so, update it to the new format
        if type(spellId) == "table" then
            spellId = spellId.spellId
            self.data.spellbook[index] = spellId
        end

        tes3mp.AddSpell(self.pid, spellId)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendSpellbookChanges(self.pid)
end

function BasePlayer:SaveSpellbook(playerPacket)

    if playerPacket == nil or type(playerPacket.spellbook) ~= "table" then
        return
    end

    if self.data.spellbook == nil then self.data.spellbook = {} end

    local action = playerPacket.action

    if action == enumerations.spellbook.SET and tableHelper.isEmpty(playerPacket.spellbook) and
        not tableHelper.isEmpty(self.data.spellbook) then
        tes3mp.LogMessage(enumerations.log.WARN, logicHandler.GetChatName(self.pid) ..
            " sent an empty spellbook snapshot; keeping saved spellbook")
        return
    end

    if action == enumerations.spellbook.SET then
        self.data.spellbook = {}
    end

    for spellIndex, spellId in pairs(playerPacket.spellbook) do
        if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
            -- Only add new spell if we don't already have it
            if not tableHelper.containsValue(self.data.spellbook, spellId) then
                tes3mp.LogMessage(enumerations.log.INFO, "Adding spellbook spell " .. spellId .. " to " ..
                    logicHandler.GetChatName(self.pid))
                table.insert(self.data.spellbook, spellId)
            end
        elseif action == enumerations.spellbook.REMOVE then
            -- Only print spell removal if the spell actually exists
            if tableHelper.containsValue(self.data.spellbook, spellId) == true then
                tes3mp.LogMessage(enumerations.log.INFO, "Removing spellbook spell " .. spellId .. " from " ..
                    logicHandler.GetChatName(self.pid))
                local foundIndex = tableHelper.getIndexByValue(self.data.spellbook, spellId)
                self.data.spellbook[foundIndex] = nil

                if logicHandler.IsGeneratedRecord(spellId) then
                    local recordStore = RecordStores["spell"]

                    if recordStore ~= nil then
                        self:RemoveLinkToRecord(recordStore.storeType, spellId)
                    end
                end
            end
        end
    end

    if action == enumerations.spellbook.REMOVE then
        tableHelper.cleanNils(self.data.spellbook)
    end

    quicksaveCharacterState(self)
end

function BasePlayer:UpdateActiveSpellTimes()

    for spellId, spellInstances in pairs(self.data.spellsActive) do
        for spellInstanceIndex, spellInstanceValues in pairs(spellInstances) do
            local hadRemainingEffect = false

            for effectIndex, effectTable in pairs(spellInstanceValues.effects) do

                local timeSinceCast = os.time() - spellInstanceValues.startTime

                if timeSinceCast <= 0 then
                    self.data.spellsActive[spellId][spellInstanceIndex].effects[effectIndex] = nil
                else
                    hadRemainingEffect = true

                    -- Subtract the time elapsed since casting the spell from the
                    -- effect's remaining time
                    effectTable.timeLeft = effectTable.timeLeft - timeSinceCast
                end
            end

            if hadRemainingEffect == false then
                self.data.spellsActive[spellId][spellInstanceIndex] = nil
            end
        end

        if tableHelper.getCount(self.data.spellsActive[spellId]) == 0 then
            self.data.spellsActive[spellId] = nil
        end
    end

    tableHelper.cleanNils(self.data.spellsActive)
end

function BasePlayer:LoadSpellsActive()

    if self.data.spellsActive == nil then self.data.spellsActive = {} end

    if tableHelper.getCount(self.data.spellsActive) > 0 then
        packetBuilder.AddPlayerSpellsActive(self.pid, self.data.spellsActive, enumerations.spellbook.SET,
            maxActiveSpellsPerPacket, function(pid)
                tes3mp.SendSpellsActiveChanges(pid, true)
            end)

        -- Send this to all players, or they'll only know about active spells added afterwards
        tes3mp.SendSpellsActiveChanges(self.pid, true)
    end
end

function BasePlayer:SaveSpellsActive(playerPacket)

    local action = playerPacket.action

    if action == enumerations.spellbook.SET or self.data.spellsActive == nil then
        self.data.spellsActive = {}
    end

    for spellId, spellInstances in pairs(playerPacket.spellsActive) do

        if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
            if self.data.spellsActive[spellId] == nil then
                self.data.spellsActive[spellId] = {}
            end

            for _, spellInstanceValues in pairs(spellInstances) do

                tes3mp.LogMessage(enumerations.log.INFO, "Adding instance of active spell " .. spellId .. " to " ..
                    logicHandler.GetChatName(self.pid))

                local spellInstanceIndex

                -- Get an unused spellInstanceIndex if this is a spell with stacking effects
                if spellInstanceValues.stackingState then
                    spellInstanceIndex = tableHelper.getUnusedNumericalIndex(self.data.spellsActive[spellId])
                -- Otherwise, replace what's under index 1
                else
                    spellInstanceIndex = 1
                end

                self.data.spellsActive[spellId][spellInstanceIndex] = {
                    displayName = spellInstanceValues.displayName,
                    stackingState = spellInstanceValues.stackingState,
                    effects = tableHelper.deepCopy(spellInstanceValues.effects),
                    startTime = os.time()
                }

                if spellInstanceValues.caster ~= nil then
                    self.data.spellsActive[spellId][spellInstanceIndex].caster = {
                        playerName = spellInstanceValues.caster.playerName,
                        playerKey = spellInstanceValues.caster.playerKey,
                        accountName = spellInstanceValues.caster.accountName,
                        characterName = spellInstanceValues.caster.characterName,
                        refId = spellInstanceValues.caster.refId,
                        uniqueIndex = spellInstanceValues.caster.uniqueIndex
                    }
                end
            end
        elseif action == enumerations.spellbook.REMOVE then
            -- Only print spell removal if the spell actually exists
            if self.data.spellsActive[spellId] ~= nil then
                tes3mp.LogMessage(enumerations.log.INFO, "Removing active spell " .. spellId .. " from " ..
                    logicHandler.GetChatName(self.pid))
                self.data.spellsActive[spellId][1] = nil
            end
        end
    end

    if action == enumerations.spellbook.REMOVE then
        tableHelper.cleanNils(self.data.spellsActive)
    end
end

function BasePlayer:LoadCooldowns()

    if self.data.cooldowns == nil then self.data.cooldowns = {} end

    if tableHelper.getCount(self.data.cooldowns) > 0 then
        tes3mp.ClearCooldownChanges(self.pid)

        local pendingChanges = 0
        for _, cooldown in pairs(self.data.cooldowns) do
            if pendingChanges >= maxCooldownChangesPerPacket then
                tes3mp.SendCooldownChanges(self.pid)
                tes3mp.ClearCooldownChanges(self.pid)
                pendingChanges = 0
            end

            tes3mp.AddCooldownSpell(self.pid, cooldown.spellId, cooldown.startDay, cooldown.startHour)
            pendingChanges = pendingChanges + 1
        end

        tes3mp.SendCooldownChanges(self.pid)
    end
end

function BasePlayer:SaveCooldowns(playerPacket)

    for _, cooldown in pairs(playerPacket.cooldowns) do
        table.insert(self.data.cooldowns, cooldown)
    end
end

function BasePlayer:LoadQuickKeys()

    if self.data.quickKeys == nil then self.data.quickKeys = {} end

    tes3mp.ClearQuickKeyChanges(self.pid)

    for slot, currentQuickKey in pairs(self.data.quickKeys) do

        if currentQuickKey ~= nil then
            tes3mp.AddQuickKey(self.pid, slot, currentQuickKey.keyType, currentQuickKey.itemId)
        end
    end

    tes3mp.SendQuickKeyChanges(self.pid)
end

function BasePlayer:SaveQuickKeys(playerPacket)

    if playerPacket == nil or type(playerPacket.quickKeys) ~= "table" then
        return
    end

    if self.data.quickKeys == nil then self.data.quickKeys = {} end

    for slot, quickKey in pairs(playerPacket.quickKeys) do
        self.data.quickKeys[slot] = {
            keyType = quickKey.keyType,
            itemId = quickKey.itemId
        }
    end

    quicksaveCharacterState(self)
end

function BasePlayer:LoadJournal()
    stateHelper:LoadJournal(self.pid, self)
end

function BasePlayer:SaveJournal(playerPacket)
    return stateHelper:SaveJournal(self, playerPacket)
end

function BasePlayer:SyncQuestStateWithOnlineAllies()
    if config.shareJournalWithAllies ~= true or config.shareJournal == true then
        return
    end

    if self.data.alliedPlayers == nil then self.data.alliedPlayers = {} end

    local changedPlayers = {}
    local onlineAllies = {}

    local function markChanged(player)
        changedPlayers[player.pid] = player
    end

    for _, otherAllyKey in ipairs(self.data.alliedPlayers) do
        local otherPlayer = logicHandler.GetLoggedInPlayerByStorageKey(otherAllyKey)

        if otherPlayer ~= nil and otherPlayer.pid ~= self.pid then
            table.insert(onlineAllies, otherPlayer)

            if mergeJournalData(self, otherPlayer) or mergeQuestClientGlobals(self, otherPlayer) then
                markChanged(self)
            end
        end
    end

    for _, otherPlayer in ipairs(onlineAllies) do
        if mergeJournalData(otherPlayer, self) or mergeQuestClientGlobals(otherPlayer, self) then
            markChanged(otherPlayer)
        end
    end

    local changedCount = 0

    for _, player in pairs(changedPlayers) do
        quicksaveCharacterState(player)
        player:LoadJournal()
        player:LoadClientScriptVariables()
        changedCount = changedCount + 1
    end

    if changedCount > 0 then
        tes3mp.LogMessage(enumerations.log.INFO, "Reconciled ally quest state for " ..
            logicHandler.GetChatName(self.pid) .. " across " .. changedCount .. " online characters")
    end
end

function BasePlayer:LoadFactionRanks()
    stateHelper:LoadFactionRanks(self.pid, self)
end

function BasePlayer:SaveFactionRanks()
    stateHelper:SaveFactionRanks(self.pid, self)
end

function BasePlayer:LoadFactionExpulsion()
    stateHelper:LoadFactionExpulsion(self.pid, self)
end

function BasePlayer:SaveFactionExpulsion()
    stateHelper:SaveFactionExpulsion(self.pid, self)
end

function BasePlayer:LoadFactionReputation()
    stateHelper:LoadFactionReputation(self.pid, self)
end

function BasePlayer:SaveFactionReputation()
    stateHelper:SaveFactionReputation(self.pid, self)
end

function BasePlayer:LoadTopics()
    stateHelper:LoadTopics(self.pid, self)
end

function BasePlayer:SaveTopics()
    return stateHelper:SaveTopics(self.pid, self)
end

function BasePlayer:LoadBounty()
    stateHelper:LoadBounty(self.pid, self)
end

function BasePlayer:SaveBounty()
    stateHelper:SaveBounty(self.pid, self)
end

function BasePlayer:LoadReputation()
    stateHelper:LoadReputation(self.pid, self)
end

function BasePlayer:SaveReputation()
    stateHelper:SaveReputation(self.pid, self)
end

function BasePlayer:LoadClientScriptVariables()
    stateHelper:LoadClientScriptVariables(self.pid, self)
end

function BasePlayer:SaveClientScriptGlobal(variables)
    stateHelper:SaveClientScriptGlobal(self, variables)
end

function BasePlayer:LoadKills(pid, forEveryone)

    if self.data.kills == nil then
        self.data.kills = {}
    end

    tes3mp.ClearKillChanges()

    local pendingChanges = 0
    for refId, killCount in pairs(self.data.kills) do
        if pendingChanges >= maxKillChangesPerPacket then
            tes3mp.SendWorldKillCount(pid, forEveryone)
            tes3mp.ClearKillChanges()
            pendingChanges = 0
        end

        tes3mp.AddKill(refId, killCount)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendWorldKillCount(pid, forEveryone)
end

function BasePlayer:LoadDestinationOverrides(pid)
    stateHelper:LoadDestinationOverrides(self.pid, self)
end

function BasePlayer:LoadMap()
    stateHelper:LoadMap(self.pid, self)
end

function BasePlayer:LoadAllies()

    if self.data.alliedPlayers == nil then self.data.alliedPlayers = {} end

    tes3mp.ClearAlliedPlayersForPlayer(self.pid)

    for _, otherAllyKey in ipairs(self.data.alliedPlayers) do
        local otherPlayer = logicHandler.GetLoggedInPlayerByStorageKey(otherAllyKey)
        if otherPlayer ~= nil then
            tes3mp.AddAlliedPlayerForPlayer(self.pid, otherPlayer.pid)
        end
    end

    tes3mp.SendAlliedPlayers(self.pid, true)
end

function BasePlayer:LoadBooks()

    if self.data.books == nil then self.data.books = {} end

    tes3mp.ClearBookChanges(self.pid)
    tes3mp.SetBookChangesAreLoad(self.pid, true)

    local pendingChanges = 0
    for index, bookId in pairs(self.data.books) do

        if pendingChanges >= maxBookChangesPerPacket then
            tes3mp.SendBookChanges(self.pid)
            tes3mp.ClearBookChanges(self.pid)
            tes3mp.SetBookChangesAreLoad(self.pid, true)
            pendingChanges = 0
        end

        tes3mp.AddBook(self.pid, bookId)
        pendingChanges = pendingChanges + 1
    end

    tes3mp.SendBookChanges(self.pid)
    tes3mp.ClearBookChanges(self.pid)
    tes3mp.SetBookChangesAreLoad(self.pid, false)
    tes3mp.SendBookChanges(self.pid)
    tes3mp.ClearBookChanges(self.pid)
end

function BasePlayer:AddBooks()

    if self.data.books == nil then self.data.books = {} end

    for index = 0, tes3mp.GetBookChangesSize(self.pid) - 1 do
        local bookId = tes3mp.GetBookId(self.pid, index)

        -- Only add new book if we don't already have it
        if not tableHelper.containsValue(self.data.books, bookId, false) then
            tes3mp.LogMessage(enumerations.log.INFO, "Adding book " .. bookId .. " to " ..
                logicHandler.GetChatName(self.pid))
            table.insert(self.data.books, bookId)
        end
    end
end

function BasePlayer:LoadMarkLocation()

    if self.data.miscellaneous == nil then self.data.miscellaneous = {} end

    if self.data.miscellaneous.markLocation ~= nil then
        local markLocation = self.data.miscellaneous.markLocation
        tes3mp.SetMarkCell(self.pid, markLocation.cell)
        tes3mp.SetMarkPos(self.pid, markLocation.posX, markLocation.posY, markLocation.posZ)
        tes3mp.SetMarkRot(self.pid, markLocation.rotX, markLocation.rotZ)
        tes3mp.SendMarkLocation(self.pid)
    end
end

function BasePlayer:SaveMarkLocation()

    if self.data.miscellaneous == nil then self.data.miscellaneous = {} end

    self.data.miscellaneous.markLocation = {
        cell = tes3mp.GetMarkCell(self.pid),
        posX = tes3mp.GetMarkPosX(self.pid),
        posY = tes3mp.GetMarkPosY(self.pid),
        posZ = tes3mp.GetMarkPosZ(self.pid),
        rotX = tes3mp.GetMarkRotX(self.pid),
        rotZ = tes3mp.GetMarkRotZ(self.pid)
    }
end

function BasePlayer:LoadSelectedSpell()

    if self.data.miscellaneous == nil then
        self.data.miscellaneous = {}
    end

    if self.data.miscellaneous.selectedSpell ~= nil then
        tes3mp.SetSelectedSpellId(self.pid, self.data.miscellaneous.selectedSpell)
        tes3mp.SendSelectedSpell(self.pid)
    end
end

function BasePlayer:SaveSelectedSpell()

    if self.data.miscellaneous == nil then self.data.miscellaneous = {} end

    self.data.miscellaneous.selectedSpell = tes3mp.GetSelectedSpellId(self.pid)
    self.data.miscellaneous.selectedEnchantedItem = nil
end

function BasePlayer:LoadSelectedEnchantedItem()

    if self.data.miscellaneous == nil then
        self.data.miscellaneous = {}
    end

    local item = self.data.miscellaneous.selectedEnchantedItem
    if item ~= nil and item.refId ~= nil and item.refId ~= "" then
        tes3mp.SetSelectedEnchantedItem(self.pid, item.refId, item.count or 1, item.charge or -1,
            item.enchantmentCharge or -1, item.soul or "")
        tes3mp.SendSelectedEnchantedItem(self.pid)
    end
end

function BasePlayer:SaveSelectedEnchantedItem()

    if self.data.miscellaneous == nil then self.data.miscellaneous = {} end

    local refId = tes3mp.GetSelectedEnchantedItemRefId(self.pid)
    local count = tes3mp.GetSelectedEnchantedItemCount(self.pid)

    if refId == nil or refId == "" or count <= 0 then
        self.data.miscellaneous.selectedEnchantedItem = nil
        return
    end

    self.data.miscellaneous.selectedSpell = ""
    self.data.miscellaneous.selectedEnchantedItem = {
        refId = refId,
        count = count,
        charge = tes3mp.GetSelectedEnchantedItemCharge(self.pid),
        enchantmentCharge = tes3mp.GetSelectedEnchantedItemEnchantmentCharge(self.pid),
        soul = tes3mp.GetSelectedEnchantedItemSoul(self.pid)
    }
end

function BasePlayer:GetDifficulty()
    self:NormalizeSettings()
    return self.data.settings.difficulty
end

function BasePlayer:GetConsoleAllowed()
    self:NormalizeSettings()
    return self.data.settings.consoleAllowed
end

function BasePlayer:GetBedRestAllowed()
    self:NormalizeSettings()
    return self.data.settings.bedRestAllowed
end

function BasePlayer:GetWildernessRestAllowed()
    self:NormalizeSettings()
    return self.data.settings.wildernessRestAllowed
end

function BasePlayer:GetWaitAllowed()
    self:NormalizeSettings()
    return self.data.settings.waitAllowed
end

function BasePlayer:GetEnforcedLogLevel()
    self:NormalizeSettings()
    return self.data.settings.enforcedLogLevel
end

function BasePlayer:GetPhysicsFramerate()
    self:NormalizeSettings()
    return self.data.settings.physicsFramerate
end

function BasePlayer:SetDifficulty(difficulty)
    self:NormalizeSettings()

    if difficulty == nil or difficulty == "default" then
        difficulty = config.difficulty
        self.data.settings.difficulty = "default"
    else
        self.data.settings.difficulty = difficulty
    end

    tes3mp.SetDifficulty(self.pid, difficulty)
    tes3mp.LogMessage(enumerations.log.INFO, "Set difficulty to " .. tostring(difficulty) .. " for " ..
        logicHandler.GetChatName(self.pid))
end

function BasePlayer:SetEnforcedLogLevel(enforcedLogLevel)
    self:NormalizeSettings()

    if enforcedLogLevel == nil or enforcedLogLevel == "default" then
        enforcedLogLevel = config.enforcedLogLevel
        self.data.settings.enforcedLogLevel = "default"
    else
        self.data.settings.enforcedLogLevel = enforcedLogLevel
    end

    tes3mp.SetEnforcedLogLevel(self.pid, enforcedLogLevel)
    tes3mp.LogMessage(enumerations.log.INFO, "Set enforced log level to " .. tostring(enforcedLogLevel) ..
        " for " .. logicHandler.GetChatName(self.pid))
end

function BasePlayer:SetPhysicsFramerate(physicsFramerate)
    self:NormalizeSettings()

    if physicsFramerate == nil or physicsFramerate == "default" then
        physicsFramerate = config.physicsFramerate
        self.data.settings.physicsFramerate = "default"
    else
        self.data.settings.physicsFramerate = physicsFramerate
    end

    tes3mp.SetPhysicsFramerate(self.pid, physicsFramerate)
    tes3mp.LogMessage(enumerations.log.INFO, "Set physics framerate to " .. tostring(physicsFramerate) ..
        " for " .. logicHandler.GetChatName(self.pid))
end

function BasePlayer:SetConsoleAllowed(state)
    self:NormalizeSettings()

    if state == nil or state == "default" then
        state = config.allowConsole
        self.data.settings.consoleAllowed = "default"
    else
        self.data.settings.consoleAllowed = state
    end

    tes3mp.SetConsoleAllowed(self.pid, state)
end

function BasePlayer:SetBedRestAllowed(state)
    self:NormalizeSettings()

    if state == nil or state == "default" then
        state = config.allowBedRest
        self.data.settings.bedRestAllowed = "default"
    else
        self.data.settings.bedRestAllowed = state
    end

    tes3mp.SetBedRestAllowed(self.pid, state)
end

function BasePlayer:SetWildernessRestAllowed(state)
    self:NormalizeSettings()

    if state == nil or state == "default" then
        state = config.allowWildernessRest
        self.data.settings.wildernessRestAllowed = "default"
    else
        self.data.settings.wildernessRestAllowed = state
    end

    tes3mp.SetWildernessRestAllowed(self.pid, state)
end

function BasePlayer:SetWaitAllowed(state)
    self:NormalizeSettings()

    if state == nil or state == "default" then
        state = config.allowWait
        self.data.settings.waitAllowed = "default"
    else
        self.data.settings.waitAllowed = state
    end

    tes3mp.SetWaitAllowed(self.pid, state)
end

function BasePlayer:SetWerewolfState(state)
    if self.data.shapeshift == nil then self.data.shapeshift = {} end

    self.data.shapeshift.isWerewolf = state

    tes3mp.SetWerewolfState(self.pid, state)
    tes3mp.SendShapeshift(self.pid)
end

function BasePlayer:SetScale(scale)
    if self.data.shapeshift == nil then self.data.shapeshift = {} end

    self.data.shapeshift.scale = scale

    tes3mp.SetScale(self.pid, scale)
    tes3mp.SendShapeshift(self.pid)
end

function BasePlayer:SetConfiscationState(state)

    if self.data.customVariables == nil then self.data.customVariables = {} end

    self.data.customVariables.isConfiscationTarget = state

    if self:IsLoggedIn() then

        if state == true then
            logicHandler.RunConsoleCommandOnPlayer(self.pid, "tm")
            logicHandler.RunConsoleCommandOnPlayer(self.pid, "disableplayercontrols")
            tes3mp.MessageBox(self.pid, -1, "You are immobilized while an item is being confiscated from you")
        elseif not state then
            self.data.customVariables.isConfiscationTarget = nil
            logicHandler.RunConsoleCommandOnPlayer(self.pid, "tm")
            logicHandler.RunConsoleCommandOnPlayer(self.pid, "enableplayercontrols")
            tes3mp.MessageBox(self.pid, -1, "You are free to move again")
        end
    end
end

function BasePlayer:LoadSettings()

    self:NormalizeSettings()

    self:SetDifficulty(self.data.settings.difficulty)
    self:SetConsoleAllowed(self.data.settings.consoleAllowed)
    self:SetBedRestAllowed(self.data.settings.bedRestAllowed)
    self:SetWildernessRestAllowed(self.data.settings.wildernessRestAllowed)
    self:SetWaitAllowed(self.data.settings.waitAllowed)
    self:SetEnforcedLogLevel(self.data.settings.enforcedLogLevel)
    self:SetPhysicsFramerate(self.data.settings.physicsFramerate)

    tes3mp.ClearGameSettingValues(self.pid)

    for _, settingPairTable in pairs(config.gameSettings) do
        tes3mp.SetGameSettingValue(self.pid, settingPairTable.name, tostring(settingPairTable.value))
    end

    tes3mp.ClearVRSettingValues(self.pid)

    for _, settingPairTable in pairs(config.vrSettings) do
        tes3mp.SetVRSettingValue(self.pid, settingPairTable.name, tostring(settingPairTable.value))
    end

    tes3mp.SendSettings(self.pid)
end

function BasePlayer:LoadSpecialStates()

    if self.data.customVariables == nil then self.data.customVariables = {} end

    if self.data.customVariables.isConfiscationTarget ~= nil then
        self:SetConfiscationState(self.data.customVariables.isConfiscationTarget)
    end
end

function BasePlayer:AddCellLoaded(cellDescription)

    -- Only add new loaded cell if we don't already have it
    if not tableHelper.containsValue(self.cellsLoaded, cellDescription) then
        table.insert(self.cellsLoaded, cellDescription)
    end
end

function BasePlayer:RemoveCellLoaded(cellDescription)

    tableHelper.removeValue(self.cellsLoaded, cellDescription)
end

function BasePlayer:RunPlayerSpecificStartupScripts()
    tes3mp.LogMessage(enumerations.log.INFO, "Running player-specific startup scripts for " ..
        logicHandler.GetChatName(self.pid) .. ":")

    for _, scriptName in pairs(config.playerStartupScripts) do
        tes3mp.LogAppend(enumerations.log.INFO, "- " .. scriptName)
        logicHandler.RunConsoleCommandOnPlayer(self.pid, "startscript " .. scriptName, false)
    end
end

return BasePlayer
