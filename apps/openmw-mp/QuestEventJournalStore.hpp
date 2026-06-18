#ifndef OPENMW_MP_QUESTEVENTJOURNALSTORE_HPP
#define OPENMW_MP_QUESTEVENTJOURNALSTORE_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace mwmp
{
    struct QuestEventJournalStatistics
    {
        bool attempted = false;
        bool available = false;
        std::string backend = "jsonl";
        std::filesystem::path path;
        std::string lastError;
        std::uint64_t eventCount = 0;
        std::uint64_t writeFailures = 0;
    };

    struct QuestEventJournalRecord
    {
        std::uint32_t revision = 0;
        std::string sourcePacket = "journal";
        std::string playerGuid;
        std::string playerName;
        std::string loginName;
        std::string legacyQuestId;
        std::string journalType;
        int journalIndex = 0;
        std::string actorRefId;
        bool finished = false;
        bool hasTimestamp = false;
        int daysPassed = 0;
        int month = 0;
        int day = 0;
        bool knownQuestDefinition = false;
        std::string questDefinitionId;
        std::string questPackageId;
        std::string questTitle;
        bool knownQuestStep = false;
        std::string questStepId;
        std::string questStepStatus;
        std::string questStepCompletionPolicy;
        std::string questStepSourceInfoId;
    };

    class QuestEventJournalStore
    {
    public:
        static QuestEventJournalStore& get();

        void ensureOpened();
        bool append(const QuestEventJournalRecord& record);
        QuestEventJournalStatistics statistics() const;

    private:
        QuestEventJournalStore() = default;

        bool ensureOpenedLocked();

        mutable std::mutex mMutex;
        QuestEventJournalStatistics mStats;
        bool mOpenAttempted = false;
        std::ofstream mStream;
    };
}

#endif // OPENMW_MP_QUESTEVENTJOURNALSTORE_HPP
