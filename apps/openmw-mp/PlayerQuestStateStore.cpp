#include "PlayerQuestStateStore.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "CommunityMpLuaEventSender.hpp"
#include "Player.hpp"
#include "QuestEventJournalStore.hpp"
#include "QuestDatabaseStore.hpp"
#include "QuestRuntimeEvaluator.hpp"

namespace
{
    struct JournalProgress
    {
        std::string questDefinitionId;
        std::string questPackageId;
        std::string questTitle;
        std::string questScopePolicy;
        std::string questSharingPolicy;
        std::string questInstancingPolicy;
        std::string currentQuestStepId;
        std::string currentQuestStepStatus;
        std::string currentQuestStepCompletionPolicy;
        std::string currentQuestStepSourceInfoId;
        std::size_t questStepCount = 0;
        int currentQuestStepIndex = 0;
        int highestIndex = 0;
        std::size_t entryCount = 0;
        bool knownQuestDefinition = false;
        bool knownQuestStep = false;
        bool hasIndex = false;
        bool isFinished = false;
        bool hasFinishedState = false;
    };

    struct PlayerQuestReadState
    {
        std::map<std::string, JournalProgress> journal;
        std::set<std::string> topics;
        std::set<std::string> books;
        std::uint32_t revision = 0;
    };

    std::mutex sQuestStateMutex;
    std::map<mwmp::PacketGuid, PlayerQuestReadState> sQuestReadStateByPlayer;
    constexpr std::size_t maxQuestStateDeltaItems = 32;

