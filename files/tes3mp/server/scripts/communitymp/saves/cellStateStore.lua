require("config")
fileHelper = require("fileHelper")
jsonInterface = require("jsonInterface")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")
local worldSaveRegistry = require("communitympWorldSaveRegistry")
local BaseCell = require("cell.base")

local Cell = class("Cell", BaseCell)

local saveRoot = "saves/world/cells"
local legacyRoot = "cell"
local schemaVersion = worldSaveRegistry.schemaVersion

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

local function compactArray(inputTable)
    if type(inputTable) ~= "table" then
        return
    end

    local values = {}

    for index, value in pairs(inputTable) do
        if type(index) == "number" then
            table.insert(values, {
                index = index,
                value = value
            })
            inputTable[index] = nil
        end
    end

    table.sort(values, function(left, right)
        return left.index < right.index
    end)

    for _, entry in ipairs(values) do
        table.insert(inputTable, entry.value)
    end
end

local function getMetadata(cell)
    return {
        cell = cell.description,
        domain = "cell",
        saveSchemaVersion = schemaVersion
    }
end

local function saveCellDocument(cell, quicksave)
    local saveFunction = quicksave and saveCodec.quicksave or saveCodec.save
    local saved = saveFunction(cell.entryPath, "cell", cell.data, getMetadata(cell))

    if saved then
        worldSaveRegistry.upsertCell(cell.description, cell.data, cell.worldSaveEntry)
    end

    return saved
end

local function getLegacyCommunityXmlPath(entryName)
    local entryDirectoryName = getCaseInsensitiveEntry(saveRoot, entryName)

    if entryDirectoryName == nil then
        return nil
    end

    return saveRoot .. "/" .. entryDirectoryName .. "/" .. entryDirectoryName .. ".xml"
end

function Cell:__init(cellDescription)
    BaseCell.__init(self, cellDescription)

    self.worldSaveEntry = worldSaveRegistry.getCellEntry(cellDescription)
    self.entryName = self.worldSaveEntry.safeName

    saveCodec.ensureDirectory(saveRoot)

    self.entryDirectoryName = self.worldSaveEntry.folder
    self.entryDirectory = self.worldSaveEntry.directory
    self.entryFile = self.worldSaveEntry.file
    self.entryPath = self.worldSaveEntry.relativePath
    self.legacyCommunityEntryName = fileHelper.fixFilename(cellDescription)
    self.legacyCommunityXmlPath = getLegacyCommunityXmlPath(self.legacyCommunityEntryName)
    self.legacyEntryFile = getCaseInsensitiveEntry(legacyRoot, self.legacyCommunityEntryName .. ".json")
    self.hasEntry = saveCodec.exists(self.entryPath) or
        (self.legacyCommunityXmlPath ~= nil and saveCodec.exists(self.legacyCommunityXmlPath)) or
        self.legacyEntryFile ~= nil
end

function Cell:CreateEntry()
    self.hasEntry = saveCellDocument(self, false)

    if self.hasEntry then
        tes3mp.LogMessage(enumerations.log.INFO, "Successfully created XML save for cell " .. self.entryName)
    else
        local message = "Failed to create XML save for " .. self.entryName
        tes3mp.SendMessage(self.pid, message, true)
    end
end

function Cell:SaveToDrive()
    if self.hasEntry then
        compactArray(self.data.packets)
        saveCellDocument(self, false)
    end
end

function Cell:QuicksaveToDrive()
    if self.hasEntry then
        compactArray(self.data.packets)
        saveCellDocument(self, true)
    end
end

function Cell:LoadFromDrive()
    self.data = saveCodec.load(self.entryPath)

    if self.data == nil and self.legacyCommunityXmlPath ~= nil then
        self.data = saveCodec.load(self.legacyCommunityXmlPath)

        if self.data ~= nil then
            tableHelper.fixNumericalKeys(self.data)
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy CommunityMP XML cell save for " .. self.description .. " to indexed world saves")
            return
        end
    end

    if self.data == nil and self.legacyEntryFile ~= nil then
        self.data = jsonInterface.load(legacyRoot .. "/" .. self.legacyEntryFile)

        if self.data ~= nil then
            tableHelper.fixNumericalKeys(self.data)
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy JSON cell save for " .. self.description .. " to XML world saves")
            return
        end
    end

    if self.data == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, self.entryPath .. " cannot be read!")
        tes3mp.StopServer(2)
    else
        tableHelper.fixNumericalKeys(self.data)
    end
end

function Cell:Save()
    self:SaveToDrive()
end

function Cell:Load()
    self:LoadFromDrive()
end

return Cell
