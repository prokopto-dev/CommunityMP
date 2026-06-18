#include "QuestDatabaseStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <components/files/conversion.hpp>
#include <components/openmw-mp/TimedLog.hpp>

namespace
{
    constexpr std::string_view questDatabaseSchema = "communitymp.questdb.v1";

    bool isBlank(std::string_view value)
    {
        for (const char c : value)
        {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                return false;
        }
        return true;
    }

    std::string pathToLogString(const std::filesystem::path& path)
    {
        return Files::pathToUnicodeString(path);
    }

    boost::property_tree::ptree parseJsonLine(
        const std::filesystem::path& path, std::string_view line, std::size_t lineNumber)
    {
        boost::property_tree::ptree tree;
        std::istringstream stream{ std::string(line) };
        try
        {
            boost::property_tree::read_json(stream, tree);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(
                pathToLogString(path) + ":" + std::to_string(lineNumber) + ": invalid JSON: " + e.what());
        }
        return tree;
    }

    boost::property_tree::ptree parseJsonFile(const std::filesystem::path& path)
    {
        boost::property_tree::ptree tree;
        try
        {
            boost::property_tree::read_json(pathToLogString(path), tree);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(pathToLogString(path) + ": invalid JSON: " + e.what());
        }
        return tree;
    }

