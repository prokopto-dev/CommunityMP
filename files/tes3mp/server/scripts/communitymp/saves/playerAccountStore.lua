require("config")
fileHelper = require("fileHelper")
jsonInterface = require("jsonInterface")
tableHelper = require("tableHelper")
local saveCodec = require("communitympSaveCodec")
local BasePlayer = require("player.base")

local Player = class("Player", BasePlayer)

local saveRoot = "saves"
local accountFileName = "account.xml"
local characterRoot = "characters"
local schemaVersion = 1
local maxStorageSegmentLength = 96

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

local function findLegacyJsonAccount(accountName)
    local legacyFile = getCaseInsensitiveEntry("player", accountName .. ".json")
    if legacyFile == nil then
        return nil
    end

    return legacyFile
end

local function getSafeCharacterName(characterData, characterIndex)
    local character = type(characterData.character) == "table" and characterData.character or {}
    local name = fileHelper.fixFilename(character.name or "")

    if name == "" then
        name = "character_" .. tostring(characterIndex)
    end

    return name
end

local function limitStorageSegment(segment)
    if string.len(segment) <= maxStorageSegmentLength then
        return segment
    end

    return string.sub(segment, 1, maxStorageSegmentLength)
end

local function getSafeStorageFolder(characterData, characterIndex, storedFolder)
    local fallback = getSafeCharacterName(characterData, characterIndex)
    local folder = nil

    if type(storedFolder) == "string" and storedFolder ~= "" then
        folder = fileHelper.fixFilename(storedFolder)
    end

    if folder == nil or folder == "" then
        folder = fallback
    end

    return limitStorageSegment(folder)
end

local function getSafeStorageFile(storedFile, folder)
    local stem = nil

    if type(storedFile) == "string" and storedFile ~= "" and
        storedFile:find("[/\\]") == nil and storedFile:find("%.%.", 1, true) == nil then
        stem = storedFile:match("^(.*)%.xml$")
        if stem == nil then
            stem = storedFile
        end

        stem = fileHelper.fixFilename(stem)
    end

    if stem == nil or stem == "" then
        stem = folder
    end

    return limitStorageSegment(stem) .. ".xml"
end

local function getSortedCharacterIndexes(entries)
    local indexes = {}

    if type(entries) ~= "table" then
        return indexes
    end

    for characterIndex, characterData in pairs(entries) do
        if type(characterIndex) == "number" and type(characterData) == "table" then
            table.insert(indexes, characterIndex)
        end
    end

    table.sort(indexes)
    return indexes
end

local function makeUniqueFolder(folder, usedFolders)
    local baseFolder = folder
    local suffix = 2

    while usedFolders[string.lower(folder)] do
        local suffixText = "_" .. tostring(suffix)
        local prefixLength = maxStorageSegmentLength - string.len(suffixText)

        if prefixLength < 1 then
            prefixLength = 1
        end

        folder = string.sub(baseFolder, 1, prefixLength) .. suffixText
        suffix = suffix + 1
    end

    usedFolders[string.lower(folder)] = true
    return folder
end

local function getOrCreateCharacterStorage(player, characterData, characterIndex, usedFolders)
    if type(characterData.characterMetadata) ~= "table" then
        characterData.characterMetadata = {}
    end

    local metadata = characterData.characterMetadata
    if type(metadata.storage) ~= "table" then
        metadata.storage = {}
    end

    local storage = metadata.storage
    local folder = makeUniqueFolder(getSafeStorageFolder(characterData, characterIndex, storage.folder), usedFolders)
    local file = getSafeStorageFile(storage.file, folder)

    storage.schemaVersion = schemaVersion
    storage.index = characterIndex
    storage.folder = folder
    storage.file = file
    storage.path = characterRoot .. "/" .. folder .. "/" .. file
    storage.relativePath = player.accountDirectory .. "/" .. storage.path

    return storage
end

local function getStorageSeedFromRosterEntry(rosterEntry)
    local storage = {}

    if type(rosterEntry) == "table" and type(rosterEntry.storage) == "table" then
        storage = tableHelper.deepCopy(rosterEntry.storage)
    end

    if type(storage.path) == "string" and (storage.folder == nil or storage.file == nil) then
        local folder, file = storage.path:match("^" .. characterRoot .. "/([^/]+)/([^/]+)$")
        storage.folder = storage.folder or folder
        storage.file = storage.file or file
    end

    local characterName = ""
    if type(rosterEntry) == "table" and type(rosterEntry.summary) == "table" then
        characterName = rosterEntry.summary.name or ""
    end

    return {
        character = {
            name = characterName
        },
        characterMetadata = {
            storage = storage
        }
    }
end

