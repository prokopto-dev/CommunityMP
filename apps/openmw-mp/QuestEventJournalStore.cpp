#include "QuestEventJournalStore.hpp"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

#include <components/files/conversion.hpp>
#include <components/openmw-mp/TimedLog.hpp>

namespace
{
    std::filesystem::path resolveJournalPath()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_QUEST_EVENT_JOURNAL"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath);
        }

        return std::filesystem::current_path() / "server" / "data" / "quest-events.jsonl";
    }

    std::string pathToLogString(const std::filesystem::path& path)
    {
        return Files::pathToUnicodeString(path);
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

    void appendJsonField(std::string& result, std::string_view name, std::string_view value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        appendJsonString(result, value);
    }

    template <class T>
    void appendJsonNumberField(std::string& result, std::string_view name, T value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        result += std::to_string(value);
    }

    void appendJsonBoolField(std::string& result, std::string_view name, bool value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        result += value ? "true" : "false";
    }

    std::uint64_t nowMilliseconds()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    std::string toJsonLine(std::uint64_t sequence, const mwmp::QuestEventJournalRecord& record)
    {
        std::string result;
        result.reserve(900);
        result.push_back('{');
        appendJsonField(result, "schema", "communitymp.quest.event.v1");
        result.push_back(',');
        appendJsonNumberField(result, "sequence", sequence);
        result.push_back(',');
        appendJsonNumberField(result, "timeUnixMs", nowMilliseconds());
        result.push_back(',');
        appendJsonNumberField(result, "revision", record.revision);
        result.push_back(',');
        appendJsonField(result, "sourcePacket", record.sourcePacket);
        result.push_back(',');
        appendJsonField(result, "playerGuid", record.playerGuid);
        result.push_back(',');
        appendJsonField(result, "playerName", record.playerName);
        result.push_back(',');
        appendJsonField(result, "loginName", record.loginName);
        result.push_back(',');
        appendJsonField(result, "legacyQuestId", record.legacyQuestId);
        result.push_back(',');
        appendJsonField(result, "journalType", record.journalType);
        result.push_back(',');
        appendJsonNumberField(result, "journalIndex", record.journalIndex);
        result.push_back(',');
        appendJsonField(result, "actorRefId", record.actorRefId);
        result.push_back(',');
        appendJsonBoolField(result, "finished", record.finished);
        result.push_back(',');
        appendJsonBoolField(result, "hasTimestamp", record.hasTimestamp);
        result.push_back(',');
        appendJsonNumberField(result, "daysPassed", record.daysPassed);
        result.push_back(',');
        appendJsonNumberField(result, "month", record.month);
        result.push_back(',');
        appendJsonNumberField(result, "day", record.day);
        result.push_back(',');
        appendJsonBoolField(result, "knownQuestDefinition", record.knownQuestDefinition);
        result.push_back(',');
        appendJsonField(result, "questDefinitionId", record.questDefinitionId);
        result.push_back(',');
        appendJsonField(result, "questPackageId", record.questPackageId);
        result.push_back(',');
        appendJsonField(result, "questTitle", record.questTitle);
        result.push_back(',');
        appendJsonBoolField(result, "knownQuestStep", record.knownQuestStep);
        result.push_back(',');
        appendJsonField(result, "questStepId", record.questStepId);
        result.push_back(',');
        appendJsonField(result, "questStepStatus", record.questStepStatus);
        result.push_back(',');
        appendJsonField(result, "questStepCompletionPolicy", record.questStepCompletionPolicy);
        result.push_back(',');
        appendJsonField(result, "questStepSourceInfoId", record.questStepSourceInfoId);
        result += "}\n";
        return result;
    }
}

namespace mwmp
{
    QuestEventJournalStore& QuestEventJournalStore::get()
    {
        static QuestEventJournalStore store;
        return store;
    }

    void QuestEventJournalStore::ensureOpened()
    {
        std::lock_guard lock(mMutex);
        static_cast<void>(ensureOpenedLocked());
    }

    bool QuestEventJournalStore::ensureOpenedLocked()
    {
        if (mStats.available && mStream.is_open())
            return true;

        if (mOpenAttempted)
            return false;

        mOpenAttempted = true;
        mStats.attempted = true;
        mStats.backend = "jsonl";
        mStats.path = resolveJournalPath();

        try
        {
            const std::filesystem::path parent = mStats.path.parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);

            mStream.open(mStats.path, std::ios::out | std::ios::app | std::ios::binary);
            if (!mStream.is_open())
                throw std::runtime_error("failed to open event journal");

            mStats.available = true;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "CommunityMP quest event journal opened path=%s", pathToLogString(mStats.path).c_str());
            return true;
        }
        catch (const std::exception& e)
        {
            mStats.available = false;
            mStats.lastError = e.what();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "CommunityMP quest event journal unavailable path=%s error=%s",
                pathToLogString(mStats.path).c_str(), mStats.lastError.c_str());
            return false;
        }
    }

    bool QuestEventJournalStore::append(const QuestEventJournalRecord& record)
    {
        std::lock_guard lock(mMutex);
        if (!ensureOpenedLocked())
        {
            ++mStats.writeFailures;
            return false;
        }

        const std::uint64_t sequence = mStats.eventCount + 1;
        mStream << toJsonLine(sequence, record);
        mStream.flush();

        if (!mStream.good())
        {
            ++mStats.writeFailures;
            mStats.available = false;
            mStats.lastError = "failed to write event journal record";
            return false;
        }

        mStats.eventCount = sequence;
        return true;
    }

    QuestEventJournalStatistics QuestEventJournalStore::statistics() const
    {
        std::lock_guard lock(mMutex);
        return mStats;
    }
}