    bool isQuestDatabaseManifest(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path))
            return false;

        try
        {
            return parseJsonFile(path).get<std::string>("schema", "") == questDatabaseSchema;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool containsQuestDatabaseManifest(const std::filesystem::path& root)
    {
        if (!std::filesystem::exists(root))
            return false;

        const std::filesystem::path directManifest = root / "manifest.json";
        if (isQuestDatabaseManifest(directManifest))
            return true;

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error)
                break;

            if (entry.path().filename() == "manifest.json" && isQuestDatabaseManifest(entry.path()))
                return true;
        }

        return false;
    }

    std::optional<std::filesystem::path> findConfiguredQuestDatabaseRoot()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_QUESTDB_DIR"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath);
        }

        const std::filesystem::path cwd = std::filesystem::current_path();
        const std::vector<std::filesystem::path> candidates = {
            cwd / "questdb",
            cwd / "server" / "questdb",
            cwd / "server" / "data" / "questdb",
            cwd / "data" / "questdb",
            cwd.parent_path() / "questdb",
        };

        for (const std::filesystem::path& candidate : candidates)
        {
            if (containsQuestDatabaseManifest(candidate))
                return candidate;
        }

        return std::nullopt;
    }

    std::vector<std::filesystem::path> findPackageDirectories(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path directManifest = root / "manifest.json";
        if (isQuestDatabaseManifest(directManifest))
        {
            result.push_back(root);
            return result;
        }

        if (!std::filesystem::exists(root))
            return result;

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error)
                break;

            if (entry.path().filename() == "manifest.json" && isQuestDatabaseManifest(entry.path()))
                result.push_back(entry.path().parent_path());
        }

        return result;
    }

    template <class Handler>
    std::size_t readJsonlTable(const std::filesystem::path& packageDir, std::string_view fileName, Handler&& handler)
    {
        const std::filesystem::path path = packageDir / std::string(fileName);
        if (!std::filesystem::exists(path))
            return 0;

        std::ifstream input(path);
        if (!input.is_open())
            throw std::runtime_error("failed to open " + pathToLogString(path));

        std::size_t count = 0;
        std::size_t lineNumber = 0;
        std::string line;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (isBlank(line))
                continue;

            boost::property_tree::ptree row = parseJsonLine(path, line, lineNumber);
            if (row.get<std::string>("schema", "") != questDatabaseSchema)
                throw std::runtime_error(pathToLogString(path) + ":" + std::to_string(lineNumber)
                    + ": unexpected quest database schema");

            handler(row);
            ++count;
        }

        return count;
    }

    bool getBool(const boost::property_tree::ptree& row, std::string_view key, bool fallback = false)
    {
        return row.get<bool>(std::string(key), fallback);
    }

    std::string getString(const boost::property_tree::ptree& row, std::string_view key)
    {
        return row.get<std::string>(std::string(key), "");
    }

    int getInt(const boost::property_tree::ptree& row, std::string_view key, int fallback = 0)
    {
        try
        {
            return row.get<int>(std::string(key), fallback);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    std::string normalizedQuestLookupKey(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    std::pair<std::string, std::string> dialogueLookupKey(
        std::string_view sourceTopicId, std::string_view dialogueType)
    {
        return { normalizedQuestLookupKey(sourceTopicId), normalizedQuestLookupKey(dialogueType) };
    }
}

namespace mwmp
{
    QuestDatabaseStore& QuestDatabaseStore::get()
    {
        static QuestDatabaseStore store;
        return store;
    }

    void QuestDatabaseStore::ensureLoaded()
    {
        std::lock_guard lock(mMutex);
        if (mLoadAttempted)
            return;

        mLoadAttempted = true;
        if (std::optional<std::filesystem::path> root = findConfiguredQuestDatabaseRoot())
            loadFromDirectoryLocked(*root);
        else
        {
            mStats = {};
            mStats.attempted = true;
            mStats.backend = "jsonl";
            mStats.lastError = "No CommunityMP quest database directory found";
        }
    }

    void QuestDatabaseStore::loadFromDirectory(const std::filesystem::path& root)
    {
        std::lock_guard lock(mMutex);
        mLoadAttempted = true;
        loadFromDirectoryLocked(root);
    }

    void QuestDatabaseStore::loadFromDirectoryLocked(const std::filesystem::path& root)
    {
        mStats = {};
        mStats.attempted = true;
        mStats.backend = "jsonl";
        mStats.rootPath = root;
        mQuestDefinitionsById.clear();
        mQuestIdBySourceQuestId.clear();
        mQuestStepsByQuestIdAndIndex.clear();
        mDialogueTopicsById.clear();
        mDialogueTopicIdBySourceTopicAndType.clear();
        mDialogueResponsesByTopicId.clear();
        mConditionsByOwnerId.clear();
        mQuestEffectsByOwnerId.clear();
        mLegacyEffectsByOwnerId.clear();

        try
        {
            const std::vector<std::filesystem::path> packageDirs = findPackageDirectories(root);
            if (packageDirs.empty())
                throw std::runtime_error("No CommunityMP quest database manifests found under " + pathToLogString(root));

            for (const std::filesystem::path& packageDir : packageDirs)
            {
                static_cast<void>(parseJsonFile(packageDir / "manifest.json"));
                ++mStats.manifestCount;

                mStats.packageCount += readJsonlTable(packageDir, "packages.jsonl", [](const auto&) {});

                mStats.questDefinitionCount += readJsonlTable(packageDir, "quest_definitions.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        QuestDefinitionRecord record;
                        record.questId = getString(row, "questId");
                        if (record.questId.empty())
                            return;

                        record.sourceQuestId = getString(row, "sourceQuestId");
                        record.packageId = getString(row, "packageId");
                        record.title = getString(row, "title");
                        record.scopePolicy = getString(row, "scopePolicy");
                        record.sharingPolicy = getString(row, "sharingPolicy");
                        record.repeatPolicy = getString(row, "repeatPolicy");
                        record.instancingPolicy = getString(row, "instancingPolicy");
                        record.deleted = getBool(row, "deleted");

                        if (!record.sourceQuestId.empty())
                        {
                            mQuestIdBySourceQuestId[record.sourceQuestId] = record.questId;
                            mQuestIdBySourceQuestId[normalizedQuestLookupKey(record.sourceQuestId)] = record.questId;
                        }

                        mQuestDefinitionsById[record.questId] = std::move(record);
                    });

                mStats.questStepCount += readJsonlTable(packageDir, "quest_steps.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        QuestStepRecord step;
                        step.stepId = getString(row, "stepId");
                        step.questId = getString(row, "questId");
                        if (step.questId.empty())
                            return;

                        auto quest = mQuestDefinitionsById.find(step.questId);
                        if (quest != mQuestDefinitionsById.end())
                        {
                            ++quest->second.stepCount;
                            step.sourceQuestId = quest->second.sourceQuestId;
                        }

                        step.packageId = getString(row, "packageId");
                        step.sourceInfoId = getString(row, "sourceInfoId");
                        step.status = getString(row, "status");
                        step.text = getString(row, "text");
                        step.completionPolicy = getString(row, "completionPolicy");
                        step.index = getInt(row, "index");
                        step.deleted = getBool(row, "deleted");

                        if (!step.stepId.empty() && !step.deleted)
                            mQuestStepsByQuestIdAndIndex[{ step.questId, step.index }] = std::move(step);
                    });

                mStats.dialogueTopicCount += readJsonlTable(packageDir, "dialogue_topics.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        DialogueTopicRecord topic;
                        topic.topicId = getString(row, "topicId");
                        if (topic.topicId.empty())
                            return;

                        topic.sourceTopicId = getString(row, "sourceTopicId");
                        topic.packageId = getString(row, "packageId");
                        topic.dialogueType = getString(row, "dialogueType");
                        topic.displayName = getString(row, "displayName");
                        topic.visibilityPolicy = getString(row, "visibilityPolicy");
                        topic.deleted = getBool(row, "deleted");

                        if (!topic.deleted)
                        {
                            if (!topic.sourceTopicId.empty())
                                mDialogueTopicIdBySourceTopicAndType[
                                    dialogueLookupKey(topic.sourceTopicId, topic.dialogueType)] = topic.topicId;

                            mDialogueTopicsById[topic.topicId] = std::move(topic);
                        }
                    });

                mStats.dialogueResponseCount += readJsonlTable(packageDir, "dialogue_responses.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        DialogueResponseRecord response;
                        response.responseId = getString(row, "responseId");
                        response.topicId = getString(row, "topicId");
                        if (response.responseId.empty() || response.topicId.empty())
                            return;

                        response.packageId = getString(row, "packageId");
                        response.sourceInfoId = getString(row, "sourceInfoId");
                        response.actor = getString(row, "actor");
                        response.race = getString(row, "race");
                        response.className = getString(row, "class");
                        response.faction = getString(row, "faction");
                        response.cell = getString(row, "cell");
                        response.text = getString(row, "text");
                        response.resultPolicy = getString(row, "resultPolicy");
                        response.order = getInt(row, "order");
                        response.deleted = getBool(row, "deleted");

                        if (!response.deleted)
                            mDialogueResponsesByTopicId[response.topicId].push_back(std::move(response));
                    });

                mStats.conditionCount += readJsonlTable(packageDir, "conditions.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        QuestConditionRecord condition;
                        condition.conditionId = getString(row, "conditionId");
                        condition.ownerKind = getString(row, "ownerKind");
                        condition.ownerId = getString(row, "ownerId");
                        if (condition.conditionId.empty() || condition.ownerId.empty())
                            return;

                        condition.order = getInt(row, "order");
                        condition.functionCode = getInt(row, "functionCode", -1);
                        condition.functionName = getString(row, "function");
                        condition.comparisonCode = getInt(row, "comparisonCode", -1);
                        condition.comparison = getString(row, "comparison");
                        condition.variable = getString(row, "variable");
                        condition.valueType = getString(row, "valueType");
                        condition.value = getString(row, "value");
                        condition.evaluationScope = getString(row, "evaluationScope");
                        condition.stateScope = getString(row, "stateScope");
                        condition.authorityRequirement = getString(row, "authorityRequirement");
                        condition.snapshotPolicy = getString(row, "snapshotPolicy");

                        mConditionsByOwnerId[condition.ownerId].push_back(std::move(condition));
                    });

                mStats.questEffectCount += readJsonlTable(packageDir, "quest_effects.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        QuestEffectRecord effect;
                        effect.effectId = getString(row, "effectId");
                        effect.ownerKind = getString(row, "ownerKind");
                        effect.ownerId = getString(row, "ownerId");
                        if (effect.effectId.empty() || effect.ownerId.empty())
                            return;

                        effect.effectKind = getString(row, "effectKind");
                        effect.executionPolicy = getString(row, "executionPolicy");
                        effect.rawCommand = getString(row, "rawCommand");
                        effect.stateScope = getString(row, "stateScope");
                        effect.transactionKind = getString(row, "transactionKind");
                        effect.authorityRequirement = getString(row, "authorityRequirement");
                        effect.conflictPolicy = getString(row, "conflictPolicy");
                        effect.idempotencyKey = getString(row, "idempotencyKey");
                        effect.target = getString(row, "target");
                        effect.targetKind = getString(row, "targetKind");
                        effect.quest = getString(row, "quest");
                        effect.topic = getString(row, "topic");
                        effect.item = getString(row, "item");
                        effect.combatTarget = getString(row, "combatTarget");
                        effect.order = getInt(row, "order");
                        effect.sourceLine = getInt(row, "sourceLine");
                        effect.index = getInt(row, "index");
                        effect.count = getInt(row, "count");
                        effect.value = getInt(row, "value");
                        effect.choiceCount = getInt(row, "choiceCount");

                        mQuestEffectsByOwnerId[effect.ownerId].push_back(std::move(effect));
                    });

                mStats.legacyEffectCount += readJsonlTable(packageDir, "legacy_effects.jsonl",
                    [&](const boost::property_tree::ptree& row) {
                        LegacyQuestEffectRecord effect;
                        effect.effectId = getString(row, "effectId");
                        effect.ownerKind = getString(row, "ownerKind");
                        effect.ownerId = getString(row, "ownerId");
                        if (effect.effectId.empty() || effect.ownerId.empty())
                            return;

                        effect.effectKind = getString(row, "effectKind");
                        effect.executionPolicy = getString(row, "executionPolicy");
                        effect.script = getString(row, "script");

                        mLegacyEffectsByOwnerId[effect.ownerId].push_back(std::move(effect));
                    });
            }

            for (auto& [topicId, responses] : mDialogueResponsesByTopicId)
            {
                static_cast<void>(topicId);
                std::sort(responses.begin(), responses.end(), [](const DialogueResponseRecord& left,
                                                              const DialogueResponseRecord& right) {
                    if (left.order != right.order)
                        return left.order < right.order;
                    return left.responseId < right.responseId;
                });
            }

            for (auto& [ownerId, conditions] : mConditionsByOwnerId)
            {
                static_cast<void>(ownerId);
                std::sort(conditions.begin(), conditions.end(), [](const QuestConditionRecord& left,
                                                               const QuestConditionRecord& right) {
                    if (left.order != right.order)
                        return left.order < right.order;
                    return left.conditionId < right.conditionId;
                });
            }

            for (auto& [ownerId, effects] : mQuestEffectsByOwnerId)
            {
                static_cast<void>(ownerId);
                std::sort(effects.begin(), effects.end(), [](const QuestEffectRecord& left,
                                                           const QuestEffectRecord& right) {
                    if (left.order != right.order)
                        return left.order < right.order;
                    return left.effectId < right.effectId;
                });
            }

            mStats.loaded = mStats.manifestCount > 0;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Loaded CommunityMP quest database root=%s packages=%zu quests=%zu steps=%zu topics=%zu responses=%zu effects=%zu",
                pathToLogString(root).c_str(), mStats.packageCount, mStats.questDefinitionCount, mStats.questStepCount,
                mStats.dialogueTopicCount, mStats.dialogueResponseCount, mStats.questEffectCount);
        }
        catch (const std::exception& e)
        {
            mStats.loaded = false;
            mStats.lastError = e.what();
            mQuestDefinitionsById.clear();
            mQuestIdBySourceQuestId.clear();
            mQuestStepsByQuestIdAndIndex.clear();
            mDialogueTopicsById.clear();
            mDialogueTopicIdBySourceTopicAndType.clear();
            mDialogueResponsesByTopicId.clear();
            mConditionsByOwnerId.clear();
            mQuestEffectsByOwnerId.clear();
            mLegacyEffectsByOwnerId.clear();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "CommunityMP quest database unavailable root=%s error=%s",
                pathToLogString(root).c_str(), mStats.lastError.c_str());
        }
    }

    QuestDatabaseStatistics QuestDatabaseStore::statistics() const
    {
        std::lock_guard lock(mMutex);
        return mStats;
    }

    std::optional<QuestDefinitionRecord> QuestDatabaseStore::findQuestById(std::string_view questId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mQuestDefinitionsById.find(std::string(questId));
        if (it == mQuestDefinitionsById.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<QuestDefinitionRecord> QuestDatabaseStore::findQuestBySourceQuestId(
        std::string_view sourceQuestId) const
    {
        std::lock_guard lock(mMutex);
        auto sourceIt = mQuestIdBySourceQuestId.find(std::string(sourceQuestId));
        if (sourceIt == mQuestIdBySourceQuestId.end())
            sourceIt = mQuestIdBySourceQuestId.find(normalizedQuestLookupKey(sourceQuestId));
        if (sourceIt == mQuestIdBySourceQuestId.end())
            return std::nullopt;

        const auto questIt = mQuestDefinitionsById.find(sourceIt->second);
        if (questIt == mQuestDefinitionsById.end())
            return std::nullopt;

        return questIt->second;
    }

    std::optional<QuestStepRecord> QuestDatabaseStore::findQuestStepByQuestIdAndIndex(
        std::string_view questId, int index) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mQuestStepsByQuestIdAndIndex.find({ std::string(questId), index });
        if (it == mQuestStepsByQuestIdAndIndex.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<QuestStepRecord> QuestDatabaseStore::findQuestStepBySourceQuestIdAndIndex(
        std::string_view sourceQuestId, int index) const
    {
        std::lock_guard lock(mMutex);
        auto sourceIt = mQuestIdBySourceQuestId.find(std::string(sourceQuestId));
        if (sourceIt == mQuestIdBySourceQuestId.end())
            sourceIt = mQuestIdBySourceQuestId.find(normalizedQuestLookupKey(sourceQuestId));
        if (sourceIt == mQuestIdBySourceQuestId.end())
            return std::nullopt;

        const auto stepIt = mQuestStepsByQuestIdAndIndex.find({ sourceIt->second, index });
        if (stepIt == mQuestStepsByQuestIdAndIndex.end())
            return std::nullopt;

        return stepIt->second;
    }

    std::optional<DialogueTopicRecord> QuestDatabaseStore::findDialogueTopicById(std::string_view topicId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mDialogueTopicsById.find(std::string(topicId));
        if (it == mDialogueTopicsById.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<DialogueTopicRecord> QuestDatabaseStore::findDialogueTopicBySourceTopicId(
        std::string_view sourceTopicId, std::string_view dialogueType) const
    {
        std::lock_guard lock(mMutex);
        const auto sourceIt = mDialogueTopicIdBySourceTopicAndType.find(dialogueLookupKey(sourceTopicId, dialogueType));
        if (sourceIt == mDialogueTopicIdBySourceTopicAndType.end())
            return std::nullopt;

        const auto topicIt = mDialogueTopicsById.find(sourceIt->second);
        if (topicIt == mDialogueTopicsById.end())
            return std::nullopt;

        return topicIt->second;
    }

    std::vector<DialogueResponseRecord> QuestDatabaseStore::findDialogueResponsesByTopicId(
        std::string_view topicId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mDialogueResponsesByTopicId.find(std::string(topicId));
        if (it == mDialogueResponsesByTopicId.end())
            return {};

        return it->second;
    }

    std::vector<DialogueResponseRecord> QuestDatabaseStore::findDialogueResponsesBySourceTopicId(
        std::string_view sourceTopicId, std::string_view dialogueType) const
    {
        std::lock_guard lock(mMutex);
        const auto sourceIt = mDialogueTopicIdBySourceTopicAndType.find(dialogueLookupKey(sourceTopicId, dialogueType));
        if (sourceIt == mDialogueTopicIdBySourceTopicAndType.end())
            return {};

        const auto responsesIt = mDialogueResponsesByTopicId.find(sourceIt->second);
        if (responsesIt == mDialogueResponsesByTopicId.end())
            return {};

        return responsesIt->second;
    }

    std::vector<QuestConditionRecord> QuestDatabaseStore::findConditionsByOwnerId(std::string_view ownerId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mConditionsByOwnerId.find(std::string(ownerId));
        if (it == mConditionsByOwnerId.end())
            return {};

        return it->second;
    }

    std::vector<LegacyQuestEffectRecord> QuestDatabaseStore::findLegacyEffectsByOwnerId(
        std::string_view ownerId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mLegacyEffectsByOwnerId.find(std::string(ownerId));
        if (it == mLegacyEffectsByOwnerId.end())
            return {};

        return it->second;
    }

    std::vector<QuestEffectRecord> QuestDatabaseStore::findQuestEffectsByOwnerId(std::string_view ownerId) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mQuestEffectsByOwnerId.find(std::string(ownerId));
        if (it == mQuestEffectsByOwnerId.end())
            return {};

        return it->second;
    }
}