local function getCharacterSummary(characterData, characterIndex)
    local character = type(characterData.character) == "table" and characterData.character or {}
    local stats = type(characterData.stats) == "table" and characterData.stats or {}
    local location = type(characterData.location) == "table" and characterData.location or {}
    local customClass = type(characterData.customClass) == "table" and characterData.customClass or {}
    local className = character.class

    if className == "custom" and customClass.name ~= nil and customClass.name ~= "" then
        className = customClass.name
    end

    return {
        index = characterIndex,
        name = character.name or ("Character " .. tostring(characterIndex)),
        race = character.race or "",
        class = className or "",
        gender = character.gender or 1,
        head = character.head or "",
        hair = character.hair or "",
        level = stats.level or 1,
        cell = location.cell or location.cellDescription or location.regionName or ""
    }
end

local function updateAccountMetadata(player)
    if type(player.data.accountMetadata) ~= "table" then
        player.data.accountMetadata = {}
    end

    local metadata = player.data.accountMetadata
    metadata.format = "CommunityMP XML Player Save"
    metadata.schemaVersion = schemaVersion
    metadata.accountName = player.accountName
    metadata.saveRoot = saveRoot
    metadata.accountDirectory = player.accountDirectory
    metadata.lastSaved = os.time()

    if metadata.created == nil or metadata.created == 0 then
        metadata.created = metadata.lastSaved
    end
end

local function updateClientMetadata(player)
    if type(player.data.clientMetadata) ~= "table" then
        player.data.clientMetadata = {}
    end

    local metadata = player.data.clientMetadata
    metadata.lastKnownAccountName = player.accountName
    metadata.lastKnownCharacterName = player.data.character ~= nil and player.data.character.name or ""
    metadata.lastKnownPid = player.pid
    metadata.lastSeen = os.time()
    metadata.saveSchemaVersion = schemaVersion
    metadata.packetModel = "server-authoritative"

    if tes3mp.GetIP ~= nil and player.pid ~= nil then
        metadata.lastIpAddress = tes3mp.GetIP(player.pid)
    end

    if tes3mp.GetServerVersion ~= nil then
        metadata.lastServerVersion = tes3mp.GetServerVersion()
    end

    if tes3mp.GetOperatingSystemType ~= nil then
        metadata.lastServerOperatingSystem = tes3mp.GetOperatingSystemType()
    end

    if tes3mp.GetArchitectureType ~= nil then
        metadata.lastServerArchitecture = tes3mp.GetArchitectureType()
    end
end

local function buildAccountDocument(player)
    local accountData = player:GetSharedAccountData()
    local fullEntries = player.data.characters ~= nil and player.data.characters.entries or {}
    local usedFolders = {}

    accountData.characters = {
        selectedIndex = player.data.characters ~= nil and player.data.characters.selectedIndex or player.activeCharacterIndex,
        entries = {}
    }

    for _, characterIndex in ipairs(getSortedCharacterIndexes(fullEntries)) do
        local characterData = fullEntries[characterIndex]
        local storage = getOrCreateCharacterStorage(player, characterData, characterIndex, usedFolders)
        local summary = getCharacterSummary(characterData, characterIndex)

        accountData.characters.entries[characterIndex] = {
            index = characterIndex,
            summary = summary,
            storage = tableHelper.deepCopy(storage),
            lastSaved = os.time()
        }
    end

    return accountData
end

local function writeCharacterDocuments(player)
    local entries = player.data.characters ~= nil and player.data.characters.entries or {}
    local usedFolders = {}
    local savedAll = true

    for _, characterIndex in ipairs(getSortedCharacterIndexes(entries)) do
        local characterData = entries[characterIndex]
        local storage = getOrCreateCharacterStorage(player, characterData, characterIndex, usedFolders)

        if type(characterData.characterMetadata) ~= "table" then
            characterData.characterMetadata = {}
        end

        characterData.characterMetadata.schemaVersion = schemaVersion
        characterData.characterMetadata.lastSaved = os.time()
        characterData.characterMetadata.accountName = player.accountName
        characterData.characterMetadata.characterIndex = characterIndex

        if not saveCodec.save(storage.relativePath, "character", characterData) then
            savedAll = false
        end
    end

    return savedAll
end

local function mergeSharedAccountData(player, accountData)
    local defaultPlayer = BasePlayer(player.pid, player.accountName)
    player.data = defaultPlayer.data

    for key, value in pairs(accountData) do
        if key ~= "characters" then
            player.data[key] = tableHelper.deepCopy(value)
        end
    end

    if type(player.data.characters) ~= "table" then
        player.data.characters = {
            selectedIndex = nil,
            entries = {}
        }
    end

    player.data.characters.selectedIndex = accountData.characters ~= nil and accountData.characters.selectedIndex or nil
    player.data.characters.entries = {}
end

