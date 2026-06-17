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
    if quest == nil or getQuestStage(quest) >= releaseQuestStage then
        return true
    end

    if sentJournalRecovery then
        return false
    end

    sentJournalRecovery = true
    pcall(function()
        quest:addJournalEntry(releaseQuestStage)
    end)
    return false
end

local function ensureReleaseTopics()
    for _, topic in ipairs(releaseTopics) do
        pcall(Player.addTopic, self, topic)
    end
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

    local journalPresent = ensureReleaseJournal()
    ensureReleaseTopics()
    verified = journalPresent or sentJournalRecovery
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
