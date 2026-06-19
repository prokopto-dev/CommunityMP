require("patterns")

contentFixer = require("contentFixer")
tableHelper = require("tableHelper")
inventoryHelper = require("inventoryHelper")
packetBuilder = require("packetBuilder")
dataTableBuilder = require("dataTableBuilder")
local itemTransactionJournal = require("communitymp.saves.itemTransactionJournal")

local BaseCell = class("BaseCell")
local containerTombstoneReplayActionCount = 1000000

local function getContainerItemKey(refId, charge, enchantmentCharge, soul)
    charge = tonumber(charge) or -1
    enchantmentCharge = tonumber(enchantmentCharge) or -1
    if soul == nil then soul = "" end

    return tostring(refId) .. "|" .. tostring(math.floor(charge)) .. "|" ..
        tostring(math.floor(enchantmentCharge)) .. "|" .. tostring(soul)
end

local function getContainerItemFromKey(key)
    local refId, charge, enchantmentCharge, soul = tostring(key):match("^(.-)|([^|]*)|([^|]*)|(.*)$")

    if refId == nil or refId == "" then
        return nil
    end

    return {
        refId = refId,
        count = containerTombstoneReplayActionCount,
        charge = tonumber(charge) or -1,
        enchantmentCharge = tonumber(enchantmentCharge) or -1,
        soul = soul or "",
        actionCount = containerTombstoneReplayActionCount
    }
end

local function hasContainerItemTombstone(objectData, refId, charge, enchantmentCharge, soul)
    if objectData.containerTombstones == nil then
        return false
    end

    return objectData.containerTombstones[getContainerItemKey(refId, charge, enchantmentCharge, soul)] == true
end

local function addContainerItemTombstone(objectData, refId, charge, enchantmentCharge, soul)
    if objectData.containerTombstones == nil then
        objectData.containerTombstones = {}
    end

    objectData.containerTombstones[getContainerItemKey(refId, charge, enchantmentCharge, soul)] = true
end

local function clearContainerItemTombstone(objectData, refId, charge, enchantmentCharge, soul)
    if objectData.containerTombstones == nil then
        return
    end

    objectData.containerTombstones[getContainerItemKey(refId, charge, enchantmentCharge, soul)] = nil

    if tableHelper.isEmpty(objectData.containerTombstones) then
        objectData.containerTombstones = nil
    end
end

local function isFiniteNumber(value)
    local number = tonumber(value)
    return number ~= nil and number == number and number ~= math.huge and number ~= -math.huge
end

local function isValidContainerLoadItem(item)
    if type(item) ~= "table" then
        return false
    end

    if type(item.refId) ~= "string" or item.refId == "" or string.find(item.refId, "$dynamic", 1, true) ~= nil then
        return false
    end

    item.count = tonumber(item.count)
    if item.count == nil or item.count <= 0 then
        return false
    end

    if item.charge == nil then
        item.charge = -1
    elseif not isFiniteNumber(item.charge) then
        return false
    else
        item.charge = tonumber(item.charge)
    end

    if item.enchantmentCharge == nil then
        item.enchantmentCharge = -1
    elseif not isFiniteNumber(item.enchantmentCharge) then
        return false
    else
        item.enchantmentCharge = tonumber(item.enchantmentCharge)
    end

    if item.soul == nil then
        item.soul = ""
    end

    return true
end

local function isLiveActorObjectData(objectData)
    return type(objectData) == "table" and (objectData.stats ~= nil or objectData.equipment ~= nil)
end

local function makeContainerInventoryMirrorItem(refId, count, charge, enchantmentCharge, soul)
    count = tonumber(count)

    if type(refId) ~= "string" or refId == "" or count == nil or count <= 0 then
        return nil
    end

    return {
        refId = refId,
        count = math.floor(count),
        charge = tonumber(charge) or -1,
        enchantmentCharge = tonumber(enchantmentCharge) or -1,
        soul = soul or ""
    }
end

local objectInteractionLocks = {}
local objectInteractionLockTtlSeconds = 90
local failedDialogueBarterTransactionTtlSeconds = 30

local function getObjectInteractionLockKind(lockKind)
    return lockKind or "container"
end

local function getObjectInteractionLockKey(cellDescription, uniqueIndex, lockKind)
    return tostring(cellDescription) .. "\n" .. tostring(uniqueIndex) .. "\n" ..
        getObjectInteractionLockKind(lockKind)
end

local function purgeExpiredObjectInteractionLocks(now)
    now = now or os.time()

    for lockKey, lock in pairs(objectInteractionLocks) do
        if lock.expiresAt ~= nil and lock.expiresAt <= now then
            objectInteractionLocks[lockKey] = nil
        end
    end
end

local function getPacketObjectUniqueIndex(objectKey, object)
    if type(object) == "table" and object.uniqueIndex ~= nil then
        return tostring(object.uniqueIndex)
    end

    if type(objectKey) == "string" and string.find(objectKey, "^%d+%-%d+$") ~= nil then
        return objectKey
    end

    return nil
end

local function getPacketObjectRefId(object)
    if type(object) == "table" and object.refId ~= nil then
        return object.refId
    end

    return ""
end

local function getExteriorCellGrid(cellDescription)
    if type(cellDescription) ~= "string" then
        return nil, nil
    end

    local _, _, gridX, gridY = string.find(cellDescription, "%((-?%d+),%s*(-?%d+)%)$")

    if gridX == nil or gridY == nil then
        _, _, gridX, gridY = string.find(cellDescription, patterns.exteriorCell)
    end

    if gridX == nil or gridY == nil then
        return nil, nil
    end

    return tonumber(gridX), tonumber(gridY)
end

local function getGridMatchedPlayerScopedCellConfig(cellDescription)
    local gridX, gridY = getExteriorCellGrid(cellDescription)

    if gridX == nil then
        return nil, nil
    end

    for configuredCellDescription, configuredCellConfig in pairs(config.playerScopedContainers) do
        local configuredGridX, configuredGridY = getExteriorCellGrid(configuredCellDescription)

        if configuredGridX == gridX and configuredGridY == gridY and
            type(configuredCellConfig) == "table" then
            return configuredCellConfig, configuredCellDescription
        end
    end

    return nil, nil
end

local function getPlayerScopedContainerCellConfig(cellDescription)
    if type(config.playerScopedContainers) ~= "table" then
        return nil, nil
    end

    local cellConfig = config.playerScopedContainers[cellDescription]

    if type(cellConfig) == "table" then
        return cellConfig, cellDescription
    end

    return getGridMatchedPlayerScopedCellConfig(cellDescription)
end

local function matchPlayerScopedContainerEntry(cellConfig, uniqueIndex, refId)
    if cellConfig[uniqueIndex] == true or (refId ~= nil and cellConfig[refId] == true) then
        return true
    end

    for _, entry in pairs(cellConfig) do
        if type(entry) == "table" then
            local matchesUniqueIndex = entry.uniqueIndex == nil or entry.uniqueIndex == uniqueIndex
            local matchesRefId = entry.refId == nil and entry.refIdPrefix == nil or entry.refId == refId
            local matchesRefIdPrefix = refId ~= nil and entry.refIdPrefix ~= nil and
                string.sub(refId, 1, string.len(entry.refIdPrefix)) == entry.refIdPrefix

            if matchesUniqueIndex and (matchesRefId or matchesRefIdPrefix) then
                return entry
            end
        end
    end

    return nil
end

local function getPlayerScopedContainerConfig(cellDescription, uniqueIndex, refId)
    local cellConfig = nil
    local storageCellDescription = nil

    cellConfig, storageCellDescription = getPlayerScopedContainerCellConfig(cellDescription)

    if cellConfig ~= nil then
        local entry = matchPlayerScopedContainerEntry(cellConfig, uniqueIndex, refId)

        if entry ~= nil then
            return entry, storageCellDescription
        end
    end

    cellConfig = config.playerScopedContainers["*"]

    if type(cellConfig) == "table" then
        local entry = matchPlayerScopedContainerEntry(cellConfig, uniqueIndex, refId)

        if entry ~= nil then
            return entry, cellDescription
        end
    end

    return nil, nil
end

local function getPlayerScopedContainerStorageKey(cellDescription, uniqueIndex, objectData)
    local refId = nil

    if objectData ~= nil then
        refId = objectData.refId
    end

    local _, storageCellDescription = getPlayerScopedContainerConfig(cellDescription, uniqueIndex, refId)

    return storageCellDescription
end

local function isPlayerScopedContainer(cellDescription, uniqueIndex, objectData)
    return getPlayerScopedContainerStorageKey(cellDescription, uniqueIndex, objectData) ~= nil
end

local function isPlayerScopedContainerItem(refId)
    if type(refId) ~= "string" or type(config.playerScopedContainerItemRefIds) ~= "table" then
        return false
    end

    local normalizedRefId = string.lower(refId)

    for configuredRefId, enabled in pairs(config.playerScopedContainerItemRefIds) do
        if enabled == true and type(configuredRefId) == "string" and
            string.lower(configuredRefId) == normalizedRefId then
            return true
        end
    end

    return false
end

local function getPlayerCellVisitKey(pid)
    if Players ~= nil and Players[pid] ~= nil then
        if type(Players[pid].GetCellVisitKey) == "function" then
            return Players[pid]:GetCellVisitKey()
        elseif Players[pid].accountName ~= nil and Players[pid].accountName ~= "" then
            return Players[pid].accountName
        end
    end

    return tostring(pid)
end

local function getLoggedInPlayerByStorageKey(playerKey)
    if logicHandler ~= nil and type(logicHandler.GetLoggedInPlayerByStorageKey) == "function" then
        return logicHandler.GetLoggedInPlayerByStorageKey(playerKey)
    end

    return nil
end

function BaseCell:__init(cellDescription)

    self.data =
    {
        entry = {
            description = cellDescription,
            creationTime = os.time()
        },
        loadState = {
            hasFullActorList = false,
            hasFullContainerData = false
        },
        lastVisit = {},
        objectData = {},
        packets = {},
        recordLinks = {}
    }

    self:EnsurePacketTables()

    self.description = cellDescription
    self.visitors = {}
    self.authority = nil

    self.isRequestingContainerData = false
    self.containerRequestPid = nil

    self.isRequestingActorList = false
    self.actorListRequestPid = nil

    self.isResetting = false

    self.unusableContainerUniqueIndexes = {}

    self.isExterior = false

    local gridX, gridY = getExteriorCellGrid(cellDescription)

    if gridX ~= nil then
        self.isExterior = true
        self.gridX = gridX
        self.gridY = gridY
    end
end

function BaseCell:ContainsPosition(posX, posY)

    local cellSize = 8192

    if self.isExterior then
        local correctGridX = math.floor(posX / cellSize)
        local correctGridY = math.floor(posY / cellSize)

        if self.gridX ~= correctGridX or self.gridY ~= correctGridY then
            return false
        end
    end

    return true
end

function BaseCell:HasEntry()
    return self.hasEntry
end

-- Iterate through the packets table and ensure all packet types are included in it
function BaseCell:EnsurePacketTables()

    if self.data.packets == nil then self.data.packets = {} end

    for _, packetType in pairs(config.cellPacketTypes) do
        if self.data.packets[packetType] == nil then
            self.data.packets[packetType] = {}
        end
    end
end

-- Iterate through saved packets and ensure the object uniqueIndexes they refer to
-- actually exist
function BaseCell:EnsurePacketValidity()

    for packetType, packetArray in pairs(self.data.packets) do
        for arrayIndex, uniqueIndex in pairs(self.data.packets[packetType]) do
            if self.data.objectData[uniqueIndex] == nil then
                tableHelper.removeValue(self.data.packets[packetType], uniqueIndex)
            end
        end
    end
end

-- Adding record links to cells is special because we'll keep track of the uniqueIndex
-- of every object that uses a particular generated record
function BaseCell:AddLinkToRecord(storeType, recordId, uniqueIndex)

    if self.data.recordLinks == nil then self.data.recordLinks = {} end

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks[storeType] == nil then recordLinks[storeType] = {} end
        if recordLinks[storeType][recordId] == nil then recordLinks[storeType][recordId] = {} end

        if not tableHelper.containsValue(self.data.recordLinks[storeType][recordId], uniqueIndex) then
            table.insert(self.data.recordLinks[storeType][recordId], uniqueIndex)
        end

        recordStore:AddLinkToCell(recordId, self)
        recordStore:QuicksaveToDrive()
    end
end

function BaseCell:RemoveLinkToRecord(storeType, recordId, uniqueIndex)

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks ~= nil and recordLinks[storeType] ~= nil and recordLinks[storeType][recordId] ~= nil then

            local linkIndex = tableHelper.getIndexByValue(recordLinks[storeType][recordId], uniqueIndex)

            if linkIndex ~= nil then
                recordLinks[storeType][recordId][linkIndex] = nil
            end

            local remainingIndexCount = tableHelper.getCount(recordLinks[storeType][recordId])

            if remainingIndexCount == 0 then
                recordLinks[storeType][recordId] = nil

                recordStore:RemoveLinkToCell(recordId, self)
                recordStore:QuicksaveToDrive()
            end
        end
    end
end

function BaseCell:ClearRecordLinks()

    for storeType, storeLinksTable in pairs(self.data.recordLinks) do

        local recordStore = RecordStores[storeType]

        if recordStore ~= nil then

            for recordId, uniqueIndexes in pairs(storeLinksTable) do

                for _, uniqueIndex in pairs(uniqueIndexes) do

                    self:RemoveLinkToRecord(storeType, recordId, uniqueIndex)
                end
            end
        end
    end