local function loadCharacterDocuments(player, accountData)
    local roster = accountData.characters ~= nil and accountData.characters.entries or {}
    local usedFolders = {}

    for _, characterIndex in ipairs(getSortedCharacterIndexes(roster)) do
        local rosterEntry = roster[characterIndex]
        local storageSeed = getStorageSeedFromRosterEntry(rosterEntry)
        local storage = getOrCreateCharacterStorage(player, storageSeed, characterIndex, usedFolders)
        local rawPath = type(rosterEntry) == "table" and type(rosterEntry.storage) == "table" and
            rosterEntry.storage.path or nil

        if rawPath ~= nil and rawPath ~= storage.path then
            tes3mp.LogMessage(enumerations.log.WARN,
                "Sanitized character XML path for " .. player.accountName .. " from " .. rawPath .. " to " .. storage.path)
        end

        local characterData = saveCodec.load(storage.relativePath)

        if characterData ~= nil then
            tableHelper.fixNumericalKeys(characterData)

            if type(characterData.characterMetadata) ~= "table" then
                characterData.characterMetadata = {}
            end

            characterData.characterMetadata.storage = tableHelper.deepCopy(storage)
            player.data.characters.entries[characterIndex] = characterData
        else
            tes3mp.LogMessage(enumerations.log.WARN,
                "Missing character XML for " .. player.accountName .. " at " .. storage.path)
        end
    end
end

local function writeAccountSnapshot(player, options)
    options = options or {}

    updateAccountMetadata(player)
    updateClientMetadata(player)
    player:SaveActiveCharacterSlot(options.preserveCreatingNewCharacter == true)
    local savedCharacters = writeCharacterDocuments(player)
    local savedAccount = saveCodec.save(player.accountFile, "account", buildAccountDocument(player))
    return savedCharacters and savedAccount
end

function Player:__init(pid, playerName)
    BasePlayer.__init(self, pid, playerName)

    self.accountName = fileHelper.fixFilename(self.accountName or "")

    if self.accountName == "" then
        self.invalidAccountName = true
        self.hasAccount = false
        self.accountDirectory = ""
        self.accountFile = ""
        return
    end

    saveCodec.ensureDirectory(saveRoot)

    local requestedAccountName = self.accountName
    local accountDirectoryName = getCaseInsensitiveEntry(saveRoot, requestedAccountName) or requestedAccountName
    self.accountName = accountDirectoryName
    self.accountDirectoryName = accountDirectoryName
    self.accountDirectory = saveRoot .. "/" .. accountDirectoryName
    self.accountFile = self.accountDirectory .. "/" .. accountFileName
    self.legacyAccountFile = findLegacyJsonAccount(self.accountName) or findLegacyJsonAccount(requestedAccountName)
    self.hasAccount = saveCodec.exists(self.accountFile) or self.legacyAccountFile ~= nil
end

function Player:CreateAccount()
    self:SaveLogin()

    local saved = writeAccountSnapshot(self)
    self.hasAccount = saved

    if self.hasAccount then
        tes3mp.LogMessage(enumerations.log.INFO, "Successfully created XML save account for player " .. self.accountName)
    else
        local message = "Failed to create XML save account for " .. self.accountName
        tes3mp.SendMessage(self.pid, message, true)
        tes3mp.Kick(self.pid)
    end
end

function Player:SaveToDrive()
    if self.hasAccount then
        self:SaveLogin()
        tes3mp.LogMessage(enumerations.log.INFO, "Saving player " .. logicHandler.GetChatName(self.pid))
        writeAccountSnapshot(self)
    end
end

function Player:QuicksaveToDrive()
    if self.hasAccount then
        self:SaveLogin()
        writeAccountSnapshot(self, { preserveCreatingNewCharacter = self.creatingNewCharacter == true })
    end
end

function Player:LoadFromDrive()
    local accountData = saveCodec.load(self.accountFile)

    if accountData == nil and self.legacyAccountFile ~= nil then
        accountData = jsonInterface.load("player/" .. self.legacyAccountFile)

        if accountData ~= nil then
            tableHelper.fixNumericalKeys(accountData)

            if accountData.login.password ~= nil and accountData.login.passwordHash == nil then
                self.data = accountData
                self:ConvertPlaintextPassword()
                accountData = self.data
            end

            self.data = accountData
            self:EnsureCharacterSlots(false)
            self.hasAccount = true
            writeAccountSnapshot(self)
            tes3mp.LogMessage(enumerations.log.INFO,
                "Migrated legacy JSON player save for " .. self.accountName .. " to XML account saves")
            return
        end
    end

    if accountData == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, self.accountFile .. " cannot be read!")
        tes3mp.StopServer(2)
        return
    end

    tableHelper.fixNumericalKeys(accountData)
    mergeSharedAccountData(self, accountData)
    loadCharacterDocuments(self, accountData)
    local entries = self:EnsureCharacterSlots(false)
    local selectedIndex = self.data.characters.selectedIndex

    if selectedIndex == nil and #entries == 1 then
        selectedIndex = 1
    end

    if selectedIndex ~= nil and entries[selectedIndex] ~= nil then
        self:SelectCharacterSlot(selectedIndex)
    end
end

function Player:Save()
    self:SaveToDrive()
end

function Player:Load()
    self:LoadFromDrive()
end

return Player
