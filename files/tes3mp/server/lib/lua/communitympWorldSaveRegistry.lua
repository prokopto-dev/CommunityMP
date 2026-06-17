require("config")

fileHelper = require("fileHelper")
local saveCodec = require("communitympSaveCodec")

local worldSaveRegistry = {}

worldSaveRegistry.schemaVersion = 2

local saveRoot = "saves/world"
local stateDirectory = saveRoot .. "/state"
local cellsDirectory = saveRoot .. "/cells"
local recordStoresDirectory = saveRoot .. "/recordstores"
local manifestPath = saveRoot .. "/manifest.xml"
local coreVariablesPath = stateDirectory .. "/core.xml"
local worldStatePath = stateDirectory .. "/global.xml"
local maxStorageNameLength = 72

local cachedManifest = nil

local function now()
    return os.time()
end

local function countEntries(inputTable)
    if type(inputTable) ~= "table" then
        return 0
    end

    local count = 0

    for _ in pairs(inputTable) do
        count = count + 1
    end

    return count
end

local function countNestedEntries(inputTable)
    if type(inputTable) ~= "table" then
        return 0
    end

    local count = 0

    for _, value in pairs(inputTable) do
        count = count + countEntries(value)
    end

    return count
end

local function getStableStorageId(kind, name)
    local seed = tostring(kind or "entry") .. ":" .. tostring(name or "")
    local hash = 5381

    for index = 1, string.len(seed) do
        hash = (hash * 33 + string.byte(seed, index)) % 1000000007
    end

    return tostring(kind or "entry") .. "-" .. string.format("%09d", hash)
end

local function getSafeStorageName(name, fallback)
    local source = tostring(name or "")

    if source == "" then
        source = fallback
    end

    local safeName = fileHelper.fixFilename(source)

    if safeName == "" then
        safeName = fallback
    end

    if string.len(safeName) > maxStorageNameLength then
        safeName = string.sub(safeName, 1, maxStorageNameLength)
    end

    return safeName
end

local function ensureLayout()
    saveCodec.ensureDirectory(saveRoot)
    saveCodec.ensureDirectory(stateDirectory)
    saveCodec.ensureDirectory(cellsDirectory)
    saveCodec.ensureDirectory(recordStoresDirectory)
end

local function createManifest()
    return {
        schemaVersion = worldSaveRegistry.schemaVersion,
        createdAt = now(),
        updatedAt = now(),
        layout = {
            root = saveRoot,
            manifestPath = manifestPath,
            stateDirectory = stateDirectory,
            cellsDirectory = cellsDirectory,
            recordStoresDirectory = recordStoresDirectory
        },
        globals = {
            coreVariables = {
                path = coreVariablesPath,
                schemaVersion = worldSaveRegistry.schemaVersion
            },
            world = {
                path = worldStatePath,
                schemaVersion = worldSaveRegistry.schemaVersion
            }
        },
        cells = {
            entries = {}
        },
        recordStores = {
            entries = {}
        }
    }
end

local function normalizeManifest(manifest)
    if type(manifest) ~= "table" then
        manifest = createManifest()
    end

    manifest.schemaVersion = worldSaveRegistry.schemaVersion
    manifest.createdAt = manifest.createdAt or now()
    manifest.updatedAt = manifest.updatedAt or now()

    if type(manifest.layout) ~= "table" then
        manifest.layout = {}
    end

    manifest.layout.root = saveRoot
    manifest.layout.manifestPath = manifestPath
    manifest.layout.stateDirectory = stateDirectory
    manifest.layout.cellsDirectory = cellsDirectory
    manifest.layout.recordStoresDirectory = recordStoresDirectory

    if type(manifest.globals) ~= "table" then
        manifest.globals = {}
    end

    if type(manifest.globals.coreVariables) ~= "table" then
        manifest.globals.coreVariables = {}
    end

    if type(manifest.globals.world) ~= "table" then
        manifest.globals.world = {}
    end

    manifest.globals.coreVariables.path = coreVariablesPath
    manifest.globals.coreVariables.schemaVersion = worldSaveRegistry.schemaVersion
    manifest.globals.world.path = worldStatePath
    manifest.globals.world.schemaVersion = worldSaveRegistry.schemaVersion

    if type(manifest.cells) ~= "table" then
        manifest.cells = {}
    end

    if type(manifest.cells.entries) ~= "table" then
        manifest.cells.entries = {}
    end

    if type(manifest.recordStores) ~= "table" then
        manifest.recordStores = {}
    end

    if type(manifest.recordStores.entries) ~= "table" then
        manifest.recordStores.entries = {}
    end

    return manifest
end

local function loadManifest()
    if cachedManifest ~= nil then
        return cachedManifest
    end

    ensureLayout()

    cachedManifest = normalizeManifest(saveCodec.load(manifestPath))
    return cachedManifest
end

local function saveManifest()
    local manifest = loadManifest()
    manifest.updatedAt = now()

    return saveCodec.save(manifestPath, "world-manifest", manifest, {
        domain = "world-manifest",
        saveSchemaVersion = worldSaveRegistry.schemaVersion
    })
end

