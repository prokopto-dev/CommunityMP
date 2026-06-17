require("config")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")

local itemTransactionJournal = {}

local schemaVersion = 1
local sequence = 0

local function getJournalRoot()
    if type(config.itemTransactionJournalRoot) == "string" and config.itemTransactionJournalRoot ~= "" then
        return config.itemTransactionJournalRoot
    end

    return "saves/tx/items"
end

local function safeSegment(value, fallback, maxLength)
    local segment = tostring(value or "")
    segment = segment:gsub("^%s+", ""):gsub("%s+$", "")
    segment = segment:gsub("[<>:\"/\\|%?%*%c]", "_")
    segment = segment:gsub("%.%.", "_")

    if segment == "" then
        segment = fallback or "unknown"
    end

    maxLength = maxLength or 64

    if string.len(segment) > maxLength then
        segment = string.sub(segment, 1, maxLength)
    end

    return segment
end

local function getEventTypeSegment(eventType)
    if eventType == "playerInventory" then
        return "pi"
    elseif eventType == "container" then
        return "c"
    elseif eventType == "object" then
        return "o"
    end

    return safeSegment(eventType, "i", 12)
end

local function getInventoryActionName(action)
    if enumerations ~= nil and type(enumerations.inventory) == "table" then
        local name = tableHelper.getIndexByValue(enumerations.inventory, action)
        if name ~= nil then
            return name
        end
    end

    return tostring(action)
end

local function normalizeItem(item)
    if type(item) ~= "table" or type(item.refId) ~= "string" or item.refId == "" then
        return nil
    end

    local count = tonumber(item.count)
    if count == nil or count <= 0 then
        return nil
    end

    return {
        refId = item.refId,
        count = math.floor(count),
        charge = tonumber(item.charge) or -1,
        enchantmentCharge = tonumber(item.enchantmentCharge) or -1,
        soul = item.soul or ""
    }
end

local function normalizeItems(items)
    local normalizedItems = {}

    if type(items) ~= "table" then
        return normalizedItems
    end

    for _, item in pairs(items) do
        local normalizedItem = normalizeItem(item)

        if normalizedItem ~= nil then
            table.insert(normalizedItems, normalizedItem)
        end
    end

    return normalizedItems
end

local function getPlayerIdentity(playerOrPid)
    local pid = nil
    local player = nil

    if type(playerOrPid) == "table" then
        player = playerOrPid
        pid = player.pid
    else
        pid = playerOrPid

        if type(Players) == "table" then
            player = Players[pid]
        end
    end

    local identity = {
        pid = pid
    }

    if type(player) == "table" then
        identity.name = player.name
        identity.accountName = player.accountName

        if type(player.GetCharacterStorageKey) == "function" then
            identity.characterStorageKey = player:GetCharacterStorageKey()
        end

        if type(player.GetCellVisitKey) == "function" then
            identity.cellVisitKey = player:GetCellVisitKey()
        end
    end

    return identity
end

local function getEventActorSegment(event)
    if type(event.actor) == "table" then
        return event.actor.characterStorageKey or event.actor.accountName or event.actor.name or event.actor.pid
    end

    return "server"
end

local function makeEventPath(event)
    local timestamp = tonumber(event.createdAt) or os.time()
    local day = os.date("!%Y%m%d", timestamp)
    local eventType = getEventTypeSegment(event.type)
    local actor = safeSegment(getEventActorSegment(event), "server", 32)
    local directory = getJournalRoot() .. "/" .. day

    if not saveCodec.ensureDirectory(directory) then
        return nil
    end

    for _ = 1, 1000 do
        sequence = sequence + 1

        if sequence > 999999 then
            sequence = 1
        end

        local fileName = string.format("%x_%x_%s_%s.xml", timestamp, sequence, eventType, actor)
        local relativePath = directory .. "/" .. fileName

        if not saveCodec.exists(relativePath) then
            return relativePath, sequence
        end
    end

    return nil
end

local function logFailure(message)
    if tes3mp ~= nil and type(tes3mp.LogMessage) == "function" then
        tes3mp.LogMessage(enumerations.log.ERROR, message)
    end
end

function itemTransactionJournal.record(event)
    if config.enableItemTransactionJournal == false then
        return true
    end

    if type(event) ~= "table" then
        return false
    end

    event.schemaVersion = schemaVersion
    event.createdAt = event.createdAt or os.time()

    local relativePath, eventSequence = makeEventPath(event)

    if relativePath == nil then
        logFailure("Failed to allocate CommunityMP item transaction journal path")
        return false
    end

    event.sequence = eventSequence
    event.path = relativePath

    local saved = saveCodec.save(relativePath, "item-transaction", event, {
        domain = "item-transaction",
        saveSchemaVersion = schemaVersion,
        transactionType = event.type
    })

    if not saved then
        logFailure("Failed to save CommunityMP item transaction journal entry at " .. relativePath)
    end

    return saved
end

function itemTransactionJournal.recordPlayerInventoryChange(player, action, items, context)
    local normalizedItems = normalizeItems(items)

    if tableHelper.isEmpty(normalizedItems) then
        return true
    end

    context = context or {}

    return itemTransactionJournal.record({
        type = "playerInventory",
        source = context.source or "playerInventoryPacket",
        inventoryAction = action,
        inventoryActionName = getInventoryActionName(action),
        actor = getPlayerIdentity(player),
        items = normalizedItems,
        fullSnapshot = context.fullSnapshot == true,
        reason = context.reason
    })
end

function itemTransactionJournal.recordContainerPacket(cell, pid, action, subAction, packetOrigin, objects, context)
    if type(objects) ~= "table" or tableHelper.isEmpty(objects) then
        return true
    end

    context = context or {}

    return itemTransactionJournal.record({
        type = "container",
        source = context.source or "containerPacket",
        actor = getPlayerIdentity(pid),
        cell = cell ~= nil and cell.description or nil,
        containerAction = action,
        containerSubAction = subAction,
        packetOrigin = packetOrigin,
        objects = tableHelper.deepCopy(objects)
    })
end

function itemTransactionJournal.recordObjectPacket(cell, pid, packetType, objects, context)
    if type(objects) ~= "table" or tableHelper.isEmpty(objects) then
        return true
    end

    context = context or {}

    return itemTransactionJournal.record({
        type = "object",
        source = context.source or "objectPacket",
        actor = getPlayerIdentity(pid),
        cell = cell ~= nil and cell.description or nil,
        packetType = packetType,
        objects = tableHelper.deepCopy(objects)
    })
end

return itemTransactionJournal