    std::string normalizedLookupKey(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    void appendJsonString(std::string& result, std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";

        result.push_back('"');
        for (const unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        result += "\\u00";
                        result.push_back(hex[(c >> 4) & 0x0f]);
                        result.push_back(hex[c & 0x0f]);
                    }
                    else
                        result.push_back(static_cast<char>(c));
            }
        }
        result.push_back('"');
    }

    const char* jsonBool(bool value)
    {
        return value ? "true" : "false";
    }

    const char* journalItemTypeName(int type)
    {
        switch (type)
        {
            case mwmp::JournalItem::ENTRY:
                return "entry";

            case mwmp::JournalItem::INDEX:
                return "index";

            case mwmp::JournalItem::FINISHED:
                return "finished";
        }

        return "unknown";
    }

    void applyQuestDefinition(JournalProgress& progress, const mwmp::QuestDefinitionRecord& definition)
    {
        progress.questDefinitionId = definition.questId;
        progress.questPackageId = definition.packageId;
        progress.questTitle = definition.title;
        progress.questScopePolicy = definition.scopePolicy;
        progress.questSharingPolicy = definition.sharingPolicy;
        progress.questInstancingPolicy = definition.instancingPolicy;
        progress.questStepCount = definition.stepCount;
        progress.knownQuestDefinition = true;
    }

    void applyQuestDefinitionIfKnown(JournalProgress& progress, std::string_view sourceQuestId)
    {
        mwmp::QuestDatabaseStore::get().ensureLoaded();
        if (std::optional<mwmp::QuestDefinitionRecord> definition
            = mwmp::QuestDatabaseStore::get().findQuestBySourceQuestId(sourceQuestId))
        {
            applyQuestDefinition(progress, *definition);
        }
    }

    void applyQuestStep(JournalProgress& progress, const mwmp::QuestStepRecord& step)
    {
        progress.currentQuestStepId = step.stepId;
        progress.currentQuestStepStatus = step.status;
        progress.currentQuestStepCompletionPolicy = step.completionPolicy;
        progress.currentQuestStepSourceInfoId = step.sourceInfoId;
        progress.currentQuestStepIndex = step.index;
        progress.knownQuestStep = true;
    }

    std::optional<mwmp::QuestStepRecord> findQuestStep(std::string_view sourceQuestId, int index);

    mwmp::QuestEventJournalRecord makeQuestEventJournalRecord(
        Player& player, const mwmp::JournalItem& item, std::uint32_t revision)
    {
        mwmp::QuestEventJournalRecord record;
        record.revision = revision;
        record.playerGuid = mwmp::packetGuidToString(player.guid);
        record.playerName = player.npc.mName;
        record.loginName = player.getLoginName();
        record.legacyQuestId = item.quest;
        record.journalType = journalItemTypeName(item.type);
        record.journalIndex = item.index;
        record.actorRefId = item.actorRefId;
        record.finished = item.isFinished;
        record.hasTimestamp = item.hasTimestamp;
        if (item.hasTimestamp)
        {
            record.daysPassed = item.timestamp.daysPassed;
            record.month = item.timestamp.month;
            record.day = item.timestamp.day;
        }

        if (std::optional<mwmp::QuestDefinitionRecord> definition
            = mwmp::QuestDatabaseStore::get().findQuestBySourceQuestId(item.quest))
        {
            record.knownQuestDefinition = true;
            record.questDefinitionId = definition->questId;
            record.questPackageId = definition->packageId;
            record.questTitle = definition->title;
        }

        if (std::optional<mwmp::QuestStepRecord> step = findQuestStep(item.quest, item.index))
        {
            record.knownQuestStep = true;
            record.questStepId = step->stepId;
            record.questStepStatus = step->status;
            record.questStepCompletionPolicy = step->completionPolicy;
            record.questStepSourceInfoId = step->sourceInfoId;
        }

        return record;
    }

    std::optional<mwmp::QuestStepRecord> findQuestStep(std::string_view sourceQuestId, int index)
    {
        mwmp::QuestDatabaseStore::get().ensureLoaded();
        return mwmp::QuestDatabaseStore::get().findQuestStepBySourceQuestIdAndIndex(sourceQuestId, index);
    }

    void appendQuestDefinitionFields(std::string& payload, const JournalProgress& progress)
    {
        payload += ",\"knownQuestDefinition\":";
        payload += jsonBool(progress.knownQuestDefinition);
        payload += ",\"questDefinitionId\":";
        appendJsonString(payload, progress.questDefinitionId);
        payload += ",\"questPackageId\":";
        appendJsonString(payload, progress.questPackageId);
        payload += ",\"questTitle\":";
        appendJsonString(payload, progress.questTitle);
        payload += ",\"questStepCount\":";
        payload += std::to_string(progress.questStepCount);
        payload += ",\"questScopePolicy\":";
        appendJsonString(payload, progress.questScopePolicy);
        payload += ",\"questSharingPolicy\":";
        appendJsonString(payload, progress.questSharingPolicy);
        payload += ",\"questInstancingPolicy\":";
        appendJsonString(payload, progress.questInstancingPolicy);
    }

    void appendQuestStepFields(std::string& payload, const JournalProgress& progress)
    {
        payload += ",\"knownQuestStep\":";
        payload += jsonBool(progress.knownQuestStep);
        payload += ",\"questStepId\":";
        appendJsonString(payload, progress.currentQuestStepId);
        payload += ",\"questStepIndex\":";
        payload += std::to_string(progress.currentQuestStepIndex);
        payload += ",\"questStepStatus\":";
        appendJsonString(payload, progress.currentQuestStepStatus);
        payload += ",\"questStepCompletionPolicy\":";
        appendJsonString(payload, progress.currentQuestStepCompletionPolicy);
        payload += ",\"questStepSourceInfoId\":";
        appendJsonString(payload, progress.currentQuestStepSourceInfoId);
    }

    void appendQuestDefinitionFields(std::string& payload, const mwmp::JournalItem& item)
    {
        JournalProgress resolved;
        applyQuestDefinitionIfKnown(resolved, item.quest);
        appendQuestDefinitionFields(payload, resolved);
    }

    void appendQuestStepFields(std::string& payload, const mwmp::JournalItem& item)
    {
        JournalProgress resolved;
        if (std::optional<mwmp::QuestStepRecord> step = findQuestStep(item.quest, item.index))
            applyQuestStep(resolved, *step);
        appendQuestStepFields(payload, resolved);
    }

    void appendJournalProgressSummary(std::string& payload, const PlayerQuestReadState& state)
    {
        payload.push_back('[');
        std::size_t index = 0;
        for (const auto& [sourceQuestId, progress] : state.journal)
        {
            if (index >= maxQuestStateDeltaItems)
                break;

            if (index != 0)
                payload.push_back(',');

            payload += "{\"quest\":";
            appendJsonString(payload, sourceQuestId);
            payload += ",\"highestIndex\":";
            payload += std::to_string(progress.highestIndex);
            payload += ",\"entryCount\":";
            payload += std::to_string(progress.entryCount);
            payload += ",\"hasIndex\":";
            payload += jsonBool(progress.hasIndex);
            payload += ",\"finished\":";
            payload += jsonBool(progress.isFinished);
            payload += ",\"hasFinishedState\":";
            payload += jsonBool(progress.hasFinishedState);
            appendQuestDefinitionFields(payload, progress);
            appendQuestStepFields(payload, progress);
            payload += "}";
            ++index;
        }
        payload.push_back(']');
    }

    void appendJournalDelta(std::string& payload, const std::vector<mwmp::JournalItem>& changes)
    {
        payload.push_back('[');
        const std::size_t count = std::min(changes.size(), maxQuestStateDeltaItems);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
                payload.push_back(',');

            const mwmp::JournalItem& item = changes[i];
            payload += "{\"quest\":";
            appendJsonString(payload, item.quest);
            payload += ",\"type\":";
            appendJsonString(payload, journalItemTypeName(item.type));
            payload += ",\"index\":";
            payload += std::to_string(item.index);
            payload += ",\"actorRefId\":";
            appendJsonString(payload, item.actorRefId);
            payload += ",\"finished\":";
            payload += jsonBool(item.isFinished);
            appendQuestDefinitionFields(payload, item);
            appendQuestStepFields(payload, item);
            payload += "}";
        }
        payload.push_back(']');
    }

    void appendTopicDelta(std::string& payload, const std::vector<mwmp::Topic>& changes)
    {
        payload.push_back('[');
        const std::size_t count = std::min(changes.size(), maxQuestStateDeltaItems);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
                payload.push_back(',');

            appendJsonString(payload, changes[i].topicId);
        }
        payload.push_back(']');
    }

    void appendBookDelta(std::string& payload, const std::vector<mwmp::Book>& changes)
    {
        payload.push_back('[');
        const std::size_t count = std::min(changes.size(), maxQuestStateDeltaItems);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
                payload.push_back(',');

            appendJsonString(payload, changes[i].bookId);
        }
        payload.push_back(']');
    }

    void sendQuestStateSummary(Player& player, std::string_view sourcePacket, const PlayerQuestReadState& state,
        std::size_t changedCount, bool loadSnapshot)
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED)
            return;

        const mwmp::QuestDatabaseStatistics questDatabase = mwmp::QuestDatabaseStore::get().statistics();
        const mwmp::QuestEventJournalStatistics eventJournal = mwmp::QuestEventJournalStore::get().statistics();
        std::size_t knownQuestDefinitionCount = 0;
        std::size_t knownQuestStepCount = 0;
        for (const auto& [sourceQuestId, progress] : state.journal)
        {
            static_cast<void>(sourceQuestId);
            if (progress.knownQuestDefinition)
                ++knownQuestDefinitionCount;
            if (progress.knownQuestStep)
                ++knownQuestStepCount;
        }

        std::string payload;
        payload.reserve(520 + sourcePacket.size());
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"quest_state\",\"sourcePacket\":";
        appendJsonString(payload, sourcePacket);
        payload += ",\"storageBackend\":";
        appendJsonString(payload, eventJournal.available ? "memory+jsonl" : "memory");
        payload += ",\"eventJournalBackend\":";
        appendJsonString(payload, eventJournal.backend);
        payload += ",\"eventJournalAvailable\":";
        payload += jsonBool(eventJournal.available);
        payload += ",\"eventJournalEventCount\":";
        payload += std::to_string(eventJournal.eventCount);
        payload += ",\"eventJournalWriteFailures\":";
        payload += std::to_string(eventJournal.writeFailures);
        payload += ",\"definitionBackend\":";
        appendJsonString(payload, questDatabase.backend);
        payload += ",\"questDatabaseLoaded\":";
        payload += jsonBool(questDatabase.loaded);
        payload += ",\"revision\":";
        payload += std::to_string(state.revision);
        payload += ",\"loadSnapshot\":";
        payload += jsonBool(loadSnapshot);
        payload += ",\"changedCount\":";
        payload += std::to_string(changedCount);
        payload += ",\"journalQuestCount\":";
        payload += std::to_string(state.journal.size());
        payload += ",\"knownQuestDefinitionCount\":";
        payload += std::to_string(knownQuestDefinitionCount);
        payload += ",\"unknownJournalQuestCount\":";
        payload += std::to_string(state.journal.size() - knownQuestDefinitionCount);
        payload += ",\"knownQuestStepCount\":";
        payload += std::to_string(knownQuestStepCount);
        payload += ",\"knownTopicCount\":";
        payload += std::to_string(state.topics.size());
        payload += ",\"readBookCount\":";
        payload += std::to_string(state.books.size());
        payload += ",\"deltaLimit\":";
        payload += std::to_string(maxQuestStateDeltaItems);
        payload += ",\"deltaTruncated\":";
        payload += jsonBool(changedCount > maxQuestStateDeltaItems);
        if (sourcePacket == "journal")
        {
            payload += ",\"journalDelta\":";
            appendJournalDelta(payload, player.journalChanges);
            payload += ",\"journalProgress\":";
            appendJournalProgressSummary(payload, state);
        }
        else if (sourcePacket == "topic")
        {
            payload += ",\"topicDelta\":";
            appendTopicDelta(payload, player.topicChanges);
        }
        else if (sourcePacket == "book")
        {
            payload += ",\"bookDelta\":";
            appendBookDelta(payload, player.bookChanges);
        }
        payload += "}";

        if (!mwmp::CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "quest_state", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send quest state summary for %s source=%.*s",
                player.npc.mName.c_str(), static_cast<int>(sourcePacket.size()), sourcePacket.data());
        }
    }

    void appendDialogueEvaluationSummary(std::string& payload, Player& player, const std::vector<mwmp::Topic>& changes)
    {
        payload.push_back('[');
        const std::size_t count = std::min(changes.size(), maxQuestStateDeltaItems);
        std::size_t written = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string& topicId = changes[i].topicId;
            if (topicId.empty())
                continue;

            const std::vector<mwmp::DialogueResponseRecord> responses
                = mwmp::QuestDatabaseStore::get().findDialogueResponsesBySourceTopicId(topicId, "Topic");

            std::size_t authoritativelyAccepted = 0;
            std::size_t conditionRejected = 0;
            std::size_t requiresFallback = 0;
            std::size_t totalConditions = 0;
            std::size_t evaluatedConditions = 0;
            std::size_t unsupportedConditions = 0;
            std::size_t legacyEffectCount = 0;
            std::size_t inventoryEffectCount = 0;
            std::size_t actorEffectCount = 0;
            std::size_t unsupportedEffectCommandCount = 0;

            for (const mwmp::DialogueResponseRecord& response : responses)
            {
                const mwmp::QuestConditionEvaluationResult conditions
                    = mwmp::QuestRuntimeEvaluator::get().evaluateConditionsForPlayer(player, response.responseId);
                const mwmp::QuestLegacyEffectAnalysis effects
                    = mwmp::QuestRuntimeEvaluator::get().analyzeLegacyEffects(response.responseId);

                totalConditions += conditions.totalConditions;
                evaluatedConditions += conditions.evaluatedConditions;
                unsupportedConditions += conditions.unsupportedConditions;
                legacyEffectCount += effects.effectCount;
                inventoryEffectCount += effects.inventoryCommands;
                actorEffectCount += effects.actorCommands;
                unsupportedEffectCommandCount += effects.unsupportedCommands;

                if (!conditions.complete)
                    ++requiresFallback;
                else if (conditions.accepted)
                    ++authoritativelyAccepted;
                else
                    ++conditionRejected;
            }

            if (written != 0)
                payload.push_back(',');

            payload += "{\"topic\":";
            appendJsonString(payload, topicId);
            payload += ",\"dialogueType\":\"Topic\"";
            payload += ",\"responseCount\":";
            payload += std::to_string(responses.size());
            payload += ",\"authoritativelyAccepted\":";
            payload += std::to_string(authoritativelyAccepted);
            payload += ",\"conditionRejected\":";
            payload += std::to_string(conditionRejected);
            payload += ",\"requiresFallback\":";
            payload += std::to_string(requiresFallback);
            payload += ",\"totalConditions\":";
            payload += std::to_string(totalConditions);
            payload += ",\"evaluatedConditions\":";
            payload += std::to_string(evaluatedConditions);
            payload += ",\"unsupportedConditions\":";
            payload += std::to_string(unsupportedConditions);
            payload += ",\"legacyEffectCount\":";
            payload += std::to_string(legacyEffectCount);
            payload += ",\"inventoryEffectCommandCount\":";
            payload += std::to_string(inventoryEffectCount);
            payload += ",\"actorEffectCommandCount\":";
            payload += std::to_string(actorEffectCount);
            payload += ",\"unsupportedEffectCommandCount\":";
            payload += std::to_string(unsupportedEffectCommandCount);
            payload += "}";
            ++written;
        }
        payload.push_back(']');
    }

    void sendDialogueEvaluationSummary(Player& player, const std::vector<mwmp::Topic>& changes)
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED || changes.empty())
            return;

        std::string payload;
        payload.reserve(480 + changes.size() * 96);
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"quest_dialogue_evaluation\",\"sourcePacket\":\"topic\"";
        payload += ",\"topicCount\":";
        payload += std::to_string(changes.size());
        payload += ",\"deltaLimit\":";
        payload += std::to_string(maxQuestStateDeltaItems);
        payload += ",\"deltaTruncated\":";
        payload += jsonBool(changes.size() > maxQuestStateDeltaItems);
        payload += ",\"topics\":";
        appendDialogueEvaluationSummary(payload, player, changes);
        payload += "}";

        if (!mwmp::CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "quest_dialogue_evaluation", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send quest dialogue evaluation summary for %s", player.npc.mName.c_str());
        }
    }
}