end

function BaseCell:GetVisitorCount()
    return tableHelper.getCount(self.visitors)
end

function BaseCell:AddVisitor(pid, options)

    if type(options) ~= "table" then
        options = {}
    end

    -- Only add new visitor if we don't already have them
    if not tableHelper.containsValue(self.visitors, pid) then
        table.insert(self.visitors, pid)

        -- Also add a record to the player's list of loaded cells
        Players[pid]:AddCellLoaded(self.description)

        if options.skipInitialCellData ~= true then
            self:LoadGeneratedRecords(pid)

            local shouldSendInfo = false
            local visitKey = getPlayerCellVisitKey(pid)
            local lastVisitTimestamp = self.data.lastVisit[visitKey]

            -- If this player has never been in this cell, they should be
            -- sent its cell data
            if lastVisitTimestamp == nil then
                shouldSendInfo = true
            -- Otherwise, send them the cell data only if they haven't
            -- visited since last connecting to the server
            elseif Players[pid].data.timestamps.lastLogin > lastVisitTimestamp then
                shouldSendInfo = true
            end

            if shouldSendInfo == true then
                -- First, fix whatever quest problems exist in this cell
                contentFixer.FixCell(pid, self.description)

                self:LoadInitialCellData(pid)
            end

            self:LoadMomentaryCellData(pid)
        end

        if options.skipContainerRequest ~= true and not self:HasFullContainerData() and not self.isRequestingContainerData then
            tes3mp.LogAppend(enumerations.log.INFO, "- Requesting containers")
            self:RequestContainers(pid)
        end

        if options.skipActorListRequest ~= true and config.cppClientActorAuthority ~= true and
            not self:HasFullActorList() and not self.isRequestingActorList then
            tes3mp.LogAppend(enumerations.log.INFO, "- Requesting actor list")
            local actorListRequestPid = pid

            if self.authority ~= nil and tableHelper.containsValue(self.visitors, self.authority) then
                actorListRequestPid = self.authority
            end

            self:RequestActorList(actorListRequestPid)
        end
    end
end

function BaseCell:RemoveVisitor(pid)

    -- Only remove visitor if they are actually recorded as one
    if tableHelper.containsValue(self.visitors, pid) then

        tableHelper.removeValue(self.visitors, pid)

        -- Also remove the record from the player's list of loaded cells
        Players[pid]:RemoveCellLoaded(self.description)

        -- Remember when this visitor left
        self:SaveLastVisit(getPlayerCellVisitKey(pid))

        -- Were we waiting on a container request from this pid?
        if self.isRequestingContainerData == true and self.containerRequestPid == pid then
            self.isRequestingContainerData = false
            self.containerRequestPid = nil
        end

        -- Were we waiting on an actorList request from this pid?
        if self.isRequestingActorList == true and self.actorListRequestPid == pid then
            self.isRequestingActorList = false
            self.actorListRequestPid = nil
        end
    end
end

function BaseCell:GetAuthority()
    if config.serverAuthoritativeActors == true then
        return nil
    end

    return self.authority
end

function BaseCell:SetAuthority(pid)
    if pid == nil or Players == nil or Players[pid] == nil then
        tes3mp.LogMessage(enumerations.log.WARN, "Refused to set authority of cell " ..
            self.data.entry.description .. " to missing player " .. tostring(pid))
        return false
    end

    if type(Players[pid].IsLoggedIn) == "function" and not Players[pid]:IsLoggedIn() then
        tes3mp.LogMessage(enumerations.log.WARN, "Refused to set authority of cell " ..
            self.data.entry.description .. " to " .. logicHandler.GetChatName(pid) ..
            " because that player is not logged in")
        return false
    end

    if not tableHelper.containsValue(self.visitors, pid) then
        tes3mp.LogMessage(enumerations.log.WARN, "Refused to set authority of cell " ..
            self.data.entry.description .. " to " .. logicHandler.GetChatName(pid) ..
            " because that player is not a visitor")
        return false
    end

    if config.serverAuthoritativeActors == true then
        self.authority = nil
        tes3mp.LogMessage(enumerations.log.INFO, "Skipped client actor authority for cell " ..
            self.data.entry.description .. " because server-authoritative actors are enabled")
        return true
    end

    self.authority = pid
    tes3mp.LogMessage(enumerations.log.INFO, "Authority of cell " .. self.data.entry.description ..
        " is now " .. logicHandler.GetChatName(pid))

    if config.cppClientActorAuthority == true then
        tes3mp.LogMessage(enumerations.log.INFO, "Deferred actor authority packet for cell " ..
            self.data.entry.description .. " because C++ actor authority is enabled")
        return true
    end

    self:LoadActorAuthority(pid)
    return true
end

-- Check whether an object is in this cell
function BaseCell:ContainsObject(uniqueIndex)
    if self.data.objectData[uniqueIndex] ~= nil and self.data.objectData[uniqueIndex].refId ~= nil then
        return true
    end

    return false
end

function BaseCell:IsDuplicateVanillaActorSpawn(uniqueIndex, objectData)
    if objectData ~= nil and objectData.summon ~= nil then
        return false
    end

    if type(uniqueIndex) ~= "string" then
        return false
    end

    local splitIndex = uniqueIndex:split("-")
    local refNum = splitIndex[1]

    if refNum == nil or refNum == "0" then
        return false
    end

    local vanillaActorIndex = refNum .. "-0"

    if vanillaActorIndex == uniqueIndex then
        return false
    end

    return self:ContainsObject(vanillaActorIndex) or
        tableHelper.containsValue(self.data.packets.actorList, vanillaActorIndex)
end

function BaseCell:AcquireObjectInteractionLock(pid, uniqueIndex, refId, lockKind)
    pid = tonumber(pid)
    uniqueIndex = uniqueIndex ~= nil and tostring(uniqueIndex) or nil

    if pid == nil or uniqueIndex == nil or uniqueIndex == "" then
        return false, nil
    end

    local now = os.time()
    purgeExpiredObjectInteractionLocks(now)

    local lockKey = getObjectInteractionLockKey(self.description, uniqueIndex, lockKind)
    local lock = objectInteractionLocks[lockKey]

    if lock ~= nil and lock.pid ~= pid then
        return false, lock.pid
    end

    objectInteractionLocks[lockKey] = {
        pid = pid,
        refId = refId or "",
        uniqueIndex = uniqueIndex,
        cellDescription = self.description,
        lockKind = getObjectInteractionLockKind(lockKind),
        expiresAt = now + objectInteractionLockTtlSeconds
    }

    return true, pid
end

function BaseCell:AcquireObjectInteractionLocks(pid, objects, lockKind)
    local acquiredUniqueIndexes = {}

    for objectKey, object in pairs(objects) do
        local uniqueIndex = getPacketObjectUniqueIndex(objectKey, object)
        local accepted, ownerPid = self:AcquireObjectInteractionLock(pid, uniqueIndex,
            getPacketObjectRefId(object), lockKind)

        if not accepted then
            for _, acquiredUniqueIndex in pairs(acquiredUniqueIndexes) do
                self:ReleaseObjectInteractionLock(pid, acquiredUniqueIndex, lockKind)
            end

            return false, ownerPid, uniqueIndex, object
        end

        table.insert(acquiredUniqueIndexes, uniqueIndex)
    end

    return true, pid
end

function BaseCell:IsObjectInteractionLockedByOther(pid, uniqueIndex, lockKind)
    pid = tonumber(pid)
    uniqueIndex = uniqueIndex ~= nil and tostring(uniqueIndex) or nil

    if pid == nil or uniqueIndex == nil or uniqueIndex == "" then
        return false, nil
    end

    purgeExpiredObjectInteractionLocks()

    local lock = objectInteractionLocks[getObjectInteractionLockKey(self.description, uniqueIndex, lockKind)]
    if lock ~= nil and lock.pid ~= pid then
        return true, lock.pid
    end

    return false, lock ~= nil and lock.pid or nil
end

function BaseCell:ReleaseObjectInteractionLock(pid, uniqueIndex, lockKind)
    pid = tonumber(pid)
    uniqueIndex = uniqueIndex ~= nil and tostring(uniqueIndex) or nil

    if pid == nil or uniqueIndex == nil or uniqueIndex == "" then
        return 0
    end

    local releasedCount = 0

    if lockKind ~= nil then
        local lockKey = getObjectInteractionLockKey(self.description, uniqueIndex, lockKind)
        local lock = objectInteractionLocks[lockKey]

        if lock ~= nil and lock.pid == pid then
            objectInteractionLocks[lockKey] = nil
            releasedCount = releasedCount + 1
        end

        return releasedCount
    end

    for lockKey, lock in pairs(objectInteractionLocks) do
        if lock.pid == pid and lock.cellDescription == self.description and lock.uniqueIndex == uniqueIndex then
            objectInteractionLocks[lockKey] = nil
            releasedCount = releasedCount + 1
        end
    end

    return releasedCount
end

function BaseCell:ReleaseObjectInteractionLocksForPid(pid)
    pid = tonumber(pid)

    if pid == nil then
        return 0
    end

    local releasedCount = 0

    for lockKey, lock in pairs(objectInteractionLocks) do
        if lock.pid == pid and lock.cellDescription == self.description then
            objectInteractionLocks[lockKey] = nil
            releasedCount = releasedCount + 1
        end
    end

    return releasedCount
end

function BaseCell:SendObjectInteractionLockGrant(pid, objects)
    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)
    tes3mp.SetObjectListAction(enumerations.container.REQUEST)
    tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.LOCK_REQUEST)

    local objectCount = 0

    for objectKey, object in pairs(objects) do
        local uniqueIndex = getPacketObjectUniqueIndex(objectKey, object)

        if uniqueIndex ~= nil then
            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetObjectRefNum(splitIndex[1])
            tes3mp.SetObjectMpNum(splitIndex[2])
            tes3mp.SetObjectRefId(getPacketObjectRefId(object))
            tes3mp.AddObject()
            objectCount = objectCount + 1
        end
    end

    if objectCount > 0 then
        tes3mp.SendContainer(false, false)
    end
end

function BaseCell:LoadObjectInteractionLockSnapshots(pid, objects)
    local sharedUniqueIndexes = {}
    local playerScopedObjectData, playerScopedUniqueIndexes = self:GetPlayerScopedContainerPackets(pid)
    local requestedPlayerScopedUniqueIndexes = {}

    for objectKey, object in pairs(objects) do
        local uniqueIndex = getPacketObjectUniqueIndex(objectKey, object)

        if uniqueIndex ~= nil then
            local objectData = self.data.objectData[uniqueIndex]

            if objectData ~= nil and objectData.inventory ~= nil and not isLiveActorObjectData(objectData) then
                table.insert(sharedUniqueIndexes, uniqueIndex)
            elseif playerScopedObjectData[uniqueIndex] ~= nil and playerScopedObjectData[uniqueIndex].inventory ~= nil then
                table.insert(requestedPlayerScopedUniqueIndexes, uniqueIndex)
            elseif objectData ~= nil and objectData.inventory ~= nil then
                tes3mp.LogAppend(enumerations.log.INFO, "- Skipping live actor container snapshot for " ..
                    tostring(objectData.refId) .. " " .. tostring(uniqueIndex))
            end
        end
    end

    if not tableHelper.isEmpty(sharedUniqueIndexes) then
        self:LoadContainers(pid, self.data.objectData, sharedUniqueIndexes)
    end

    if not tableHelper.isEmpty(requestedPlayerScopedUniqueIndexes) then
        self:LoadContainers(pid, playerScopedObjectData, requestedPlayerScopedUniqueIndexes, {
            includePlayerScoped = true
        })
    end
end

function BaseCell:IsPlayerScopedContainer(uniqueIndex, objectData)
    return isPlayerScopedContainer(self.description, uniqueIndex, objectData)
end

function BaseCell:GetPlayerScopedContainerData(pid, uniqueIndex, refId)
    if Players[pid].data.playerScopedContainers == nil then
        Players[pid].data.playerScopedContainers = {}
    end

    local objectData = {
        refId = refId
    }
    local storageCellDescription = getPlayerScopedContainerStorageKey(self.description, uniqueIndex, objectData)

    if storageCellDescription == nil then
        storageCellDescription = self.description
    end

    local cellContainers = Players[pid].data.playerScopedContainers[storageCellDescription]

    if cellContainers == nil then
        cellContainers = {}
        Players[pid].data.playerScopedContainers[storageCellDescription] = cellContainers
    end

    objectData = cellContainers[uniqueIndex]

    if objectData == nil then
        objectData = {
            refId = refId,
            inventory = {}
        }
        cellContainers[uniqueIndex] = objectData
    elseif objectData.refId == nil or objectData.refId == "" then
        objectData.refId = refId
    end

    return objectData
end

