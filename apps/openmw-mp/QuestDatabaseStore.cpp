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

    std::string normalizedQuestLookupKey(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
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
                        const std::string questId = getString(row, "questId");
                        if (questId.empty())
                            return;

                        auto quest = mQuestDefinitionsById.find(questId);
                        if (quest != mQuestDefinitionsById.end())
                            ++quest->second.stepCount;
                    });

                mStats.dialogueTopicCount += readJsonlTable(packageDir, "dialogue_topics.jsonl", [](const auto&) {});
                mStats.dialogueResponseCount
                    += readJsonlTable(packageDir, "dialogue_responses.jsonl", [](const auto&) {});
                mStats.conditionCount += readJsonlTable(packageDir, "conditions.jsonl", [](const auto&) {});
                mStats.legacyEffectCount += readJsonlTable(packageDir, "legacy_effects.jsonl", [](const auto&) {});
            }

            mStats.loaded = mStats.manifestCount > 0;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Loaded CommunityMP quest database root=%s packages=%zu quests=%zu steps=%zu topics=%zu responses=%zu",
                pathToLogString(root).c_str(), mStats.packageCount, mStats.questDefinitionCount, mStats.questStepCount,
                mStats.dialogueTopicCount, mStats.dialogueResponseCount);
        }
        catch (const std::exception& e)
        {
            mStats.loaded = false;
            mStats.lastError = e.what();
            mQuestDefinitionsById.clear();
            mQuestIdBySourceQuestId.clear();
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
}