namespace mwmp
{
    PlayerQuestStateStore& PlayerQuestStateStore::get()
    {
        static PlayerQuestStateStore store;
        return store;
    }

    void PlayerQuestStateStore::applyJournalChanges(Player& player)
    {
        QuestDatabaseStore::get().ensureLoaded();
        QuestEventJournalStore::get().ensureOpened();

        PlayerQuestReadState snapshot;
        std::vector<QuestEventJournalRecord> journalEvents;
        {
            std::lock_guard lock(sQuestStateMutex);
            PlayerQuestReadState& state = sQuestReadStateByPlayer[player.guid];
            if (player.journalChangesAreLoad)
                state.journal.clear();

            const std::uint32_t nextRevision = state.revision + 1;
            for (const JournalItem& item : player.journalChanges)
            {
                if (item.quest.empty())
                    continue;

                journalEvents.push_back(makeQuestEventJournalRecord(player, item, nextRevision));

                JournalProgress& progress = state.journal[item.quest];
                applyQuestDefinitionIfKnown(progress, item.quest);
                if (item.type == JournalItem::ENTRY || item.type == JournalItem::INDEX)
                {
                    const bool advancesCurrentStep = !progress.hasIndex || item.index >= progress.highestIndex;
                    progress.highestIndex = progress.hasIndex
                        ? std::max(progress.highestIndex, item.index)
                        : item.index;
                    progress.hasIndex = true;

                    if (advancesCurrentStep)
                    {
                        if (std::optional<QuestStepRecord> step = findQuestStep(item.quest, item.index))
                            applyQuestStep(progress, *step);
                    }
                }

                if (item.type == JournalItem::ENTRY)
                    ++progress.entryCount;
                else if (item.type == JournalItem::FINISHED)
                {
                    progress.isFinished = item.isFinished;
                    progress.hasFinishedState = true;
                }
            }

            ++state.revision;
            snapshot = state;
        }

        for (const QuestEventJournalRecord& event : journalEvents)
            static_cast<void>(QuestEventJournalStore::get().append(event));

        sendQuestStateSummary(player, "journal", snapshot, player.journalChanges.size(), player.journalChangesAreLoad);
    }