function BaseCell:GetPlayerScopedContainerPackets(pid)
    if Players[pid] == nil or Players[pid].data == nil or
        Players[pid].data.playerScopedContainers == nil then
        return {}, {}
    end

    local objectData = {}
    local function mergeCellContainers(cellContainers)
        if type(cellContainers) ~= "table" then
            return
        end

        for uniqueIndex, containerData in pairs(cellContainers) do
            objectData[uniqueIndex] = containerData
        end
    end

    mergeCellContainers(Players[pid].data.playerScopedContainers[self.description])

    local cellConfig, storageCellDescription = getPlayerScopedContainerCellConfig(self.description)

    if cellConfig ~= nil and storageCellDescription ~= self.description then
        mergeCellContainers(Players[pid].data.playerScopedContainers[storageCellDescription])
    end

    if tableHelper.isEmpty(objectData) then
        return {}, {}
    end

    local uniqueIndexes = {}

    for uniqueIndex, containerData in pairs(objectData) do
        if self:IsPlayerScopedContainer(uniqueIndex, containerData) and containerData.inventory ~= nil then
            table.insert(uniqueIndexes, uniqueIndex)
        end
    end

    return objectData, uniqueIndexes
end

function BaseCell:HasFullContainerData()

    if self.data.loadState.hasFullContainerData == true then
        return true
    end

    return false
end

function BaseCell:HasFullActorList()

    if self.data.loadState.hasFullActorList == true then
        return true
    end

    return false
end

function BaseCell:InitializeObjectData(uniqueIndex, refId)

    if uniqueIndex ~= nil and refId ~= nil and self.data.objectData[uniqueIndex] == nil then
        self.data.objectData[uniqueIndex] = {}
        self.data.objectData[uniqueIndex].refId = refId
    end
end

function BaseCell:DeleteObjectData(uniqueIndex)

    if self.data.objectData[uniqueIndex] == nil then
        return
    end

    -- Is this a player's summon? If so, remove it from the summons tracked
    -- for the player
    local summon = self.data.objectData[uniqueIndex].summon

    if summon ~= nil then
        if summon.summoner.playerName ~= nil or summon.summoner.playerKey ~= nil then
            local summoner = getLoggedInPlayerByStorageKey(summon.summoner.playerKey)
            if summoner == nil then
                summoner = logicHandler.GetLoggedInPlayerByName(summon.summoner.playerName)
            end
            if summoner ~= nil then
                summoner.summons[uniqueIndex] = nil
            end
        end
    end

    -- Delete all packets associated with an object
    for packetIndex, packetType in pairs(self.data.packets) do
        tableHelper.removeValue(self.data.packets[packetIndex], uniqueIndex)
    end

    -- Delete all object data
    self.data.objectData[uniqueIndex] = nil
end

function BaseCell:MoveObjectData(uniqueIndex, newCell)

    -- Ensure we're not trying to move the object to the cell it's already in
    if self.description == newCell.description then return end

    -- Move all packets about this uniqueIndex from the old cell to the new cell
    for packetIndex, packetType in pairs(self.data.packets) do

        if tableHelper.containsValue(self.data.packets[packetIndex], uniqueIndex) then

            table.insert(newCell.data.packets[packetIndex], uniqueIndex)
            tableHelper.removeValue(self.data.packets[packetIndex], uniqueIndex)
        end
    end

    newCell.data.objectData[uniqueIndex] = self.data.objectData[uniqueIndex]
    self.data.objectData[uniqueIndex] = nil
end

function BaseCell:SaveLastVisit(playerName)
    self.data.lastVisit[playerName] = os.time()
end

function BaseCell:SaveObjectsDeleted(objects)

    local temporaryLoadedCells = {}

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        -- Check whether this object was moved to this cell from another one
        local wasMovedHere = tableHelper.containsValue(self.data.packets.cellChangeFrom, uniqueIndex)

        if wasMovedHere == true then

            local originalCellDescription = self.data.objectData[uniqueIndex].cellChangeFrom

            -- If the new cell is not loaded, load it temporarily
            if LoadedCells[originalCellDescription] == nil then
                logicHandler.LoadCell(originalCellDescription)
                table.insert(temporaryLoadedCells, originalCellDescription)
            end

            local originalCell = LoadedCells[originalCellDescription]

            originalCell:DeleteObjectData(uniqueIndex)
            table.insert(originalCell.data.packets.delete, uniqueIndex)
            originalCell:InitializeObjectData(uniqueIndex, refId)

            self:DeleteObjectData(uniqueIndex)

        else
            -- Check whether this is a placed or spawned object
            local wasPlacedHere = tableHelper.containsValue(self.data.packets.place, uniqueIndex) or
                tableHelper.containsValue(self.data.packets.spawn, uniqueIndex)

            self:DeleteObjectData(uniqueIndex)

            -- If this is an object from the game's data files, we should keep sending ObjectDelete
            -- packets for it to visitors
            if not wasPlacedHere then

                table.insert(self.data.packets.delete, uniqueIndex)
                self:InitializeObjectData(uniqueIndex, refId)

            -- If this is an object based on a generated record, we need to remove the link to it
            elseif logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:RemoveLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, originalCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(originalCellDescription)
    end
end

function BaseCell:SaveObjectsPlaced(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        -- Ensure data integrity before proceeeding
        if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
            self:ContainsPosition(location.posX, location.posY) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            local count = object.count
            local charge = object.charge
            local enchantmentCharge = object.enchantmentCharge
            local soul = object.soul
            local goldValue = object.goldValue

            -- Only save count if it isn't the default value of 1
            if count ~= 1 then
                self.data.objectData[uniqueIndex].count = count
            end

            -- Only save charge if it isn't the default value of -1
            if charge ~= -1 then
                self.data.objectData[uniqueIndex].charge = charge
            end

            -- Only save enchantment charge if it isn't the default value of -1
            if enchantmentCharge ~= -1 then
                self.data.objectData[uniqueIndex].enchantmentCharge = enchantmentCharge
            end

            if soul ~= "" then
               self.data.objectData[uniqueIndex].soul = soul
            end

            -- Only save goldValue if it isn't the default value of 1
            if goldValue ~= 1 then
                self.data.objectData[uniqueIndex].goldValue = goldValue
            end

            self.data.objectData[uniqueIndex].location = location

            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
                ", count: " .. count .. ", charge: " .. charge .. ", enchantmentCharge: " .. enchantmentCharge ..
                ", soul: " .. soul .. ", goldValue: " .. goldValue)

            table.insert(self.data.packets.place, uniqueIndex)

            if logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveObjectsSpawned(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        if self:IsDuplicateVanillaActorSpawn(uniqueIndex, object) then
            tes3mp.LogAppend(enumerations.log.WARN, "- Rejected duplicate actor ObjectSpawn " .. uniqueIndex ..
                ", refId: " .. object.refId)
        -- Ensure data integrity before proceeeding
        elseif tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
            self:ContainsPosition(location.posX, location.posY) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)

            self.data.objectData[uniqueIndex].location = location

            if object.summon ~= nil then
                local summonDuration = object.summon.duration

                if summonDuration > 0 then
                    local summon = {}
                    summon.duration = object.summon.duration
                    summon.effectId = object.summon.effectId
                    summon.spellId = object.summon.spellId
                    summon.startTime = object.summon.startTime
                    summon.summoner = {}

                    local hasPlayerSummoner = object.summon.hasPlayerSummoner

                    if hasPlayerSummoner then
                        local summonerPid = object.summon.summoner.pid
                        tes3mp.LogAppend(enumerations.log.INFO, "- summoned by player " ..
                            logicHandler.GetChatName(summonerPid))

                        -- Track the player and the summon for each other
                        summon.summoner.playerName = object.summon.summoner.playerName
                        summon.summoner.playerKey = object.summon.summoner.playerKey
                        summon.summoner.accountName = object.summon.summoner.accountName
                        summon.summoner.characterName = object.summon.summoner.characterName

                        Players[summonerPid].summons[uniqueIndex] = refId
                    else
                        summon.summoner.refId = object.summon.summoner.refId
                        summon.summoner.uniqueIndex = object.summon.summoner.uniqueIndex
                        tes3mp.LogAppend(enumerations.log.INFO, "- summoned by actor " .. summon.summoner.uniqueIndex ..
                            ", refId: " .. summon.summoner.refId)
                    end

                    self.data.objectData[uniqueIndex].summon = summon
                end
            end

            table.insert(self.data.packets.spawn, uniqueIndex)
            table.insert(self.data.packets.actorList, uniqueIndex)

            if logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end
end

function BaseCell:SaveObjectsMoved(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        if type(location) == "table" and tableHelper.getCount(location) == 3 and
            tableHelper.usesNumericalValues(location) and self:ContainsPosition(location.posX, location.posY) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            if self.data.objectData[uniqueIndex] ~= nil then
                if self.data.objectData[uniqueIndex].location == nil then
                    self.data.objectData[uniqueIndex].location = {}
                end

                self.data.objectData[uniqueIndex].location.posX = location.posX
                self.data.objectData[uniqueIndex].location.posY = location.posY
                self.data.objectData[uniqueIndex].location.posZ = location.posZ

                tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
                    ", pos: " .. location.posX .. ", " .. location.posY .. ", " .. location.posZ)

                tableHelper.insertValueIfMissing(self.data.packets.move, uniqueIndex)
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveObjectsRotated(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        if type(location) == "table" and tableHelper.getCount(location) == 3 and
            tableHelper.usesNumericalValues(location) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            if self.data.objectData[uniqueIndex] ~= nil then
                if self.data.objectData[uniqueIndex].location == nil then
                    self.data.objectData[uniqueIndex].location = {}
                end

                self.data.objectData[uniqueIndex].location.rotX = location.rotX
                self.data.objectData[uniqueIndex].location.rotY = location.rotY
                self.data.objectData[uniqueIndex].location.rotZ = location.rotZ

                tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
                    ", rot: " .. location.rotX .. ", " .. location.rotY .. ", " .. location.rotZ)

                tableHelper.insertValueIfMissing(self.data.packets.rotate, uniqueIndex)
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveObjectsLocked(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local lockLevel = object.lockLevel

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].lockLevel = lockLevel

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", lockLevel: " .. lockLevel)

        tableHelper.insertValueIfMissing(self.data.packets.lock, uniqueIndex)
    end
end

function BaseCell:SaveObjectsMiscellaneous(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].goldPool = object.goldPool
        self.data.objectData[uniqueIndex].lastGoldRestockHour = object.lastGoldRestockHour
        self.data.objectData[uniqueIndex].lastGoldRestockDay = object.lastGoldRestockDay

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", goldPool: " .. object.goldPool .. ", lastGoldRestockHour: " .. object.lastGoldRestockHour ..
            ", lastGoldRestockDay: " .. object.lastGoldRestockDay)

        tableHelper.insertValueIfMissing(self.data.packets.miscellaneous, uniqueIndex)
    end
end

function BaseCell:SaveObjectTrapsTriggered(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        self:InitializeObjectData(uniqueIndex, refId)

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)

        tableHelper.insertValueIfMissing(self.data.packets.trap, uniqueIndex)
    end
end

function BaseCell:SaveObjectsScaled(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local scale = object.scale

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].scale = scale

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", scale: " .. scale)

        tableHelper.insertValueIfMissing(self.data.packets.scale, uniqueIndex)
    end
end

function BaseCell:SaveObjectStates(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local state = object.state

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].state = state

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", state: " .. tostring(state))

        tableHelper.insertValueIfMissing(self.data.packets.state, uniqueIndex)
    end
end

function BaseCell:SaveDoorStates(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local doorState = object.doorState

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].doorState = doorState

        tableHelper.insertValueIfMissing(self.data.packets.doorState, uniqueIndex)
    end
end

function BaseCell:SaveDoorDestinations(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local hasTeleportDestination = object.teleportState == true and object.doorDestination ~= nil

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].teleportState = hasTeleportDestination

        if hasTeleportDestination then
            self.data.objectData[uniqueIndex].doorDestination = tableHelper.deepCopy(object.doorDestination)
        else
            self.data.objectData[uniqueIndex].doorDestination = nil
        end

        tableHelper.insertValueIfMissing(self.data.packets.doorDestination, uniqueIndex)
    end
end

