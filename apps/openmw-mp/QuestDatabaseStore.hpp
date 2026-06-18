#ifndef OPENMW_MP_QUESTDATABASESTORE_HPP
#define OPENMW_MP_QUESTDATABASESTORE_HPP

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

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

    class QuestDatabaseStore
    {
    public:
        static QuestDatabaseStore& get();

        void ensureLoaded();
        void loadFromDirectory(const std::filesystem::path& root);
        QuestDatabaseStatistics statistics() const;
        std::optional<QuestDefinitionRecord> findQuestById(std::string_view questId) const;
        std::optional<QuestDefinitionRecord> findQuestBySourceQuestId(std::string_view sourceQuestId) const;

    private:
        QuestDatabaseStore() = default;

        void loadFromDirectoryLocked(const std::filesystem::path& root);

        mutable std::mutex mMutex;
        bool mLoadAttempted = false;
        QuestDatabaseStatistics mStats;
        std::map<std::string, QuestDefinitionRecord> mQuestDefinitionsById;
        std::map<std::string, std::string> mQuestIdBySourceQuestId;
    };
}

#endif // OPENMW_MP_QUESTDATABASESTORE_HPP
