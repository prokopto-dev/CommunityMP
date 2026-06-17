require("config")
jsonInterface = require("jsonInterface")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")
local worldSaveRegistry = require("communitympWorldSaveRegistry")
local BaseWorld = require("world.base")

local World = class("World", BaseWorld)

local globalPaths = worldSaveRegistry.getGlobalPaths()
local legacyCoreVariablesFile = "world/coreVariables.json"
local legacyWorldFile = "world/world.json"
local legacyCommunityCoreVariablesFile = "saves/world/core.xml"
local legacyCommunityWorldFile = "saves/world/world.xml"
local schemaVersion = worldSaveRegistry.schemaVersion

local function legacyJsonExists(relativePath)
    return saveCodec.readFromFile(relativePath) ~= nil
end

local function getMetadata(domain)
    return {
        domain = domain,
        saveSchemaVersion = schemaVersion
    }
end

local function saveWorldDocuments(world, quicksave)
    local saveFunction = quicksave and saveCodec.quicksave or saveCodec.save
    local savedCoreVariables = saveFunction(
        world.coreVariablesPath, "world-core", world.coreVariables, getMetadata("world-core"))
    local savedWorld = saveFunction(world.worldPath, "world", world.data, getMetadata("world"))

    if savedCoreVariables and savedWorld then
        worldSaveRegistry.upsertGlobal(world.coreVariables, world.data)
    end

    return savedCoreVariables and savedWorld
end

local function fixWorldTables(world)
    if world.data ~= nil then
        tableHelper.fixNumericalKeys(world.data)
    end

    if world.coreVariables ~= nil then
        tableHelper.fixNumericalKeys(world.coreVariables)
    end

    world:EnsureCoreVariablesExist()
    world:EnsureTimeDataExists()
end

function World:__init()
    BaseWorld.__init(self)

    self.saveDirectory = globalPaths.root
    self.coreVariablesFile = "core.xml"
    self.worldFile = "global.xml"
    self.coreVariablesPath = globalPaths.coreVariablesPath
    self.worldPath = globalPaths.worldPath
    self.legacyCoreVariablesPath = legacyCoreVariablesFile
    self.legacyWorldPath = legacyWorldFile
    self.legacyCommunityCoreVariablesPath = legacyCommunityCoreVariablesFile
    self.legacyCommunityWorldPath = legacyCommunityWorldFile

    saveCodec.ensureDirectory(globalPaths.stateDirectory)

    if self.hasEntry == nil then
        self.hasEntry = saveCodec.exists(self.worldPath) or
            saveCodec.exists(self.legacyCommunityWorldPath) or legacyJsonExists(self.legacyWorldPath)
    end
end

function World:CreateEntry()
    self.hasEntry = saveWorldDocuments(self, false)
end

function World:SaveToDrive()
    if self.hasEntry then
        saveWorldDocuments(self, false)
    end
end

function World:QuicksaveToDrive()
    if self.hasEntry then
        saveWorldDocuments(self, true)
    end
end

function World:QuicksaveCoreVariablesToDrive()
    if self.hasEntry then
        if saveCodec.quicksave(self.coreVariablesPath, "world-core", self.coreVariables, getMetadata("world-core")) then
            worldSaveRegistry.upsertGlobal(self.coreVariables, self.data)
        end
    end
end

function World:LoadFromDrive()
    local coreVariables = saveCodec.load(self.coreVariablesPath)
    local worldData = saveCodec.load(self.worldPath)

    if worldData == nil and saveCodec.exists(self.legacyCommunityWorldPath) then
        coreVariables = saveCodec.load(self.legacyCommunityCoreVariablesPath) or self.coreVariables
        worldData = saveCodec.load(self.legacyCommunityWorldPath)

        if worldData ~= nil then
            self.coreVariables = coreVariables or self.coreVariables
            self.data = worldData
            fixWorldTables(self)
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy CommunityMP XML world save to indexed world saves")
            return
        end
    end

    if worldData == nil and legacyJsonExists(self.legacyWorldPath) then
        coreVariables = jsonInterface.load(self.legacyCoreVariablesPath) or self.coreVariables
        worldData = jsonInterface.load(self.legacyWorldPath)

        if worldData ~= nil then
            self.coreVariables = coreVariables or self.coreVariables
            self.data = worldData
            fixWorldTables(self)
            self.hasEntry = true
            self:SaveToDrive()
            tes3mp.LogMessage(enumerations.log.INFO, "Migrated legacy JSON world save to XML world saves")
            return
        end
    end

    if worldData == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, self.worldPath .. " cannot be read!")
        tes3mp.StopServer(2)
        return
    end

    self.coreVariables = coreVariables or self.coreVariables
    self.data = worldData
    fixWorldTables(self)
    worldSaveRegistry.upsertGlobal(self.coreVariables, self.data)
end

function World:Save()
    self:SaveToDrive()
end

function World:Load()
    self:LoadFromDrive()
end

return World