function BaseCell:SaveClientScriptLocals(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local variables = object.variables

        self:InitializeObjectData(uniqueIndex, refId)

        if self.data.objectData[uniqueIndex].variables == nil then
            self.data.objectData[uniqueIndex].variables = {}
        end

        for variableType, variableTable in pairs(object.variables) do
            if self.data.objectData[uniqueIndex].variables[variableType] == nil then
                self.data.objectData[uniqueIndex].variables[variableType] = {}
            end

            for internalIndex, value in pairs(variableTable) do
                self.data.objectData[uniqueIndex].variables[variableType][internalIndex] = value
            end
        end

        tableHelper.insertValueIfMissing(self.data.packets.clientScriptLocal, uniqueIndex)
    end
end

function BaseCell:SaveContainers(pid)

    tes3mp.ReadReceivedObjectList()
    tes3mp.CopyReceivedObjectListToStore()

    tes3mp.LogMessage(enumerations.log.INFO, "Saving Container from " .. logicHandler.GetChatName(pid) ..
        " about " .. self.description)

    local packetOrigin = tes3mp.GetObjectListOrigin()
    local action = tes3mp.GetObjectListAction()
    local subAction = tes3mp.GetObjectListContainerSubAction()
    local isDialogueBarterContainerTransfer = packetOrigin == enumerations.packetOrigin.CLIENT_DIALOGUE and
        subAction == enumerations.containerSub.BARTER
    local isGameplayContainerTake = packetOrigin == enumerations.packetOrigin.CLIENT_GAMEPLAY and
        (subAction == enumerations.containerSub.DRAG or subAction == enumerations.containerSub.TAKE_ALL) or
        (isDialogueBarterContainerTransfer and action == enumerations.container.REMOVE)
    local isGameplayContainerDrop = packetOrigin == enumerations.packetOrigin.CLIENT_GAMEPLAY and
        subAction == enumerations.containerSub.DROP or
        (isDialogueBarterContainerTransfer and action == enumerations.container.ADD)
    local hasSharedContainerChanges = false
    local hasPlayerScopedContainerChanges = false
    local hasRejectedGameplayContainerDrop = false
    local shouldReloadPlayerInventory = false
    local sharedContainerUniqueIndexes = {}
    local playerScopedContainerUniqueIndexes = {}
    local playerScopedContainerData = {}
    local dialogueBarterPacketLockUniqueIndexes = {}

    local function buildContainerTransactionObjects()
        local objects = {}

        for objectIndex = 0, tes3mp.GetObjectListSize() - 1 do
            local transactionObject = {
                uniqueIndex = tes3mp.GetObjectRefNum(objectIndex) .. "-" .. tes3mp.GetObjectMpNum(objectIndex),
                refId = tes3mp.GetObjectRefId(objectIndex),
                items = {}
            }

            for itemIndex = 0, tes3mp.GetContainerChangesSize(objectIndex) - 1 do
                table.insert(transactionObject.items, {
                    refId = tes3mp.GetContainerItemRefId(objectIndex, itemIndex),
                    count = tes3mp.GetContainerItemCount(objectIndex, itemIndex),
                    charge = tes3mp.GetContainerItemCharge(objectIndex, itemIndex),
                    enchantmentCharge = tes3mp.GetContainerItemEnchantmentCharge(objectIndex, itemIndex),
                    soul = tes3mp.GetContainerItemSoul(objectIndex, itemIndex),
                    actionCount = tes3mp.GetContainerItemActionCount(objectIndex, itemIndex)
                })
            end

            table.insert(objects, transactionObject)
        end

        return objects
    end

    local transactionObjects = buildContainerTransactionObjects()

    local function rememberFailedDialogueBarterTransaction(reason)
        if not isDialogueBarterContainerTransfer or type(Players) ~= "table" or Players[pid] == nil then
            return
        end

        local uniqueIndexes = {}

        for _, object in pairs(transactionObjects) do
            if object.uniqueIndex ~= nil then
                uniqueIndexes[tostring(object.uniqueIndex)] = true
            end
        end

        Players[pid].failedDialogueBarterTransaction = {
            cellDescription = self.description,
            uniqueIndexes = uniqueIndexes,
            reason = reason or "unverifiedInventory",
            expiresAt = os.time() + failedDialogueBarterTransactionTtlSeconds
        }
    end

    local function rejectGameplayContainerDrop(reason)
        hasRejectedGameplayContainerDrop = true

        if isDialogueBarterContainerTransfer then
            rememberFailedDialogueBarterTransaction(reason)
        else
            shouldReloadPlayerInventory = true
        end
    end

    local function releaseDialogueBarterPacketLocks()
        if not isDialogueBarterContainerTransfer then
            return
        end

        for _, uniqueIndex in pairs(dialogueBarterPacketLockUniqueIndexes) do
            self:ReleaseObjectInteractionLock(pid, uniqueIndex, "barter")
        end

        dialogueBarterPacketLockUniqueIndexes = {}
    end

    local function prevalidateDialogueBarterDrop()
        if not isDialogueBarterContainerTransfer or action ~= enumerations.container.ADD then
            return true
        end

        local player = type(Players) == "table" and Players[pid] or nil

        if player == nil or type(player.CanApplyContainerInventoryMirror) ~= "function" then
            rejectGameplayContainerDrop("missingPlayerInventoryMirror")
            tes3mp.LogAppend(enumerations.log.WARN,
                "- Rejected dialogue barter sale because the server cannot verify the saved inventory")
            return false
        end

        local requestedItemsByRefId = {}

        for _, object in pairs(transactionObjects) do
            for _, item in pairs(object.items) do
                local mirrorItem = makeContainerInventoryMirrorItem(item.refId, item.count,
                    item.charge, item.enchantmentCharge, item.soul)

                if mirrorItem == nil then
                    rejectGameplayContainerDrop("invalidBarterItem")
                    tes3mp.LogAppend(enumerations.log.WARN,
                        "- Rejected dialogue barter sale because it contains an invalid item")
                    return false
                end

                local refIdKey = string.lower(mirrorItem.refId)

                if requestedItemsByRefId[refIdKey] == nil then
                    requestedItemsByRefId[refIdKey] = mirrorItem
                else
                    requestedItemsByRefId[refIdKey].count =
                        requestedItemsByRefId[refIdKey].count + mirrorItem.count
                end
            end
        end

        for _, mirrorItem in pairs(requestedItemsByRefId) do
            local canApply, savedCount = player:CanApplyContainerInventoryMirror(
                enumerations.inventory.REMOVE, mirrorItem)

            if not canApply then
                rejectGameplayContainerDrop("insufficientSavedInventory")
                tes3mp.LogAppend(enumerations.log.WARN, "- Rejected dialogue barter sale of " ..
                    mirrorItem.count .. " " .. mirrorItem.refId ..
                    " because the saved inventory only had " .. tostring(savedCount or 0))

                if type(player.Message) == "function" then
                    player:Message("That barter could not be completed because the server could not verify your inventory.\n")
                end

                return false
            end
        end

        return true
    end

    local function getInventoryItemCount(inventory, mirrorItem)
        if type(inventory) ~= "table" or mirrorItem == nil then
            return 0
        end

        if not inventoryHelper.containsItem(inventory, mirrorItem.refId, mirrorItem.charge,
            mirrorItem.enchantmentCharge, mirrorItem.soul) then
            return 0
        end

        local foundIndex = inventoryHelper.getItemIndex(inventory, mirrorItem.refId,
            mirrorItem.charge, mirrorItem.enchantmentCharge, mirrorItem.soul)
        local item = inventory[foundIndex]

        if type(item) ~= "table" or item.count == nil then
            return 0
        end

        return item.count
    end

    local function prevalidateDialogueBarterTake()
        if not isDialogueBarterContainerTransfer or action ~= enumerations.container.REMOVE then
            return true
        end

        for _, object in pairs(transactionObjects) do
            local objectData = self.data.objectData[object.uniqueIndex]

            if objectData == nil or objectData.inventory == nil then
                if self:HasFullContainerData() then
                    rejectGameplayContainerDrop("missingMerchantContainer")
                    tes3mp.LogAppend(enumerations.log.WARN,
                        "- Rejected dialogue barter purchase because the merchant source container was not in the authoritative cell snapshot")
                    return false
                end
            else
                for _, item in pairs(object.items) do
                    local mirrorItem = makeContainerInventoryMirrorItem(item.refId, item.actionCount,
                        item.charge, item.enchantmentCharge, item.soul)

                    if mirrorItem == nil then
                        rejectGameplayContainerDrop("invalidBarterItem")
                        tes3mp.LogAppend(enumerations.log.WARN,
                            "- Rejected dialogue barter purchase because it contains an invalid item")
                        return false
                    end

                    local savedCount = getInventoryItemCount(objectData.inventory, mirrorItem)

                    if savedCount < mirrorItem.count or
                        hasContainerItemTombstone(objectData, mirrorItem.refId, mirrorItem.charge,
                            mirrorItem.enchantmentCharge, mirrorItem.soul) then
                        rejectGameplayContainerDrop("insufficientMerchantInventory")
                        tes3mp.LogAppend(enumerations.log.WARN, "- Rejected dialogue barter purchase of " ..
                            mirrorItem.count .. " " .. mirrorItem.refId ..
                            " because the authoritative merchant source only had " .. tostring(savedCount))

                        if type(Players) == "table" and Players[pid] ~= nil and
                            type(Players[pid].Message) == "function" then
                            Players[pid]:Message("That barter could not be completed because the merchant stock changed.\n")
                        end

                        return false
                    end
                end
            end
        end

        return true
    end

    local function rejectLockedContainerMutation(uniqueIndex, refId, ownerPid)
        tes3mp.LogAppend(enumerations.log.WARN, "- Rejected container mutation for " .. tostring(refId) ..
            " " .. tostring(uniqueIndex) .. " because it is locked by pid " .. tostring(ownerPid))

        if type(Players) == "table" and Players[pid] ~= nil then
            Players[pid]:Message("That container is already in use.\n")

            if not isDialogueBarterContainerTransfer then
                Players[pid]:LoadInventory()
                Players[pid]:LoadEquipment()
            end
        end

        if not isDialogueBarterContainerTransfer and self.data.objectData[uniqueIndex] ~= nil and
            self.data.objectData[uniqueIndex].inventory ~= nil then
            self:LoadContainers(pid, self.data.objectData, { uniqueIndex })
        end
    end

    if action ~= enumerations.container.REQUEST and subAction ~= enumerations.containerSub.REPLY_TO_REQUEST and
        subAction ~= enumerations.containerSub.RESTOCK_RESULT then
        local lockKind = isDialogueBarterContainerTransfer and "barter" or "container"

        for objectIndex = 0, tes3mp.GetObjectListSize() - 1 do
            local uniqueIndex = tes3mp.GetObjectRefNum(objectIndex) .. "-" .. tes3mp.GetObjectMpNum(objectIndex)
            local refId = tes3mp.GetObjectRefId(objectIndex)
            local _, currentOwnerPid = self:IsObjectInteractionLockedByOther(pid, uniqueIndex, lockKind)
            local accepted, ownerPid = self:AcquireObjectInteractionLock(pid, uniqueIndex, refId, lockKind)

            if not accepted then
                rejectLockedContainerMutation(uniqueIndex, refId, ownerPid)
                releaseDialogueBarterPacketLocks()
                return false
            end

            if isDialogueBarterContainerTransfer and currentOwnerPid ~= pid then
                table.insert(dialogueBarterPacketLockUniqueIndexes, uniqueIndex)
            end
        end
    end

    if not prevalidateDialogueBarterDrop() or not prevalidateDialogueBarterTake() then
        releaseDialogueBarterPacketLocks()
        return false
    end

    if subAction ~= enumerations.containerSub.REPLY_TO_REQUEST and
        not itemTransactionJournal.recordContainerPacket(self, pid, action, subAction, packetOrigin,
            transactionObjects, { source = "acceptedContainerPacket" }) then
        releaseDialogueBarterPacketLocks()

        if not isDialogueBarterContainerTransfer and type(Players) == "table" and Players[pid] ~= nil then
            Players[pid]:LoadInventory()
            Players[pid]:LoadEquipment()
        end

        return
    end

    local function mirrorGameplayContainerTake(itemRefId, count, itemCharge, itemEnchantmentCharge, itemSoul)
        if not isGameplayContainerTake or type(Players) ~= "table" or Players[pid] == nil or
            type(Players[pid].ApplyContainerInventoryMirror) ~= "function" then
            return
        end

        local mirrorItem = makeContainerInventoryMirrorItem(itemRefId, count,
            itemCharge, itemEnchantmentCharge, itemSoul)

        if mirrorItem ~= nil then
            Players[pid]:ApplyContainerInventoryMirror(enumerations.inventory.ADD, mirrorItem)
        end
    end

    local function getAcceptedGameplayContainerDrop(itemRefId, count, itemCharge, itemEnchantmentCharge, itemSoul)
        if not isGameplayContainerDrop then
            return true, nil
        end

        local player = type(Players) == "table" and Players[pid] or nil
        local mirrorItem = makeContainerInventoryMirrorItem(itemRefId, count,
            itemCharge, itemEnchantmentCharge, itemSoul)

        if player == nil or mirrorItem == nil or
            type(player.CanApplyContainerInventoryMirror) ~= "function" or
            type(player.ApplyContainerInventoryMirror) ~= "function" then
            rejectGameplayContainerDrop("missingPlayerInventoryMirror")
            return false, nil
        end

        local canApply = player:CanApplyContainerInventoryMirror(enumerations.inventory.REMOVE, mirrorItem)

        if not canApply then
            rejectGameplayContainerDrop("insufficientSavedInventory")
            tes3mp.LogAppend(enumerations.log.WARN, "- Rejected gameplay container drop of " ..
                mirrorItem.count .. " " .. mirrorItem.refId .. " because the saved inventory cannot fund it")
            return false, nil
        end

        return true, mirrorItem
    end

    local function mirrorGameplayContainerDrop(mirrorItem)
        if not isGameplayContainerDrop or mirrorItem == nil then
            return true
        end

        if type(Players) ~= "table" or Players[pid] == nil then
            rejectGameplayContainerDrop("missingPlayer")
            return false
        end

        local applied = Players[pid]:ApplyContainerInventoryMirror(enumerations.inventory.REMOVE, mirrorItem)

        if not applied then
            rejectGameplayContainerDrop("mirrorApplyFailed")
            return false
        end

        return true
    end

    local function containerPacketTouchesPlayerScopedItem(objectIndex)
        for itemIndex = 0, tes3mp.GetContainerChangesSize(objectIndex) - 1 do
            if isPlayerScopedContainerItem(tes3mp.GetContainerItemRefId(objectIndex, itemIndex)) then
                return true
            end
        end

        return false
    end

    for objectIndex = 0, tes3mp.GetObjectListSize() - 1 do

        local uniqueIndex = tes3mp.GetObjectRefNum(objectIndex) .. "-" .. tes3mp.GetObjectMpNum(objectIndex)
        local refId = tes3mp.GetObjectRefId(objectIndex)

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)

        local objectData = {
            refId = refId
        }
        local isScopedContainer = self:IsPlayerScopedContainer(uniqueIndex, objectData) or
            containerPacketTouchesPlayerScopedItem(objectIndex)

        if isScopedContainer then
            objectData = self:GetPlayerScopedContainerData(pid, uniqueIndex, refId)
            hasPlayerScopedContainerChanges = true
            playerScopedContainerData[uniqueIndex] = objectData
            tableHelper.insertValueIfMissing(playerScopedContainerUniqueIndexes, uniqueIndex)
        else
            self:InitializeObjectData(uniqueIndex, refId)
            objectData = self.data.objectData[uniqueIndex]
            tableHelper.insertValueIfMissing(self.data.packets.container, uniqueIndex)
            hasSharedContainerChanges = true
            tableHelper.insertValueIfMissing(sharedContainerUniqueIndexes, uniqueIndex)
        end

        local inventory = objectData.inventory

        -- If this object's inventory is nil, or if the action is SET,
        -- change the inventory to an empty table
        if action == enumerations.container.SET then
            inventory = {}
            objectData.containerTombstones = nil
        elseif inventory == nil then
            inventory = {}
        end

        for itemIndex = 0, tes3mp.GetContainerChangesSize(objectIndex) - 1 do

            local itemRefId = tes3mp.GetContainerItemRefId(objectIndex, itemIndex)
            local itemCount = tes3mp.GetContainerItemCount(objectIndex, itemIndex)
            local itemCharge = tes3mp.GetContainerItemCharge(objectIndex, itemIndex)
            local itemEnchantmentCharge = tes3mp.GetContainerItemEnchantmentCharge(objectIndex, itemIndex)
            local itemSoul = tes3mp.GetContainerItemSoul(objectIndex, itemIndex)
            local actionCount = tes3mp.GetContainerItemActionCount(objectIndex, itemIndex)

            -- Check if the object's stored inventory contains this item already
            if inventoryHelper.containsItem(inventory, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul) then
                local foundIndex = inventoryHelper.getItemIndex(inventory, itemRefId, itemCharge,
                    itemEnchantmentCharge, itemSoul)
                local item = inventory[foundIndex]

                if action == enumerations.container.ADD then
                    local canAcceptDrop, mirrorItem = getAcceptedGameplayContainerDrop(itemRefId, itemCount,
                        itemCharge, itemEnchantmentCharge, itemSoul)

                    if canAcceptDrop and mirrorGameplayContainerDrop(mirrorItem) then
                        tes3mp.LogAppend(enumerations.log.VERBOSE, "- Adding count of " .. itemCount .. " to existing item " ..
                            item.refId .. " with current count of " .. item.count)
                        item.count = item.count + itemCount
                        clearContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)
                    end

                elseif action == enumerations.container.REMOVE then
                    local newCount = item.count - actionCount

                    -- The item will still exist in the container with a lower count
                    if newCount > 0 then
                        tes3mp.LogAppend(enumerations.log.VERBOSE, "- Removed count of " .. actionCount .. " from item " ..
                            item.refId .. " that had count of " .. item.count .. ", resulting in remaining count of " .. newCount)
                        item.count = newCount
                        mirrorGameplayContainerTake(itemRefId, actionCount,
                            itemCharge, itemEnchantmentCharge, itemSoul)
                    -- The item is to be completely removed
                    elseif newCount == 0 then
                        inventory[foundIndex] = nil
                        addContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)
                        mirrorGameplayContainerTake(itemRefId, actionCount,
                            itemCharge, itemEnchantmentCharge, itemSoul)
                    else
                        if isGameplayContainerTake and
                            not hasContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul) then
                            tes3mp.LogAppend(enumerations.log.INFO, "- Accepting visible gameplay removal of " ..
                                actionCount .. " from stale item " .. item.refId .. " that had stored count of " ..
                                item.count)
                            inventory[foundIndex] = nil
                            addContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)
                            mirrorGameplayContainerTake(itemRefId, actionCount,
                                itemCharge, itemEnchantmentCharge, itemSoul)
                        else
                            actionCount = item.count
                            tes3mp.LogAppend(enumerations.log.WARN, "- Attempt to remove count of " .. actionCount ..
                                " from item" .. item.refId .. " that only had count of " .. item.count)
                            tes3mp.LogAppend(enumerations.log.WARN, "- Removed just " .. actionCount .. " instead")
                            tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, actionCount)
                            inventory[foundIndex] = nil
                            addContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)
                            mirrorGameplayContainerTake(itemRefId, actionCount,
                                itemCharge, itemEnchantmentCharge, itemSoul)
                        end
                    end

                    -- Is this a generated record? If so, remove the link to it
                    if not isScopedContainer and inventory[foundIndex] == nil and
                        logicHandler.IsGeneratedRecord(itemRefId) then
                        local recordStore = logicHandler.GetRecordStoreByRecordId(itemRefId)

                        if recordStore ~= nil then
                            self:RemoveLinkToRecord(recordStore.storeType, itemRefId, uniqueIndex)
                        end
                    end
                end
            else
                if action == enumerations.container.REMOVE then
                    local allowUnknownContainerRemove = actionCount > 0 and
                        (not self:HasFullContainerData() or isGameplayContainerTake) and
                        not hasContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)

                    if allowUnknownContainerRemove then
                        local inferredCount = itemCount

                        if inferredCount == nil or inferredCount < actionCount then
                            inferredCount = actionCount
                        end

                        local remainingCount = inferredCount - actionCount

                        tes3mp.LogAppend(enumerations.log.INFO, "- Accepting first-touch removal of " ..
                            actionCount .. " from unsnapshotted item " .. itemRefId)

                        if remainingCount > 0 then
                            inventoryHelper.addItem(inventory, itemRefId, remainingCount,
                                itemCharge, itemEnchantmentCharge, itemSoul)
                        else
                            addContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)
                        end
                        mirrorGameplayContainerTake(itemRefId, actionCount,
                            itemCharge, itemEnchantmentCharge, itemSoul)
                    else
                        tes3mp.LogAppend(enumerations.log.WARN, "- Attempt to remove count of " .. actionCount ..
                            " from non-existent item " .. itemRefId)
                        tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, 0)
                    end
                else
                    local canAcceptDrop, mirrorItem = getAcceptedGameplayContainerDrop(itemRefId, itemCount,
                        itemCharge, itemEnchantmentCharge, itemSoul)

                    if canAcceptDrop and mirrorGameplayContainerDrop(mirrorItem) then
                        tes3mp.LogAppend(enumerations.log.VERBOSE, "- Added new item " .. itemRefId .. " with count of " ..
                            itemCount)
                        inventoryHelper.addItem(inventory, itemRefId, itemCount,
                            itemCharge, itemEnchantmentCharge, itemSoul)
                        clearContainerItemTombstone(objectData, itemRefId, itemCharge, itemEnchantmentCharge, itemSoul)

                        -- Is this a generated record? If so, add a link to it
                        if not isScopedContainer and logicHandler.IsGeneratedRecord(itemRefId) then
                            local recordStore = logicHandler.GetRecordStoreByRecordId(itemRefId)

                            if recordStore ~= nil then
                                self:AddLinkToRecord(recordStore.storeType, itemRefId, uniqueIndex)
                            end
                        end
                    end
                end
            end
        end

        tableHelper.cleanNils(inventory)
        objectData.inventory = inventory
    end

    if hasPlayerScopedContainerChanges and Players[pid] ~= nil then
        Players[pid]:QuicksaveToDrive()
    end

    if hasSharedContainerChanges then
        -- Is this a player replying to our request for container contents?
        -- If so, only send the reply to other players, unless it also contains
        -- player-scoped containers that should not be shared.
        if subAction == enumerations.containerSub.REPLY_TO_REQUEST then
            if hasPlayerScopedContainerChanges then
                self:LoadContainers(pid, self.data.objectData, sharedContainerUniqueIndexes, {
                    sendToOtherPlayers = true,
                    skipAttachedPlayer = true
                })
            else
                tes3mp.SendContainer(true, true)
            end
        elseif hasRejectedGameplayContainerDrop then
            if not isDialogueBarterContainerTransfer then
                self:LoadContainers(pid, self.data.objectData, sharedContainerUniqueIndexes, {
                    sendToOtherPlayers = true,
                    skipAttachedPlayer = false
                })

                if hasPlayerScopedContainerChanges then
                    self:LoadContainers(pid, playerScopedContainerData, playerScopedContainerUniqueIndexes, {
                        includePlayerScoped = true
                    })
                end
            end
        -- Is this a container packet originating from a client script or
        -- dialogue? If so, its effects have already taken place on the
        -- sending client, so only send it to other players
        elseif packetOrigin == enumerations.packetOrigin.CLIENT_SCRIPT_LOCAL or
            packetOrigin == enumerations.packetOrigin.CLIENT_SCRIPT_GLOBAL or
            packetOrigin == enumerations.packetOrigin.CLIENT_DIALOGUE then
            if hasPlayerScopedContainerChanges then
                self:LoadContainers(pid, self.data.objectData, sharedContainerUniqueIndexes, {
                    sendToOtherPlayers = true,
                    skipAttachedPlayer = true
                })
                self:LoadContainers(pid, playerScopedContainerData, playerScopedContainerUniqueIndexes, {
                    includePlayerScoped = true
                })
            else
                tes3mp.SendContainer(true, true)
            end
        -- Otherwise, send the received packet to everyone, including the
        -- player who sent it (because no clientside changes will be made
        -- to the related container otherwise)
        -- i.e. sendToOtherPlayers is true and skipAttachedPlayer is false
        else
            if hasPlayerScopedContainerChanges then
                -- Mixed packets need two audiences: the original delta is
                -- echoed only to the sender so quest/private containers and
                -- local inventory effects are corrected, then a server-built
                -- shared snapshot is sent to the other cell observers.
                tes3mp.SendContainer(false, false)
                self:LoadContainers(pid, self.data.objectData, sharedContainerUniqueIndexes, {
                    sendToOtherPlayers = true,
                    skipAttachedPlayer = true
                })
            else
                tes3mp.SendContainer(true, false)
            end
        end

        if not (hasRejectedGameplayContainerDrop and isDialogueBarterContainerTransfer) then
            self:QuicksaveToDrive()
        end
    elseif hasPlayerScopedContainerChanges and subAction ~= enumerations.containerSub.REPLY_TO_REQUEST then
        if hasRejectedGameplayContainerDrop then
            if not isDialogueBarterContainerTransfer then
                self:LoadContainers(pid, playerScopedContainerData, playerScopedContainerUniqueIndexes, {
                    includePlayerScoped = true
                })
            end
        else
            tes3mp.SendContainer(false, false)
        end
    end

    if shouldReloadPlayerInventory and type(Players) == "table" and Players[pid] ~= nil then
        Players[pid]:LoadInventory()
        Players[pid]:LoadEquipment()
    end

    releaseDialogueBarterPacketLocks()

    -- Were we waiting on a full container data request from this pid?
    if self.isRequestingContainerData == true and self.containerRequestPid == pid and
        subAction == enumerations.containerSub.REPLY_TO_REQUEST then
        self.isRequestingContainerData = false
        self.containerRequestPid = nil
        self.data.loadState.hasFullContainerData = true

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. self.description ..
            " is now recorded as having full container data")
    end