    void PlayerQuestStateStore::applyTopicChanges(Player& player)
    {
        PlayerQuestReadState snapshot;
        {
            std::lock_guard lock(sQuestStateMutex);
            PlayerQuestReadState& state = sQuestReadStateByPlayer[player.guid];
            if (player.topicChangesAreLoad)
                state.topics.clear();

            for (const Topic& topic : player.topicChanges)
            {
                if (!topic.topicId.empty())
                    state.topics.insert(topic.topicId);
            }

            ++state.revision;
            snapshot = state;
        }

        sendQuestStateSummary(player, "topic", snapshot, player.topicChanges.size(), player.topicChangesAreLoad);
        sendDialogueEvaluationSummary(player, player.topicChanges);
    }

    void PlayerQuestStateStore::applyBookChanges(Player& player)
    {
        PlayerQuestReadState snapshot;
        {
            std::lock_guard lock(sQuestStateMutex);
            PlayerQuestReadState& state = sQuestReadStateByPlayer[player.guid];
            if (player.bookChangesAreLoad)
                state.books.clear();

            for (const Book& book : player.bookChanges)
            {
                if (!book.bookId.empty())
                    state.books.insert(book.bookId);
            }

            ++state.revision;
            snapshot = state;
        }

        sendQuestStateSummary(player, "book", snapshot, player.bookChanges.size(), player.bookChangesAreLoad);
    }

