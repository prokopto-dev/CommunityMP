require("config")

jsonInterface = require("jsonInterface")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")

local serverStateRegistry = {}

serverStateRegistry.schemaVersion = 1

local saveRoot = "saves/server"
local securityDirectory = saveRoot .. "/security"
local configDirectory = saveRoot .. "/config"
local manifestPath = saveRoot .. "/manifest.xml"
local banListPath = securityDirectory .. "/banlist.xml"
local dataFileRequirementsPath = configDirectory .. "/data-files.xml"
local legacyBanListPath = "banlist.json"
local legacyDataFileRequirementsPath = "requiredDataFiles.json"

local cachedManifest = nil

local function now()
    return os.time()
end

local function ensureLayout()
    saveCodec.ensureDirectory(saveRoot)
    saveCodec.ensureDirectory(securityDirectory)
    saveCodec.ensureDirectory(configDirectory)
end

local function countArray(inputTable)
    if type(inputTable) ~= "table" then
        return 0
    end

    tableHelper.fixNumericalKeys(inputTable, true)
    return #inputTable
end

local function normalizeArray(inputTable)
    local output = {}

    if type(inputTable) ~= "table" then
        return output
    end

    tableHelper.fixNumericalKeys(inputTable, true)

    local seen = {}
    for _, value in ipairs(inputTable) do
        local entry = tostring(value or "")
        if entry ~= "" and seen[entry] == nil then
            table.insert(output, entry)
            seen[entry] = true
        end
    end

    return output
end

local function normalizeBanList(banList)
    if type(banList) ~= "table" then
        banList = {}
    end

    return {
        playerNames = normalizeArray(banList.playerNames),
        ipAddresses = normalizeArray(banList.ipAddresses)
    }
end

local function normalizeChecksumList(checksums)
    local output = {}

    if type(checksums) ~= "table" then
        return output
    end

    tableHelper.fixNumericalKeys(checksums, true)

    local seen = {}
    for _, checksum in ipairs(checksums) do
        local checksumText = tostring(checksum or "")
        if checksumText ~= "" and seen[checksumText] == nil then
            table.insert(output, checksumText)
            seen[checksumText] = true
        end
    end

    return output
end

local function normalizeDataFileRequirements(requirements)
    local output = {}

    if type(requirements) ~= "table" then
        return output
    end

    tableHelper.fixNumericalKeys(requirements, true)

    for _, pluginEntry in ipairs(requirements) do
        if type(pluginEntry) == "table" then
            local entries = {}
            for pluginName, checksums in pairs(pluginEntry) do
                table.insert(entries, {
                    name = tostring(pluginName or ""),
                    checksums = checksums
                })
            end

            table.sort(entries, function(left, right)
                return left.name < right.name
            end)

            for _, entry in ipairs(entries) do
                local name = entry.name
                if name ~= "" then
                    table.insert(output, {
                        [name] = normalizeChecksumList(entry.checksums)
                    })
                end
            end
        end
    end

    return output
end

local function countDataFileChecksums(requirements)
    local count = 0

    if type(requirements) ~= "table" then
        return count
    end

    tableHelper.fixNumericalKeys(requirements, true)

    for _, pluginEntry in ipairs(requirements) do
        if type(pluginEntry) == "table" then
            for _, checksums in pairs(pluginEntry) do
                count = count + countArray(checksums)
            end
        end
    end

    return count
end

local function createManifest()
    return {
        schemaVersion = serverStateRegistry.schemaVersion,
        createdAt = now(),
        updatedAt = now(),
        layout = {
            root = saveRoot,
            manifestPath = manifestPath,
            securityDirectory = securityDirectory,
            configDirectory = configDirectory
        },
        security = {
            banList = {
                path = banListPath,
                schemaVersion = serverStateRegistry.schemaVersion
            }
        },
        config = {
            dataFileRequirements = {
                path = dataFileRequirementsPath,
                schemaVersion = serverStateRegistry.schemaVersion
            }
        }
    }
end