local function buildEntry(kind, name, directory, fileName)
    local storageId = getStableStorageId(kind, name)
    local safeName = getSafeStorageName(name, kind)
    local folder = safeName .. "_" .. storageId

    return {
        kind = kind,
        storageId = storageId,
        safeName = safeName,
        folder = folder,
        directory = directory .. "/" .. folder,
        file = fileName,
        relativePath = directory .. "/" .. folder .. "/" .. fileName,
        schemaVersion = worldSaveRegistry.schemaVersion
    }
end

function worldSaveRegistry.getManifestPath()
    return manifestPath
end

function worldSaveRegistry.getSaveRoot()
    return saveRoot
end

function worldSaveRegistry.getGlobalPaths()
    ensureLayout()

    return {
        root = saveRoot,
        manifestPath = manifestPath,
        stateDirectory = stateDirectory,
        coreVariablesPath = coreVariablesPath,
        worldPath = worldStatePath
    }
end

function worldSaveRegistry.getCellEntry(cellDescription)
    local entry = buildEntry("cell", cellDescription, cellsDirectory, "cell.xml")
    entry.description = tostring(cellDescription or "")
    return entry
end

function worldSaveRegistry.getRecordStoreEntry(storeType)
    local entry = buildEntry("recordstore", storeType, recordStoresDirectory, "records.xml")
    entry.storeType = tostring(storeType or "")
    return entry
end

function worldSaveRegistry.getManifest()
    return loadManifest()
end

function worldSaveRegistry.saveManifest()
    return saveManifest()
end

function worldSaveRegistry.upsertGlobal(coreVariables, worldData)
    local manifest = loadManifest()
    local savedAt = now()

    manifest.globals.coreVariables.path = coreVariablesPath
    manifest.globals.coreVariables.schemaVersion = worldSaveRegistry.schemaVersion
    manifest.globals.coreVariables.lastSaved = savedAt

    if type(coreVariables) == "table" then
        manifest.globals.coreVariables.currentMpNum = coreVariables.currentMpNum or 0
        manifest.globals.coreVariables.hasRunStartupScripts = coreVariables.hasRunStartupScripts == true
    end

    manifest.globals.world.path = worldStatePath
    manifest.globals.world.schemaVersion = worldSaveRegistry.schemaVersion
    manifest.globals.world.lastSaved = savedAt

    if type(worldData) == "table" then
        manifest.globals.world.killCount = countEntries(worldData.kills)
        manifest.globals.world.journalCount = countEntries(worldData.journal)
        manifest.globals.world.topicCount = countEntries(worldData.topics)
        manifest.globals.world.mapExploredCount = countEntries(worldData.mapExplored)
        manifest.globals.world.destinationOverrideCount = countEntries(worldData.destinationOverrides)
        manifest.globals.world.customVariableCount = countEntries(worldData.customVariables)
    end

    return saveManifest()
end

function worldSaveRegistry.upsertCell(cellDescription, cellData, entry)
    local manifest = loadManifest()
    local savedAt = now()
    entry = entry or worldSaveRegistry.getCellEntry(cellDescription)

    local manifestEntry = {
        kind = "cell",
        storageId = entry.storageId,
        description = tostring(cellDescription or ""),
        safeName = entry.safeName,
        folder = entry.folder,
        path = entry.relativePath,
        schemaVersion = worldSaveRegistry.schemaVersion,
        lastSaved = savedAt
    }

    if type(cellData) == "table" then
        manifestEntry.objectCount = countEntries(cellData.objectData)
        manifestEntry.packetReferenceCount = countNestedEntries(cellData.packets)
        manifestEntry.recordLinkCount = countNestedEntries(cellData.recordLinks)

        if type(cellData.loadState) == "table" then
            manifestEntry.hasFullActorList = cellData.loadState.hasFullActorList == true
            manifestEntry.hasFullContainerData = cellData.loadState.hasFullContainerData == true
        end
    end

    manifest.cells.entries[entry.storageId] = manifestEntry
    return saveManifest()
end

function worldSaveRegistry.upsertRecordStore(storeType, recordStoreData, entry)
    local manifest = loadManifest()
    local savedAt = now()
    entry = entry or worldSaveRegistry.getRecordStoreEntry(storeType)

    local manifestEntry = {
        kind = "recordstore",
        storageId = entry.storageId,
        storeType = tostring(storeType or ""),
        safeName = entry.safeName,
        folder = entry.folder,
        path = entry.relativePath,
        schemaVersion = worldSaveRegistry.schemaVersion,
        lastSaved = savedAt
    }

    if type(recordStoreData) == "table" then
        manifestEntry.generatedRecordCount = countEntries(recordStoreData.generatedRecords)
        manifestEntry.permanentRecordCount = countEntries(recordStoreData.permanentRecords)
        manifestEntry.recordLinkCount = countEntries(recordStoreData.recordLinks)
        manifestEntry.unlinkedRecordCount = countEntries(recordStoreData.unlinkedRecordsToCheck)

        if type(recordStoreData.general) == "table" then
            manifestEntry.currentGeneratedNum = recordStoreData.general.currentGeneratedNum or 0
        end
    end

    manifest.recordStores.entries[entry.storageId] = manifestEntry
    return saveManifest()
end

return worldSaveRegistry
