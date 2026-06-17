require("config")
fileHelper = require("fileHelper")
jsonInterface = require("jsonInterface")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")
local worldSaveRegistry = require("communitympWorldSaveRegistry")
local BaseRecordStore = require("recordstore.base")

local RecordStore = class("RecordStore", BaseRecordStore)

local saveRoot = "saves/world/recordstores"
local legacyRoot = "recordstore"
local schemaVersion = worldSaveRegistry.schemaVersion

local function legacyJsonExists(relativePath)
    return saveCodec.readFromFile(relativePath) ~= nil
end

local function getMetadata(recordStore)
    return {
        domain = "recordstore",
        saveSchemaVersion = schemaVersion,
        storeType = recordStore.storeType
    }
end

local function relativeToFullPath(relativePath)
    return config.dataPath .. "/" .. relativePath
end

local function getCaseInsensitiveEntry(relativeDirectory, entryName)
    if tes3mp.GetCaseInsensitiveFilename == nil then
        return nil
    end

    local found = tes3mp.GetCaseInsensitiveFilename(relativeToFullPath(relativeDirectory) .. "/", entryName)
    if found == nil or found == "invalid" then
        return nil
    end

    return found
end

local function getLegacyCommunityXmlPath(recordstoreName)
    local entryDirectoryName = getCaseInsensitiveEntry(saveRoot, recordstoreName)

    if entryDirectoryName == nil then
        return nil
    end

    return saveRoot .. "/" .. entryDirectoryName .. "/" .. entryDirectoryName .. ".xml"
end

local function saveRecordStoreDocument(recordStore, quicksave)
    local saveFunction = quicksave and saveCodec.quicksave or saveCodec.save
    local saved = saveFunction(recordStore.recordstorePath, "recordstore", recordStore.data, getMetadata(recordStore))

    if saved then
        worldSaveRegistry.upsertRecordStore(recordStore.storeType, recordStore.data, recordStore.worldSaveEntry)
    end

    return saved
end

function RecordStore:__init(storeType)
    BaseRecordStore.__init(self, storeType)

    self.worldSaveEntry = worldSaveRegistry.getRecordStoreEntry(storeType)
    self.recordstoreName = self.worldSaveEntry.safeName
    self.recordstoreDirectory = self.worldSaveEntry.directory
    self.recordstoreFile = self.worldSaveEntry.file
    self.recordstorePath = self.worldSaveEntry.relativePath
    self.legacyCommunityRecordstoreName = fileHelper.fixFilename(storeType)
    self.legacyCommunityXmlPath = getLegacyCommunityXmlPath(self.legacyCommunityRecordstoreName)
    self.legacyRecordstorePath = legacyRoot .. "/" .. storeType .. ".json"

    saveCodec.ensureDirectory(saveRoot)

    if self.hasEntry == nil then
        self.hasEntry = saveCodec.exists(self.recordstorePath) or
            (self.legacyCommunityXmlPath ~= nil and saveCodec.exists(self.legacyCommunityXmlPath)) or
            legacyJsonExists(self.legacyRecordstorePath)
    end
end

function RecordStore:CreateEntry()
    self.hasEntry = saveRecordStoreDocument(self, false)
end

function RecordStore:SaveToDrive()
    if self.hasEntry then
        saveRecordStoreDocument(self, false)
    end
end

function RecordStore:QuicksaveToDrive()
    if self.hasEntry then
        saveRecordStoreDocument(self, true)
    end
end

function RecordStore:LoadFromDrive()
    self.data = saveCodec.load(self.recordstorePath)

    if self.data == nil and self.legacyCommunityXmlPath ~= nil then
        self.data = saveCodec.load(self.legacyCommunityXmlPath)

        if self.data ~= nil then
            tableHelper.fixNumericalKeys(self.data)
            self:EnsureDataStructure()
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy CommunityMP XML recordstore save for " .. self.storeType ..
                " to indexed world saves")
            return
        end
    end

    if self.data == nil and legacyJsonExists(self.legacyRecordstorePath) then
        self.data = jsonInterface.load(self.legacyRecordstorePath)

        if self.data ~= nil then
            tableHelper.fixNumericalKeys(self.data)
            self:EnsureDataStructure()
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy JSON recordstore save for " .. self.storeType .. " to XML world saves")
            return
        end
    end

    if self.data == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, self.recordstorePath .. " cannot be read!")
        tes3mp.StopServer(2)
    else
        tableHelper.fixNumericalKeys(self.data)
        self:EnsureDataStructure()
    end
end

function RecordStore:Save()
    self:SaveToDrive()
end

function RecordStore:Load()
    self:LoadFromDrive()
end

return RecordStore