local function normalizeManifest(manifest)
    if type(manifest) ~= "table" then
        manifest = createManifest()
    end

    manifest.schemaVersion = serverStateRegistry.schemaVersion
    manifest.createdAt = manifest.createdAt or now()
    manifest.updatedAt = manifest.updatedAt or now()

    if type(manifest.layout) ~= "table" then
        manifest.layout = {}
    end

    manifest.layout.root = saveRoot
    manifest.layout.manifestPath = manifestPath
    manifest.layout.securityDirectory = securityDirectory
    manifest.layout.configDirectory = configDirectory

    if type(manifest.security) ~= "table" then
        manifest.security = {}
    end

    if type(manifest.security.banList) ~= "table" then
        manifest.security.banList = {}
    end

    manifest.security.banList.path = banListPath
    manifest.security.banList.schemaVersion = serverStateRegistry.schemaVersion

    if type(manifest.config) ~= "table" then
        manifest.config = {}
    end

    if type(manifest.config.dataFileRequirements) ~= "table" then
        manifest.config.dataFileRequirements = {}
    end

    manifest.config.dataFileRequirements.path = dataFileRequirementsPath
    manifest.config.dataFileRequirements.schemaVersion = serverStateRegistry.schemaVersion

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

    return saveCodec.save(manifestPath, "server-state-manifest", manifest, {
        domain = "server-state-manifest",
        saveSchemaVersion = serverStateRegistry.schemaVersion
    })
end

local function upsertBanListManifest(banList)
    local manifest = loadManifest()
    local entry = manifest.security.banList

    entry.path = banListPath
    entry.schemaVersion = serverStateRegistry.schemaVersion
    entry.lastSaved = now()
    entry.playerNameCount = countArray(banList.playerNames)
    entry.ipAddressCount = countArray(banList.ipAddresses)

    return saveManifest()
end

local function upsertDataFileManifest(requirements)
    local manifest = loadManifest()
    local entry = manifest.config.dataFileRequirements

    entry.path = dataFileRequirementsPath
    entry.schemaVersion = serverStateRegistry.schemaVersion
    entry.lastSaved = now()
    entry.dataFileCount = countArray(requirements)
    entry.checksumCount = countDataFileChecksums(requirements)

    return saveManifest()
end

function serverStateRegistry.getPaths()
    ensureLayout()

    return {
        root = saveRoot,
        manifestPath = manifestPath,
        securityDirectory = securityDirectory,
        configDirectory = configDirectory,
        banListPath = banListPath,
        dataFileRequirementsPath = dataFileRequirementsPath,
        legacyBanListPath = legacyBanListPath,
        legacyDataFileRequirementsPath = legacyDataFileRequirementsPath
    }
end

function serverStateRegistry.getManifest()
    return loadManifest()
end

function serverStateRegistry.saveManifest()
    return saveManifest()
end

function serverStateRegistry.saveBanList(banList)
    local normalizedBanList = normalizeBanList(banList)
    local saved = saveCodec.save(banListPath, "server-banlist", normalizedBanList, {
        domain = "server-security",
        saveSchemaVersion = serverStateRegistry.schemaVersion
    })

    if saved then
        upsertBanListManifest(normalizedBanList)
    end

    return saved
end

function serverStateRegistry.loadBanList()
    local banList = saveCodec.load(banListPath)

    if banList ~= nil then
        banList = normalizeBanList(banList)
        upsertBanListManifest(banList)
        return banList
    end

    banList = jsonInterface.load(legacyBanListPath)
    banList = normalizeBanList(banList)
    serverStateRegistry.saveBanList(banList)

    if countArray(banList.playerNames) > 0 or countArray(banList.ipAddresses) > 0 then
        tes3mp.LogMessage(enumerations.log.INFO, "Migrated legacy JSON banlist to CommunityMP server state")
    end

    return banList
end

function serverStateRegistry.saveDataFileRequirements(requirements)
    local normalizedRequirements = normalizeDataFileRequirements(requirements)
    local saved = saveCodec.save(dataFileRequirementsPath, "server-data-files", normalizedRequirements, {
        domain = "server-config",
        saveSchemaVersion = serverStateRegistry.schemaVersion
    })

    if saved then
        upsertDataFileManifest(normalizedRequirements)
    end

    return saved
end

function serverStateRegistry.loadDataFileRequirements(filename)
    if filename ~= nil and filename ~= "" and filename ~= legacyDataFileRequirementsPath then
        return jsonInterface.load(filename)
    end

    local requirements = saveCodec.load(dataFileRequirementsPath)

    if requirements ~= nil then
        requirements = normalizeDataFileRequirements(requirements)
        upsertDataFileManifest(requirements)
        return requirements
    end

    requirements = jsonInterface.load(legacyDataFileRequirementsPath)
    if requirements == nil then
        return nil
    end

    requirements = normalizeDataFileRequirements(requirements)
    serverStateRegistry.saveDataFileRequirements(requirements)
    tes3mp.LogMessage(enumerations.log.INFO,
        "Migrated legacy JSON data file requirements to CommunityMP server state")

    return requirements
end

return serverStateRegistry