end

function BaseCell:SaveActorsByPacketType(packetType, actors, pid)

    if packetType == "ActorList" then
        self:SaveActorList(actors, pid)
    elseif packetType == "ActorEquipment" then
        self:SaveActorEquipment(actors)
    elseif packetType == "ActorSpellsActive" then
        self:SaveActorSpellsActive(actors)
    elseif packetType == "ActorDeath" then
        self:SaveActorDeath(actors)
    end
end

function BaseCell:SaveObjectsByPacketType(packetType, objects, pid)

    if (packetType == "ObjectPlace" or packetType == "ObjectDelete") and
        not itemTransactionJournal.recordObjectPacket(self, pid, packetType, objects, {
            source = "acceptedObjectPacket"
        }) then
        return false
    end

    if packetType == "ObjectPlace" then
        self:SaveObjectsPlaced(objects)
    elseif packetType == "ObjectSpawn" then
        self:SaveObjectsSpawned(objects)
    elseif packetType == "ObjectMove" then
        self:SaveObjectsMoved(objects)
    elseif packetType == "ObjectRotate" then
        self:SaveObjectsRotated(objects)
    elseif packetType == "ObjectDelete" then
        self:SaveObjectsDeleted(objects)
    elseif packetType == "ObjectLock" then
        self:SaveObjectsLocked(objects)
    elseif packetType == "ObjectMiscellaneous" then
        self:SaveObjectsMiscellaneous(objects)
    elseif packetType == "ObjectTrap" then
        self:SaveObjectTrapsTriggered(objects)
    elseif packetType == "ObjectScale" then
        self:SaveObjectsScaled(objects)
    elseif packetType == "ObjectState" then
        self:SaveObjectStates(objects)
    elseif packetType == "DoorDestination" then
        self:SaveDoorDestinations(objects)
    elseif packetType == "DoorState" then
        self:SaveDoorStates(objects)
    elseif packetType == "ClientScriptLocal" then
        self:SaveClientScriptLocals(objects)
    end

    return true
end