    std::optional<int> PlayerQuestStateStore::getJournalIndex(PacketGuid guid, std::string_view sourceQuestId) const
    {
        std::lock_guard lock(sQuestStateMutex);
        const auto stateIt = sQuestReadStateByPlayer.find(guid);
        if (stateIt == sQuestReadStateByPlayer.end())
            return std::nullopt;

        auto progressIt = stateIt->second.journal.find(std::string(sourceQuestId));
        if (progressIt == stateIt->second.journal.end())
        {
            const std::string normalizedQuestId = normalizedLookupKey(sourceQuestId);
            progressIt = std::find_if(stateIt->second.journal.begin(), stateIt->second.journal.end(),
                [&](const auto& entry) { return normalizedLookupKey(entry.first) == normalizedQuestId; });
        }

        if (progressIt == stateIt->second.journal.end())
            return 0;

        if (!progressIt->second.hasIndex)
            return 0;

        return progressIt->second.highestIndex;
    }

    bool PlayerQuestStateStore::hasTopic(PacketGuid guid, std::string_view topicId) const
    {
        std::lock_guard lock(sQuestStateMutex);
        const auto stateIt = sQuestReadStateByPlayer.find(guid);
        if (stateIt == sQuestReadStateByPlayer.end())
            return false;

        if (stateIt->second.topics.find(std::string(topicId)) != stateIt->second.topics.end())
            return true;

        const std::string normalizedTopicId = normalizedLookupKey(topicId);
        return std::find_if(stateIt->second.topics.begin(), stateIt->second.topics.end(),
            [&](const std::string& topic) { return normalizedLookupKey(topic) == normalizedTopicId; })
            != stateIt->second.topics.end();
    }

    bool PlayerQuestStateStore::hasReadBook(PacketGuid guid, std::string_view bookId) const
    {
        std::lock_guard lock(sQuestStateMutex);
        const auto stateIt = sQuestReadStateByPlayer.find(guid);
        if (stateIt == sQuestReadStateByPlayer.end())
            return false;

        if (stateIt->second.books.find(std::string(bookId)) != stateIt->second.books.end())
            return true;

        const std::string normalizedBookId = normalizedLookupKey(bookId);
        return std::find_if(stateIt->second.books.begin(), stateIt->second.books.end(),
            [&](const std::string& book) { return normalizedLookupKey(book) == normalizedBookId; })
            != stateIt->second.books.end();
    }

    void PlayerQuestStateStore::clearPlayer(PacketGuid guid)
    {
        std::lock_guard lock(sQuestStateMutex);
        sQuestReadStateByPlayer.erase(guid);
    }
}
