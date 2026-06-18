#ifndef OPENMW_MP_QUESTDATABASESTORE_HPP
#define OPENMW_MP_QUESTDATABASESTORE_HPP

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mwmp
{
    struct QuestDatabaseStatistics
    {
        bool attempted = false;
        bool loaded = false;
        std::string backend = "jsonl";
        std::filesystem::path rootPath;
        std::string lastError;
        std::size_t manifestCount = 0;
        std::size_t packageCount = 0;
        std::size_t questDefinitionCount = 0;
        std::size_t questStepCount = 0;
        std::size_t dialogueTopicCount = 0;
        std::size_t dialogueResponseCount = 0;
        std::size_t conditionCount = 0;
        std::size_t legacyEffectCount = 0;
    };

    struct QuestDefinitionRecord
    {
        std::string questId;
        std::string sourceQuestId;
        std::string packageId;
        std::string title;
        std::string scopePolicy;
        std::string sharingPolicy;
        std::string repeatPolicy;
        std::string instancingPolicy;
        std::size_t stepCount = 0;
        bool deleted = false;
    };

    struct QuestStepRecord
    {
        std::string stepId;
        std::string questId;
        std::string sourceQuestId;
        std::string sourceInfoId;
        std::string packageId;
        std::string status;
        std::string text;
        std::string completionPolicy;
        int index = 0;
        bool deleted = false;
    };

    struct DialogueTopicRecord
    {
        std::string topicId;
        std::string sourceTopicId;
        std::string packageId;
        std::string dialogueType;
        std::string displayName;
        std::string visibilityPolicy;
        bool deleted = false;
    };

    struct DialogueResponseRecord
    {
        std::string responseId;
        std::string topicId;
        std::string packageId;
        std::string sourceInfoId;
        std::string actor;
        std::string race;
        std::string className;
        std::string faction;
        std::string cell;
        std::string text;
        std::string resultPolicy;
        int order = 0;
        bool deleted = false;
    };

    struct QuestConditionRecord
    {
        std::string conditionId;
        std::string ownerKind;
        std::string ownerId;
        std::string functionName;
        std::string comparison;
        std::string variable;
        std::string valueType;
        std::string value;
        std::string evaluationScope;
        int functionCode = -1;
        int comparisonCode = -1;
        int order = 0;
    };

    struct LegacyQuestEffectRecord
    {
        std::string effectId;
        std::string ownerKind;
        std::string ownerId;
        std::string effectKind;
        std::string executionPolicy;
        std::string script;
    };

    class QuestDatabaseStore
    {
    public:
        static QuestDatabaseStore& get();

        void ensureLoaded();
        void loadFromDirectory(const std::filesystem::path& root);
        QuestDatabaseStatistics statistics() const;
        std::optional<QuestDefinitionRecord> findQuestById(std::string_view questId) const;
        std::optional<QuestDefinitionRecord> findQuestBySourceQuestId(std::string_view sourceQuestId) const;
        std::optional<QuestStepRecord> findQuestStepByQuestIdAndIndex(std::string_view questId, int index) const;
        std::optional<QuestStepRecord> findQuestStepBySourceQuestIdAndIndex(
            std::string_view sourceQuestId, int index) const;
        std::optional<DialogueTopicRecord> findDialogueTopicById(std::string_view topicId) const;
        std::optional<DialogueTopicRecord> findDialogueTopicBySourceTopicId(
            std::string_view sourceTopicId, std::string_view dialogueType) const;
        std::vector<DialogueResponseRecord> findDialogueResponsesByTopicId(std::string_view topicId) const;
        std::vector<DialogueResponseRecord> findDialogueResponsesBySourceTopicId(
            std::string_view sourceTopicId, std::string_view dialogueType) const;
        std::vector<QuestConditionRecord> findConditionsByOwnerId(std::string_view ownerId) const;
        std::vector<LegacyQuestEffectRecord> findLegacyEffectsByOwnerId(std::string_view ownerId) const;

    private:
        QuestDatabaseStore() = default;

        void loadFromDirectoryLocked(const std::filesystem::path& root);

        mutable std::mutex mMutex;
        bool mLoadAttempted = false;
        QuestDatabaseStatistics mStats;
        std::map<std::string, QuestDefinitionRecord> mQuestDefinitionsById;
        std::map<std::string, std::string> mQuestIdBySourceQuestId;
        std::map<std::pair<std::string, int>, QuestStepRecord> mQuestStepsByQuestIdAndIndex;
        std::map<std::string, DialogueTopicRecord> mDialogueTopicsById;
        std::map<std::pair<std::string, std::string>, std::string> mDialogueTopicIdBySourceTopicAndType;
        std::map<std::string, std::vector<DialogueResponseRecord>> mDialogueResponsesByTopicId;
        std::map<std::string, std::vector<QuestConditionRecord>> mConditionsByOwnerId;
        std::map<std::string, std::vector<LegacyQuestEffectRecord>> mLegacyEffectsByOwnerId;
    };
}

#endif // OPENMW_MP_QUESTDATABASESTORE_HPP