function BaseCell:SaveActorList(actors, pid)

    local isNativeCppActorListSnapshot = config.cppClientActorAuthority == true and
        self.data.loadState.hasFullActorList ~= true and pid ~= nil and self.authority == pid

    if self.isRequestingActorList == true and pid ~= nil and self.actorListRequestPid ~= pid then
        tes3mp.LogAppend(enumerations.log.WARN, "- Rejected ActorList snapshot for " .. self.description ..
            " from unexpected pid " .. tostring(pid))
        return
    end

    for uniqueIndex, actor in pairs(actors) do

        self:InitializeObjectData(uniqueIndex, actor.refId)
        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. actor.refId)

        tableHelper.insertValueIfMissing(self.data.packets.actorList, uniqueIndex)
    end

    self:QuicksaveToDrive()

    -- Were we waiting on an actor list request from this pid?
    if self.isRequestingActorList == true or isNativeCppActorListSnapshot then
        self.isRequestingActorList = false
        self.actorListRequestPid = nil
        self.data.loadState.hasFullActorList = true

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. self.description ..
            " is now recorded as having a full actor list")
    end
end

function BaseCell:SaveActorPositions()

    tes3mp.ReadCellActorList(self.description)
    local actorListSize = tes3mp.GetActorListSize()

    if actorListSize == 0 then
        return
    end

    for objectIndex = 0, actorListSize - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(objectIndex) .. "-" .. tes3mp.GetActorMpNum(objectIndex)

        if tes3mp.DoesActorHavePosition(objectIndex) == true and self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].location = {
                posX = tes3mp.GetActorPosX(objectIndex),
                posY = tes3mp.GetActorPosY(objectIndex),
                posZ = tes3mp.GetActorPosZ(objectIndex),
                rotX = tes3mp.GetActorRotX(objectIndex),
                rotY = tes3mp.GetActorRotY(objectIndex),
                rotZ = tes3mp.GetActorRotZ(objectIndex)
            }

            tableHelper.insertValueIfMissing(self.data.packets.position, uniqueIndex)
        end
    end
end

function BaseCell:SaveActorStatsDynamic()

    tes3mp.ReadCellActorList(self.description)
    local actorListSize = tes3mp.GetActorListSize()

    if actorListSize == 0 then
        return
    end

    for objectIndex = 0, actorListSize - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(objectIndex) .. "-" .. tes3mp.GetActorMpNum(objectIndex)

        if tes3mp.DoesActorHaveStatsDynamic(objectIndex) == true and self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].stats = {
                healthBase = tes3mp.GetActorHealthBase(objectIndex),
                healthCurrent = tes3mp.GetActorHealthCurrent(objectIndex),
                healthModified = tes3mp.GetActorHealthModified(objectIndex),
                magickaBase = tes3mp.GetActorMagickaBase(objectIndex),
                magickaCurrent = tes3mp.GetActorMagickaCurrent(objectIndex),
                magickaModified = tes3mp.GetActorMagickaModified(objectIndex),
                fatigueBase = tes3mp.GetActorFatigueBase(objectIndex),
                fatigueCurrent = tes3mp.GetActorFatigueCurrent(objectIndex),
                fatigueModified = tes3mp.GetActorFatigueModified(objectIndex)
            }

            tableHelper.insertValueIfMissing(self.data.packets.statsDynamic, uniqueIndex)
        end
    end
end

function BaseCell:SaveActorAI()

    tes3mp.ReadCellActorList(self.description)
    local actorListSize = tes3mp.GetActorListSize()

    if actorListSize == 0 then
        return
    end

    for objectIndex = 0, actorListSize - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(objectIndex) .. "-" .. tes3mp.GetActorMpNum(objectIndex)

        if tes3mp.DoesActorHaveAI(objectIndex) == true and self:ContainsObject(uniqueIndex) then

            local action = tes3mp.GetActorAIAction(objectIndex)

            if action == enumerations.ai.CANCEL or action == enumerations.ai.ACTIVATE then
                self.data.objectData[uniqueIndex].ai = nil
                tableHelper.removeValue(self.data.packets.ai, uniqueIndex)
            else
                local targetPid
                local targetUniqueIndex

                if tes3mp.DoesActorHaveAITarget(objectIndex) == true then
                    if tes3mp.DoesActorAITargetPlayer(objectIndex) == true then
                        local candidatePid = tes3mp.GetActorAITargetPid(objectIndex)
                        if candidatePid >= 0 then
                            targetPid = candidatePid
                        end
                    else
                        targetUniqueIndex = tes3mp.GetActorAITargetRefNum(objectIndex) .. "-" ..
                            tes3mp.GetActorAITargetMpNum(objectIndex)
                    end
                end

                local posX
                local posY
                local posZ
                if action == enumerations.ai.TRAVEL or action == enumerations.ai.ESCORT then
                    posX = tes3mp.GetActorAIPosX(objectIndex)
                    posY = tes3mp.GetActorAIPosY(objectIndex)
                    posZ = tes3mp.GetActorAIPosZ(objectIndex)
                end

                local distance
                if action == enumerations.ai.WANDER then
                    distance = tes3mp.GetActorAIDistance(objectIndex)
                end

                local duration
                if action == enumerations.ai.WANDER or action == enumerations.ai.ESCORT then
                    duration = tes3mp.GetActorAIDuration(objectIndex)
                end

                self.data.objectData[uniqueIndex].ai = dataTableBuilder.BuildAIData(targetPid, targetUniqueIndex,
                    action, posX, posY, posZ, distance, duration, tes3mp.GetActorAIRepetition(objectIndex))
                tableHelper.insertValueIfMissing(self.data.packets.ai, uniqueIndex)
            end
        end
    end
end

function BaseCell:SaveActorEquipment(actors)

    for uniqueIndex, actor in pairs(actors) do

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex)

        if self:ContainsObject(uniqueIndex) then
            self.data.objectData[uniqueIndex].equipment = {}

            for equipmentIndex, item in pairs(actor.equipment) do
                self.data.objectData[uniqueIndex].equipment[equipmentIndex] = item
            end

            tableHelper.insertValueIfMissing(self.data.packets.equipment, uniqueIndex)
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveActorSpellsActive(actors)

    for uniqueIndex, actor in pairs(actors) do

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex)

        if self:ContainsObject(uniqueIndex) then

            local action = actor.spellActiveChangesAction

            if action == enumerations.spellbook.SET or self.data.objectData[uniqueIndex].spellsActive == nil then
                self.data.objectData[uniqueIndex].spellsActive = {}
            end

            for spellId, spellInstances in pairs(actor.spellsActive) do

                if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                    if self.data.objectData[uniqueIndex].spellsActive[spellId] == nil then
                        self.data.objectData[uniqueIndex].spellsActive[spellId] = {}
                    end

                    for _, spellInstanceValues in pairs(spellInstances) do

                        local spellInstanceIndex

                        -- Get an unused spellInstanceIndex if this is a spell with stacking effects
                        if spellInstanceValues.stackingState then
                            spellInstanceIndex = tableHelper.getUnusedNumericalIndex(
                                self.data.objectData[uniqueIndex].spellsActive[spellId])
                        -- Otherwise, replace what's under index 1
                        else
                            spellInstanceIndex = 1
                        end

                        self.data.objectData[uniqueIndex].spellsActive[spellId][spellInstanceIndex] = {
                            displayName = spellInstanceValues.displayName,
                            stackingState = spellInstanceValues.stackingState,
                            effects = tableHelper.deepCopy(spellInstanceValues.effects),
                            startTime = os.time()
                        }

                        if spellInstanceValues.caster ~= nil then
                            self.data.objectData[uniqueIndex].spellsActive[spellId][spellInstanceIndex].caster = {
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
                    if self.data.objectData[uniqueIndex].spellsActive[spellId] ~= nil then
                        self.data.objectData[uniqueIndex].spellsActive[spellId][1] = nil

                        if tableHelper.getCount(self.data.objectData[uniqueIndex].spellsActive[spellId]) == 0 then
                            self.data.objectData[uniqueIndex].spellsActive[spellId] = nil
                        end
                    end
                end
            end

            if action == enumerations.spellbook.REMOVE then
                tableHelper.cleanNils(self.data.objectData[uniqueIndex].spellsActive)
            end

            if tableHelper.getCount(self.data.objectData[uniqueIndex].spellsActive) > 0 then
                tableHelper.insertValueIfMissing(self.data.packets.spellsActive, uniqueIndex)
            else
                tableHelper.removeValue(self.data.packets.spellsActive, uniqueIndex)
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveActorDeath(actors)

    if self.data.packets.death == nil then
        self.data.packets.death = {}
    end

    for uniqueIndex, actor in pairs(actors) do

        if self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].deathState = actor.deathState

            if actor.killer.pid ~= nil then
                self.data.objectData[uniqueIndex].killer = {
                    playerName = actor.killer.playerName,
                    playerKey = actor.killer.playerKey,
                    accountName = actor.killer.accountName,
                    characterName = actor.killer.characterName
                }
            elseif actor.killer.name ~= "" then
                self.data.objectData[uniqueIndex].killer = {
                    refId = actor.killer.refId,
                    uniqueIndex = actor.killer.uniqueIndex
                }
            end

            tableHelper.insertValueIfMissing(self.data.packets.death, uniqueIndex)
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveActorCellChanges(pid)

    local temporaryLoadedCells = {}

    tes3mp.ReadReceivedActorList()
    tes3mp.LogMessage(enumerations.log.INFO, "Saving ActorCellChange from " .. logicHandler.GetChatName(pid) ..
        " about " .. self.description)

    for actorIndex = 0, tes3mp.GetActorListSize() - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(actorIndex) .. "-" .. tes3mp.GetActorMpNum(actorIndex)
        local newCellDescription = tes3mp.GetActorCell(actorIndex)

        if newCellDescription == self.description then
            tes3mp.LogAppend(enumerations.log.INFO, "- Ignored invalid cell change that was moving " .. uniqueIndex .. " to " ..
                self.description .. " despite that actor already being in that cell")
        else
            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. " moved to " .. newCellDescription)

            -- If the new cell is not loaded, load it temporarily
            if LoadedCells[newCellDescription] == nil then
                logicHandler.LoadCell(newCellDescription)
                table.insert(temporaryLoadedCells, newCellDescription)
            end

            local newCell = LoadedCells[newCellDescription]

            -- Only proceed if this Actor is actually supposed to exist in this cell
            if self.data.objectData[uniqueIndex] ~= nil then

                -- Was this actor spawned in the old cell, instead of being a pre-existing actor?
                -- If so, delete it entirely from the old cell and make it get spawned in the new cell
                if tableHelper.containsValue(self.data.packets.spawn, uniqueIndex) == true then
                    tes3mp.LogAppend(enumerations.log.INFO, "-- As a server-only object, it was moved entirely")

                    -- If this object is based on a generated record, move its record link
                    -- to the new cell
                    local refId = self.data.objectData[uniqueIndex].refId

                    if logicHandler.IsGeneratedRecord(refId) then

                        local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                        if recordStore ~= nil then
                            newCell:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                            self:RemoveLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                        end

                        -- Send this generated record to every visitor in the new cell
                        for _, visitorPid in pairs(newCell.visitors) do
                            if pid ~= visitorPid then
                                recordStore:LoadGeneratedRecords(visitorPid, recordStore.data.generatedRecords, { refId })
                            end
                        end
                    end

                    -- This actor won't exist at all for players who have not loaded the actor's original
                    -- cell and were not online when it was first spawned, so send all of its details to them
                    for _, player in pairs(Players) do
                        if pid ~= player.pid and not tableHelper.containsValue(self.visitors, player.pid) then
                            self:LoadActorPackets(player.pid, self.data.objectData, { uniqueIndex })
                        end
                    end

                    self:MoveObjectData(uniqueIndex, newCell)

                -- Was this actor moved to the old cell from another cell?
                elseif tableHelper.containsValue(self.data.packets.cellChangeFrom, uniqueIndex) == true then

                    local originalCellDescription = self.data.objectData[uniqueIndex].cellChangeFrom

                    -- Is the new cell actually this actor's original cell?
                    -- If so, move its data back and remove all of its cell change data
                    if originalCellDescription == newCellDescription then
                        tes3mp.LogAppend(enumerations.log.INFO, "-- It is now back in its original cell " .. originalCellDescription)
                        self:MoveObjectData(uniqueIndex, newCell)

                        tableHelper.removeValue(newCell.data.packets.cellChangeTo, uniqueIndex)
                        tableHelper.removeValue(newCell.data.packets.cellChangeFrom, uniqueIndex)

                        newCell.data.objectData[uniqueIndex].cellChangeTo = nil
                        newCell.data.objectData[uniqueIndex].cellChangeFrom = nil
                    -- Otherwise, move its data to the new cell, delete it from the old cell, and update its
                    -- information in its original cell
                    else
                        self:MoveObjectData(uniqueIndex, newCell)

                        -- If the original cell is not loaded, load it temporarily
                        if LoadedCells[originalCellDescription] == nil then
                            logicHandler.LoadCell(originalCellDescription)
                            table.insert(temporaryLoadedCells, originalCellDescription)
                        end

                        local originalCell = LoadedCells[originalCellDescription]

                        if originalCell.data.objectData[uniqueIndex] ~= nil then
                            tes3mp.LogAppend(enumerations.log.INFO, "-- This is now referenced in its original cell " ..
                                originalCellDescription)
                            originalCell.data.objectData[uniqueIndex].cellChangeTo = newCellDescription
                        else
                            tes3mp.LogAppend(enumerations.log.ERROR, "-- It does not exist in its original cell " ..
                                originalCellDescription .. "! Please report this to a developer")
                        end
                    end

                -- Otherwise, simply move this actor's data to the new cell and mark it as being moved there
                -- in its old cell, as long as it's not supposed to already be in the new cell
                elseif self.data.objectData[uniqueIndex].cellChangeTo ~= newCellDescription then

                    tes3mp.LogAppend(enumerations.log.INFO, "-- This was its first move away from its original cell")

                    self:MoveObjectData(uniqueIndex, newCell)

                    table.insert(self.data.packets.cellChangeTo, uniqueIndex)

                    if self.data.objectData[uniqueIndex] == nil then
                        self.data.objectData[uniqueIndex] = {}
                    end

                    self.data.objectData[uniqueIndex].cellChangeTo = newCellDescription

                    table.insert(newCell.data.packets.cellChangeFrom, uniqueIndex)

                    newCell.data.objectData[uniqueIndex].cellChangeFrom = self.description
                end

                if newCell.data.objectData[uniqueIndex] ~= nil then
                    newCell.data.objectData[uniqueIndex].location = {
                        posX = tes3mp.GetActorPosX(actorIndex),
                        posY = tes3mp.GetActorPosY(actorIndex),
                        posZ = tes3mp.GetActorPosZ(actorIndex),
                        rotX = tes3mp.GetActorRotX(actorIndex),
                        rotY = tes3mp.GetActorRotY(actorIndex),
                        rotZ = tes3mp.GetActorRotZ(actorIndex)
                    }
                end
            else
                tes3mp.LogAppend(enumerations.log.ERROR, "-- Invalid cell change was attempted! Please report " ..
                    "this to a developer")
            end
        end
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, newCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(newCellDescription)
    end

    self:QuicksaveToDrive()
