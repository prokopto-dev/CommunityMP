local core = require('openmw.core')
local self = require('openmw.self')
local Player = require('openmw.types').Player

local ok, communitymp = pcall(require, 'openmw.communitymp')
if not ok then
    return {}
end

local releaseQuestId = 'a1_1_findspymaster'
local releaseQuestStage = 1
local releaseTopics = {
    'duties',
    'Caius Cosades',
    'South Wall',
    'specific place',
    'someone in particular',
    'services',
    'my trade',
    'little secret',
    'latest rumors',
    'little advice',
}

local pollSeconds = 0.5
local settleSeconds = 1.0
local elapsed = 0
local readySince = nil
local verified = false
local sentJournalRecovery = false
local sentReleaseObservation = false

local function jsonString(value)
    value = tostring(value or '')
    value = value:gsub('\\', '\\\\')
    value = value:gsub('"', '\\"')
    value = value:gsub('\b', '\\b')
    value = value:gsub('\f', '\\f')
    value = value:gsub('\n', '\\n')
    value = value:gsub('\r', '\\r')
    value = value:gsub('\t', '\\t')
    return '"' .. value .. '"'
end

local function jsonNumber(value)
    if type(value) ~= 'number' then
        return 'null'
    end
    return tostring(value)
end

local function jsonBool(value)
    return value and 'true' or 'false'
end

local function getReleaseQuest()
    local okQuests, quests = pcall(Player.quests, self)
    if not okQuests or quests == nil then
        return nil
    end

    local okQuest, quest = pcall(function()
        return quests[releaseQuestId]
    end)

    if not okQuest then
        return nil
    end

    return quest
end

local function getQuestStage(quest)
    local okStage, stage = pcall(function()
        return quest.stage
    end)

    if not okStage or type(stage) ~= 'number' then
        return 0
    end

    return stage
end

local function ensureReleaseJournal()
    local quest = getReleaseQuest()
    local stage = getQuestStage(quest)
    if quest == nil or stage >= releaseQuestStage then
        return true, false, stage
    end

    if sentJournalRecovery then
        return false, true, stage
    end

    sentJournalRecovery = true
    pcall(function()
        quest:addJournalEntry(releaseQuestStage)
    end)
    return false, true, stage
end

local function ensureReleaseTopics()
    for _, topic in ipairs(releaseTopics) do
        pcall(Player.addTopic, self, topic)
    end

    return true
end

local function sendReleaseObservation(journalRecovered, topicsApplied, questStage)
    if sentReleaseObservation then
        return true
    end

    if type(communitymp.sendPlayerEvent) ~= 'function' then
        return false
    end

    local payload = table.concat({
        '{',
        '"schema":', tostring(communitymp.BRIDGE_SCHEMA_VERSION), ',',
        '"kind":"chargen_release",',
        '"time":', jsonNumber(core.getSimulationTime()), ',',
        '"objectId":', jsonString(self.id), ',',
        '"releaseQuestStage":', jsonNumber(questStage), ',',
        '"releaseJournalRecovered":', jsonBool(journalRecovered), ',',
        '"releaseTopicsApplied":', jsonBool(topicsApplied),
        '}',
    })

    sentReleaseObservation = communitymp.sendPlayerEvent('communitymp.player', 'chargen_release', payload)
    return sentReleaseObservation
end

local function canVerifyReleaseState()
    return communitymp.isConnected() and Player.isCharGenFinished(self)
end

local function verifyReleaseState()
    if not canVerifyReleaseState() then
        readySince = nil
        return
    end

    local now = core.getSimulationTime()
    if readySince == nil then
        readySince = now
        return
    end

    if now - readySince < settleSeconds then
        return
    end

    local journalPresent, journalRecovered, questStage = ensureReleaseJournal()
    local topicsApplied = ensureReleaseTopics()
    local releaseObservationSent = sendReleaseObservation(journalRecovered, topicsApplied, questStage)
    verified = (journalPresent or journalRecovered) and releaseObservationSent
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            if verified or dt <= 0 then
                return
            end

            elapsed = elapsed + dt
            if elapsed < pollSeconds then
                return
            end

            elapsed = 0
            verifyReleaseState()
        end,
    },
}