end

function BaseCell:LoadActorPackets(pid, objectData, uniqueIndexArray)

    local packets = self.data.packets

    self:LoadObjectsDeleted(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.delete))
    self:LoadObjectsSpawned(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.spawn))
    self:LoadObjectsScaled(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.scale))

    self:LoadContainers(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.container))

    self:LoadActorPositions(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.position))
    self:LoadActorDeath(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.statsDynamic))
    self:LoadActorStatsDynamic(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.statsDynamic))
    self:LoadActorEquipment(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.equipment))
    self:LoadActorAI(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.ai))
end

function BaseCell:LoadObjectsDeleted(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        packetBuilder.AddObjectDelete(uniqueIndex, objectData[uniqueIndex])
        objectCount = objectCount + 1
    end

    if objectCount > 0 then
        tes3mp.SendObjectDelete(forEveryone)
    end
end

function BaseCell:LoadObjectsPlaced(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then

            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if type(location) == "table" and tableHelper.getCount(location) == 6 and
                tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                packetBuilder.AddObjectPlace(uniqueIndex, objectData[uniqueIndex])
                objectCount = objectCount + 1
            else
                objectData[uniqueIndex] = nil
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end

            -- If we're about to exceed the maximum number of objects in a single packet,
            -- start a new packet
            if objectCount >= 3000 then
                tes3mp.SendObjectPlace()
                tes3mp.ClearObjectList()
                tes3mp.SetObjectListPid(pid)
                tes3mp.SetObjectListCell(self.description)
                objectCount = 0
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectPlace(forEveryone)
        -- The object rotation isn't set correctly via ObjectPlace in clients without a certain hotfix,
        -- so set it separately here
        tes3mp.SendObjectRotate(forEveryone)
    end
end

function BaseCell:LoadObjectsSpawned(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then
            if self:IsDuplicateVanillaActorSpawn(uniqueIndex, objectData[uniqueIndex]) then
                tes3mp.LogAppend(enumerations.log.WARN, "- Purged duplicate actor ObjectSpawn " .. uniqueIndex ..
                    ", refId: " .. objectData[uniqueIndex].refId)
                self:DeleteObjectData(uniqueIndex)
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            else

                local location = objectData[uniqueIndex].location

                -- Ensure data integrity before proceeeding
                if type(location) == "table" and tableHelper.getCount(location) == 6 and
                    tableHelper.usesNumericalValues(location) and
                    self:ContainsPosition(location.posX, location.posY) then

                    local shouldSkip = false
                    local summon = objectData[uniqueIndex].summon

                    if summon ~= nil then
                        local currentTime = os.time()
                        local finishTime = summon.startTime + summon.duration

                        -- Don't spawn this summoned creature if its summoning duration is over..
                        if currentTime >= finishTime then
                            self:DeleteObjectData(uniqueIndex)
                            shouldSkip = true
                        -- ...or if its player is offline
                        elseif summon.summoner.playerName ~= nil or summon.summoner.playerKey ~= nil then
                            local summoner = getLoggedInPlayerByStorageKey(summon.summoner.playerKey)
                            if summoner == nil then
                                summoner = logicHandler.GetLoggedInPlayerByName(summon.summoner.playerName)
                            end

                            if summoner == nil then
                                shouldSkip = true
                            end
                        -- ...or if it doesn't have an actor stored as its summoner
                        elseif summon.summoner.uniqueIndex == nil then
                            shouldSkip = true
                        end
                    end

                    if not shouldSkip then
                        if packetBuilder.AddObjectSpawn(uniqueIndex, objectData[uniqueIndex]) ~= false then
                            objectCount = objectCount + 1
                        end
                    end
                else
                    objectData[uniqueIndex] = nil
                    tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
                end
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectSpawn(forEveryone)
    end
end

function BaseCell:LoadObjectsMoved(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then

            local location = objectData[uniqueIndex].location

            if type(location) == "table" and location.posX ~= nil and location.posY ~= nil and location.posZ ~= nil and
                self:ContainsPosition(location.posX, location.posY) then

                packetBuilder.AddObjectMove(uniqueIndex, objectData[uniqueIndex])
                objectCount = objectCount + 1
            else
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectMove(forEveryone)
    end
end

function BaseCell:LoadObjectsRotated(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then

            local location = objectData[uniqueIndex].location

            if type(location) == "table" and location.rotX ~= nil and location.rotY ~= nil and location.rotZ ~= nil then
                packetBuilder.AddObjectRotate(uniqueIndex, objectData[uniqueIndex])
                objectCount = objectCount + 1
            else
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectRotate(forEveryone)
    end
end

function BaseCell:LoadObjectsLocked(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].lockLevel ~= nil then

            packetBuilder.AddObjectLock(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectLock(forEveryone)
    end
end

function BaseCell:LoadObjectsMiscellaneous(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].goldPool ~= nil then

            local lastGoldRestockHour = objectData[uniqueIndex].lastGoldRestockHour
            local lastGoldRestockDay = objectData[uniqueIndex].lastGoldRestockDay

            if lastGoldRestockHour == nil or lastGoldRestockDay == nil then
                objectData[uniqueIndex].lastGoldRestockHour = 0
                objectData[uniqueIndex].lastGoldRestockDay = 0
            end

            packetBuilder.AddObjectMiscellaneous(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectMiscellaneous(forEveryone)
    end
end

function BaseCell:LoadObjectTrapsTriggered(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then
            packetBuilder.AddObjectTrap(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectTrap(forEveryone)
    end
end

function BaseCell:LoadObjectsScaled(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].scale ~= nil then

            packetBuilder.AddObjectScale(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectScale(forEveryone)
    end
end

function BaseCell:LoadObjectStates(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].state ~= nil then

            packetBuilder.AddObjectState(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectState(forEveryone)
    end
end

function BaseCell:LoadDoorStates(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then
            packetBuilder.AddDoorState(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendDoorState(forEveryone)
    end
end

function BaseCell:LoadDoorDestinations(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].teleportState ~= nil then
            packetBuilder.AddDoorDestination(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendDoorDestination(forEveryone)
    end
end

function BaseCell:LoadClientScriptLocals(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do
        packetBuilder.AddClientScriptLocal(uniqueIndex, objectData[uniqueIndex])
        objectCount = objectCount + 1
    end

    if objectCount > 0 then
        tes3mp.SendClientScriptLocal(forEveryone)
    end
end

function BaseCell:LoadContainerTombstones(pid, objectData, uniqueIndexArray, options)

    local objectCount = 0
    local includePlayerScoped = type(options) == "table" and options.includePlayerScoped == true
    local sendToOtherPlayers = type(options) == "table" and options.sendToOtherPlayers
    local skipAttachedPlayer = type(options) == "table" and options.skipAttachedPlayer

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local currentObjectData = objectData[uniqueIndex]

        if currentObjectData ~= nil and currentObjectData.refId ~= nil and
            currentObjectData.containerTombstones ~= nil and
            not isLiveActorObjectData(currentObjectData) and
            (includePlayerScoped or not self:IsPlayerScopedContainer(uniqueIndex, currentObjectData)) then

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetObjectRefNum(splitIndex[1])
            tes3mp.SetObjectMpNum(splitIndex[2])
            tes3mp.SetObjectRefId(currentObjectData.refId)

            local tombstoneCount = 0

            for tombstoneKey, isRemoved in pairs(currentObjectData.containerTombstones) do
                if isRemoved == true then
                    local item = getContainerItemFromKey(tombstoneKey)

                    if item == nil or not isValidContainerLoadItem(item) then
                        tes3mp.LogAppend(enumerations.log.WARN, "- Skipping invalid saved container tombstone for " ..
                            uniqueIndex)
                    else
                        tes3mp.SetContainerItemRefId(item.refId)
                        tes3mp.SetContainerItemCount(item.count)
                        tes3mp.SetContainerItemActionCount(item.actionCount)
                        tes3mp.SetContainerItemCharge(item.charge)
                        tes3mp.SetContainerItemEnchantmentCharge(item.enchantmentCharge)
                        tes3mp.SetContainerItemSoul(item.soul)

                        tes3mp.AddContainerItem()
                        tombstoneCount = tombstoneCount + 1
                    end
                end
            end

            if tombstoneCount > 0 then
                tes3mp.AddObject()
                objectCount = objectCount + 1
            end
        elseif currentObjectData ~= nil and currentObjectData.containerTombstones ~= nil and
            isLiveActorObjectData(currentObjectData) then
            tes3mp.LogAppend(enumerations.log.INFO, "- Skipping live actor container tombstones for " ..
                tostring(currentObjectData.refId) .. " " .. tostring(uniqueIndex))
        end
    end

    if objectCount > 0 then
        tes3mp.SetObjectListAction(enumerations.container.REMOVE)
        tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.NONE)

        if sendToOtherPlayers ~= nil or skipAttachedPlayer ~= nil then
            tes3mp.SendContainer(sendToOtherPlayers == true, skipAttachedPlayer == true)
        else
            tes3mp.SendContainer()
        end
    end
end

function BaseCell:LoadContainers(pid, objectData, uniqueIndexArray, options)

    local objectCount = 0
    local includePlayerScoped = type(options) == "table" and options.includePlayerScoped == true
    local sendToOtherPlayers = type(options) == "table" and options.sendToOtherPlayers
    local skipAttachedPlayer = type(options) == "table" and options.skipAttachedPlayer

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetObjectRefNum(splitIndex[1])
        tes3mp.SetObjectMpNum(splitIndex[2])

        local currentObjectData = objectData[uniqueIndex]

        if currentObjectData ~= nil and currentObjectData.refId ~= nil and
            currentObjectData.inventory ~= nil and
            not isLiveActorObjectData(currentObjectData) and
            (includePlayerScoped or not self:IsPlayerScopedContainer(uniqueIndex, currentObjectData)) then
            tes3mp.SetObjectRefId(currentObjectData.refId)

            for itemIndex, item in pairs(currentObjectData.inventory) do

                if not isValidContainerLoadItem(item) then
                    tes3mp.LogAppend(enumerations.log.WARN, "- Skipping invalid saved container item for " ..
                        uniqueIndex)
                else
                    tes3mp.SetContainerItemRefId(item.refId)
                    tes3mp.SetContainerItemCount(item.count)
                    tes3mp.SetContainerItemCharge(item.charge)
                    tes3mp.SetContainerItemEnchantmentCharge(item.enchantmentCharge)
                    tes3mp.SetContainerItemSoul(item.soul)

                    tes3mp.AddContainerItem()
                end
            end

            tes3mp.AddObject()

            objectCount = objectCount + 1
        elseif currentObjectData ~= nil and currentObjectData.inventory ~= nil and
            isLiveActorObjectData(currentObjectData) then
            tes3mp.LogAppend(enumerations.log.INFO, "- Skipping live actor container load for " ..
                tostring(currentObjectData.refId) .. " " .. tostring(uniqueIndex))
        elseif currentObjectData == nil or currentObjectData.inventory == nil then
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had container packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then

        -- Set the action to SET
        tes3mp.SetObjectListAction(0)
        tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.NONE)

        if sendToOtherPlayers ~= nil or skipAttachedPlayer ~= nil then
            tes3mp.SendContainer(sendToOtherPlayers == true, skipAttachedPlayer == true)
        else
            tes3mp.SendContainer()
        end
    end

    self:LoadContainerTombstones(pid, objectData, uniqueIndexArray, options)
end

function BaseCell:LoadPlayerScopedContainers(pid)
    local objectData, uniqueIndexes = self:GetPlayerScopedContainerPackets(pid)

    if not tableHelper.isEmpty(uniqueIndexes) then
        self:LoadContainers(pid, objectData, uniqueIndexes, { includePlayerScoped = true })
    end
end

function BaseCell:LoadObjectsByPacketType(packetType, pid, objectData, uniqueIndexArray, forEveryone)

    if packetType == "ObjectPlace" then
        self:LoadObjectsPlaced(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectSpawn" then
        self:LoadObjectsSpawned(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectMove" then
        self:LoadObjectsMoved(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectRotate" then
        self:LoadObjectsRotated(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectDelete" then
        self:LoadObjectsDeleted(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectLock" then
        self:LoadObjectsLocked(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectMiscellaneous" then
        self:LoadObjectsMiscellaneous(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectTrap" then
        self:LoadObjectTrapsTriggered(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectScale" then
        self:LoadObjectsScaled(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectState" then
        self:LoadObjectStates(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "DoorDestination" then
        self:LoadDoorDestinations(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "DoorState" then
        self:LoadDoorStates(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ClientScriptLocal" then
        self:LoadClientScriptLocals(pid, objectData, uniqueIndexArray, forEveryone)
    end
end

function BaseCell:LoadActorList(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) then
            tes3mp.SetActorRefId(objectData[uniqueIndex].refId)

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had actorList packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then

        -- Set the action to SET
        tes3mp.SetActorListAction(0)

        tes3mp.SendActorList()
    end
end

function BaseCell:LoadActorAuthority(pid)
    if config.cppClientActorAuthority == true then
        return
    end

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    tes3mp.SendActorAuthority()
end

function BaseCell:LoadActorPositions(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) then
            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                tes3mp.AddActor()

                actorCount = actorCount + 1
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had position packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorPosition()
    end
end

function BaseCell:LoadActorStatsDynamic(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].stats ~= nil then
            local stats = objectData[uniqueIndex].stats

            tes3mp.SetActorHealthBase(stats.healthBase)
            tes3mp.SetActorHealthCurrent(stats.healthCurrent)
            tes3mp.SetActorHealthModified(stats.healthModified)
            tes3mp.SetActorMagickaBase(stats.magickaBase)
            tes3mp.SetActorMagickaCurrent(stats.magickaCurrent)
            tes3mp.SetActorMagickaModified(stats.magickaModified)
            tes3mp.SetActorFatigueBase(stats.fatigueBase)
            tes3mp.SetActorFatigueCurrent(stats.fatigueCurrent)
            tes3mp.SetActorFatigueModified(stats.fatigueModified)

            tes3mp.AddActor()

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had statsDynamic packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorStatsDynamic()
    end
end

function BaseCell:LoadActorEquipment(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].equipment ~= nil then
            local equipment = objectData[uniqueIndex].equipment

            for itemIndex = 0, tes3mp.GetEquipmentSize() - 1 do

                local currentItem = equipment[itemIndex]

                if currentItem ~= nil then
                    if currentItem.enchantmentCharge == nil then
                        currentItem.enchantmentCharge = -1
                    end

                    tes3mp.EquipActorItem(itemIndex, currentItem.refId, currentItem.count,
                        currentItem.charge, currentItem.enchantmentCharge)
                else
                    tes3mp.UnequipActorItem(itemIndex)
                end
            end

            tes3mp.AddActor()

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had equipment packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorEquipment()
    end
end

function BaseCell:LoadActorSpellsActive(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].spellsActive ~= nil then

            packetBuilder.AddActorSpellsActive(uniqueIndex, objectData[uniqueIndex].spellsActive,
                enumerations.spellbook.SET)

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had spellsActive packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorSpellsActiveChanges()
    end
end

function BaseCell:LoadActorDeath(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].deathState ~= nil then
            if objectData[uniqueIndex].stats == nil or objectData[uniqueIndex].stats.healthCurrent < 1 then
                tes3mp.SetActorDeathState(objectData[uniqueIndex].deathState)
                tes3mp.SetActorDeathInstant(true)
                tes3mp.AddActor()

                actorCount = actorCount + 1
            else
                tes3mp.LogAppend(enumerations.log.ERROR, "- Had death packet recorded for " .. uniqueIndex ..
                ", but its health is above 0! Please report this to a developer")
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had death packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorDeath()
    end
end

function BaseCell:LoadActorAI(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    -- These packets only need to be sent to the new visitor, unless the
    -- new visitor is the target of some of them, in which case those
    -- need to be tracked and sent separately to all the cell's visitors
    local sharedPacketUniqueIndexes = {}

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].ai ~= nil then
            local ai = objectData[uniqueIndex].ai
            local targetPid

            if ai.targetPlayer ~= nil or ai.targetPlayerKey ~= nil or ai.targetAccountName ~= nil then
                local targetPlayer = getLoggedInPlayerByStorageKey(ai.targetPlayerKey)
                local targetPlayerName = ai.targetPlayer or ai.targetAccountName
                if targetPlayer == nil and targetPlayerName ~= nil then
                    targetPlayer = logicHandler.GetLoggedInPlayerByName(targetPlayerName)
                end
                if targetPlayer ~= nil then
                    targetPid = targetPlayer.pid
                end
            end

            local isValid = true

            -- Don't allow untargeted packets that require targets
            if targetPid == nil and ai.targetUniqueIndex == nil then
                if ai.action == enumerations.ai.ACTIVATE or ai.action == enumerations.ai.COMBAT or
                    ai.action == enumerations.ai.ESCORT or ai.action == enumerations.ai.FOLLOW then

                    isValid = false
                    tes3mp.LogAppend(enumerations.log.WARN, "- Could not find valid AI target for actor " ..
                        uniqueIndex)
                end
            end

            if isValid then
                -- Is this new visitor the target of one of the actors? If so, we'll
                -- send a separate packet to every cell visitor with just that at
                -- the end
                if pid == targetPid then
                    table.insert(sharedPacketUniqueIndexes, uniqueIndex)
                else
                    packetBuilder.AddAIActor(uniqueIndex, targetPid, ai)

                    actorCount = actorCount + 1
                end
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had AI packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    -- Send the packets meant for just this new visitor
    if actorCount > 0 then
        tes3mp.SendActorAI(false)
    end

    -- Send the packets targeting this visitor that all the visitors
    -- need to have
    if tableHelper.getCount(sharedPacketUniqueIndexes) > 0 then

        tes3mp.ClearActorList()
        tes3mp.SetActorListPid(pid)
        tes3mp.SetActorListCell(self.description)

        for arrayIndex, uniqueIndex in pairs(sharedPacketUniqueIndexes) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetActorRefNum(splitIndex[1])
            tes3mp.SetActorMpNum(splitIndex[2])
            local ai = objectData[uniqueIndex].ai
            packetBuilder.AddAIActor(uniqueIndex, pid, ai)
        end

        tes3mp.SendActorAI(true)
    end
end

function BaseCell:LoadActorCellChanges(pid, objectData)

    local temporaryLoadedCells = {}
    local actorCount = 0

    -- Move actors originally from this cell to other cells
    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(self.data.packets.cellChangeTo) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].cellChangeTo ~= nil then

            local newCellDescription = objectData[uniqueIndex].cellChangeTo

            tes3mp.SetActorCell(newCellDescription)

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetActorRefNum(splitIndex[1])
            tes3mp.SetActorMpNum(splitIndex[2])

            -- If the new cell is not loaded, load it temporarily
            if LoadedCells[newCellDescription] == nil then
                logicHandler.LoadCell(newCellDescription)
                table.insert(temporaryLoadedCells, newCellDescription)
            end

            if LoadedCells[newCellDescription].data.objectData[uniqueIndex] ~= nil then

                local location = LoadedCells[newCellDescription].data.objectData[uniqueIndex].location

                -- Ensure data integrity before proceeeding
                if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                    LoadedCells[newCellDescription]:ContainsPosition(location.posX, location.posY) then

                    tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                    tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                    tes3mp.AddActor()

                    actorCount = actorCount + 1
                end
            else
                tes3mp.LogAppend(enumerations.log.ERROR, "- Tried to move " .. uniqueIndex .. " from " ..
                    self.description .. " to  " .. newCellDescription .. " with no position data!")
                objectData[uniqueIndex] = nil
                tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had cellChangeTo packet recorded for " .. uniqueIndex ..
                ", but no matching cell description! Please report this to a developer")
            tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorCellChange()
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, newCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(newCellDescription)
    end

    -- Make a table of every cell that has sent actors to this cell
    local cellChangesFrom = {}

    for arrayIndex, uniqueIndex in pairs(self.data.packets.cellChangeFrom) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].cellChangeFrom ~= nil then

            local originalCellDescription = objectData[uniqueIndex].cellChangeFrom

            if cellChangesFrom[originalCellDescription] == nil then
                cellChangesFrom[originalCellDescription] = {}
            end

            table.insert(cellChangesFrom[originalCellDescription], uniqueIndex)
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had cellChangeFrom packet recorded for " .. uniqueIndex ..
                ", but no matching cell description! Please report this to a developer")
            tableHelper.removeValue(self.data.packets.cellChangeFrom, uniqueIndex)
        end
    end

    local actorCount = 0

    -- Send a cell change packet for every cell that has sent actors to this cell
    for originalCellDescription, actorArray in pairs(cellChangesFrom) do

        tes3mp.ClearActorList()
        tes3mp.SetActorListPid(pid)
        tes3mp.SetActorListCell(originalCellDescription)

        for arrayIndex, uniqueIndex in pairs(actorArray) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetActorRefNum(splitIndex[1])
            tes3mp.SetActorMpNum(splitIndex[2])

            tes3mp.SetActorCell(self.description)

            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                tes3mp.AddActor()

                actorCount = actorCount + 1
            end
        end

        if actorCount > 0 then
            tes3mp.SendActorCellChange()
        end
    end
end

function BaseCell:RequestContainers(pid, requestUniqueIndexes)

    self.isRequestingContainerData = true
    self.containerRequestPid = pid

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    -- Set the action to REQUEST
    tes3mp.SetObjectListAction(enumerations.container.REQUEST)
    tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.NONE)

    -- If certain uniqueIndexes are specified, iterate through them and
    -- add them as world objects
    --
    -- Otherwise, the client will simply reply with the contents of all
    -- the containers in this cell
    if requestUniqueIndexes ~= nil and type(requestUniqueIndexes) == "table" then
        for arrayIndex, uniqueIndex in pairs(requestUniqueIndexes) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetObjectRefNum(splitIndex[1])
            tes3mp.SetObjectMpNum(splitIndex[2])

            if self.data.objectData[uniqueIndex] ~= nil and self.data.objectData[uniqueIndex].refId ~= nil then
                tes3mp.SetObjectRefId(self.data.objectData[uniqueIndex].refId)
            end
            tes3mp.AddObject()
        end
    end

    tes3mp.SendContainer()
end

function BaseCell:RequestActorList(pid)

    self.isRequestingActorList = true
    self.actorListRequestPid = pid

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    -- Set the action to REQUEST
    tes3mp.SetActorListAction(3)

    tes3mp.SendActorList()
end

function BaseCell:LoadInitialCellData(pid)

    self:EnsurePacketTables()
    self:EnsurePacketValidity()

    if self.data.loadState == nil then
        self.data.loadState = {
            hasFullActorList = false,
            hasFullContainerData = false
        }
    end

    tes3mp.LogMessage(enumerations.log.INFO, "Loading data of cell " .. self.description .. " for " ..
        logicHandler.GetChatName(pid))

    local objectData = self.data.objectData
    local packets = self.data.packets

    self:LoadObjectsDeleted(pid, objectData, packets.delete)
    self:LoadObjectsPlaced(pid, objectData, packets.place)
    self:LoadObjectsSpawned(pid, objectData, packets.spawn)
    self:LoadObjectsMoved(pid, objectData, packets.move)
    self:LoadObjectsRotated(pid, objectData, packets.rotate)
    self:LoadObjectsLocked(pid, objectData, packets.lock)
    self:LoadObjectTrapsTriggered(pid, objectData, packets.trap)
    self:LoadObjectsScaled(pid, objectData, packets.scale)
    self:LoadObjectsMiscellaneous(pid, objectData, packets.miscellaneous)
    self:LoadObjectStates(pid, objectData, packets.state)
    self:LoadDoorDestinations(pid, objectData, packets.doorDestination)
    self:LoadDoorStates(pid, objectData, packets.doorState)
    self:LoadClientScriptLocals(pid, objectData, packets.clientScriptLocal)

    self:LoadContainers(pid, objectData, packets.container)
    self:LoadPlayerScopedContainers(pid)

    self:LoadActorCellChanges(pid, objectData)
    self:LoadActorDeath(pid, objectData, packets.death)
    self:LoadActorEquipment(pid, objectData, packets.equipment)
    self:LoadActorSpellsActive(pid, objectData, packets.spellsActive)
    self:LoadActorAI(pid, objectData, packets.ai)
end

function BaseCell:LoadMomentaryCellData(pid)

    local objectData = self.data.objectData
    local packets = self.data.packets

    self:LoadActorPositions(pid, objectData, packets.position)
    self:LoadActorStatsDynamic(pid, objectData, packets.statsDynamic)
end

function BaseCell:LoadGeneratedRecords(pid)

    if self.data.recordLinks == nil then self.data.recordLinks = {} end

    local recordLinks = self.data.recordLinks

    for storeType, recordList in pairs(recordLinks) do

        local recordStore = RecordStores[storeType]

        if recordStore ~= nil then
            recordStore:LoadGeneratedRecords(pid, recordStore.data.generatedRecords,
                tableHelper.getArrayFromIndexes(recordList))
        end
    end
end

return BaseCell
