#include "WorldDatabaseStore.hpp"

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
    constexpr std::string_view manifestSchema = "communitymp.worlddb.v1";
    constexpr std::string_view loadOrderRowSchema = "communitymp.worlddb.load-order.v1";
    constexpr std::string_view cellRecordRowSchema = "communitymp.worlddb.cell-record.v1";
    constexpr std::string_view cellReferenceWinnerRowSchema = "communitymp.worlddb.cell-reference-winner.v1";

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
        std::ifstream input(path);
        if (!input.is_open())
            throw std::runtime_error("failed to open " + pathToLogString(path));

        try
        {
            boost::property_tree::read_json(input, tree);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(pathToLogString(path) + ": invalid JSON: " + e.what());
        }
        return tree;
    }

    bool isWorldDatabaseManifest(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path))
            return false;

        try
        {
            return parseJsonFile(path).get<std::string>("schema", "") == manifestSchema;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool containsWorldDatabaseManifest(const std::filesystem::path& root)
    {
        if (!std::filesystem::exists(root))
            return false;

        const std::filesystem::path directManifest = root / "manifest.json";
        if (isWorldDatabaseManifest(directManifest))
            return true;

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error)
                break;

            if (entry.path().filename() == "manifest.json" && isWorldDatabaseManifest(entry.path()))
                return true;
        }

        return false;
    }

    std::optional<std::filesystem::path> findConfiguredWorldDatabaseRoot()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_WORLDDB_DIR"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath);
        }

        std::vector<std::filesystem::path> candidates;
        std::filesystem::path cursor = std::filesystem::current_path();
        for (std::size_t depth = 0; depth < 8 && !cursor.empty(); ++depth)
        {
            candidates.push_back(cursor / "server" / "data" / "worlddb");
            candidates.push_back(cursor / "data" / "worlddb");
            candidates.push_back(cursor / "worlddb");

            const std::filesystem::path parent = cursor.parent_path();
            if (parent.empty() || parent == cursor)
                break;
            cursor = parent;
        }

        for (const std::filesystem::path& candidate : candidates)
        {
            if (containsWorldDatabaseManifest(candidate))
                return candidate;
        }

        return std::nullopt;
    }

    template <class Handler>
    std::size_t readJsonlTable(const std::filesystem::path& root, std::string_view fileName,
        std::string_view expectedSchema, Handler&& handler)
    {
        const std::filesystem::path path = root / std::string(fileName);
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
            if (row.get<std::string>("schema", "") != expectedSchema)
                throw std::runtime_error(pathToLogString(path) + ":" + std::to_string(lineNumber)
                    + ": unexpected world database schema");

            handler(row);
            ++count;
        }

        return count;
    }

    std::string getString(const boost::property_tree::ptree& row, std::string_view key)
    {
        return row.get<std::string>(std::string(key), "");
    }

    bool getBool(const boost::property_tree::ptree& row, std::string_view key, bool fallback = false)
    {
        return row.get<bool>(std::string(key), fallback);
    }

    long long getLongLong(const boost::property_tree::ptree& row, std::string_view key, long long fallback = 0)
    {
        try
        {
            return row.get<long long>(std::string(key), fallback);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    int getInt(const boost::property_tree::ptree& row, std::string_view key, int fallback = 0)
    {
        const long long value = getLongLong(row, key, fallback);
        return static_cast<int>(value);
    }

    std::size_t getSizeT(const boost::property_tree::ptree& row, std::string_view key, std::size_t fallback = 0)
    {
        const long long value = getLongLong(row, key, static_cast<long long>(fallback));
        if (value < 0)
            return fallback;
        return static_cast<std::size_t>(value);
    }

    unsigned int getUnsigned(const boost::property_tree::ptree& row, std::string_view key, unsigned int fallback = 0)
    {
        const long long value = getLongLong(row, key, fallback);
        if (value < 0)
            return fallback;
        return static_cast<unsigned int>(value);
    }

    float getFloat(const boost::property_tree::ptree& row, std::string_view key, float fallback = 0.f)
    {
        try
        {
            return row.get<float>(std::string(key), fallback);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    std::string normalizedLookupKey(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char c : value)
        {
            if (c == '\\')
                result.push_back('/');
            else
                result.push_back(static_cast<char>(std::tolower(c)));
        }
        return result;
    }

    bool referenceSortLess(
        const mwmp::WorldCellReferenceRecord& left, const mwmp::WorldCellReferenceRecord& right)
    {
        if (left.loadOrderIndex != right.loadOrderIndex)
            return left.loadOrderIndex < right.loadOrderIndex;
        if (left.cellRecordIndex != right.cellRecordIndex)
            return left.cellRecordIndex < right.cellRecordIndex;
        if (left.referenceOrder != right.referenceOrder)
            return left.referenceOrder < right.referenceOrder;
        return left.refKey < right.refKey;
    }
}

namespace mwmp
{
    WorldDatabaseStore& WorldDatabaseStore::get()
    {
        static WorldDatabaseStore store;
        return store;
    }

    void WorldDatabaseStore::ensureLoaded()
    {
        std::lock_guard lock(mMutex);
        if (mLoadAttempted)
            return;

        mLoadAttempted = true;
        if (std::optional<std::filesystem::path> root = findConfiguredWorldDatabaseRoot())
            loadFromDirectoryLocked(*root);
        else
        {
            mStats = {};
            mStats.attempted = true;
            mStats.backend = "jsonl-worlddb";
            mStats.lastError = "No CommunityMP world database directory found";
        }
    }

    void WorldDatabaseStore::loadFromDirectory(const std::filesystem::path& root)
    {
        std::lock_guard lock(mMutex);
        mLoadAttempted = true;
        loadFromDirectoryLocked(root);
    }

    void WorldDatabaseStore::loadFromDirectoryLocked(const std::filesystem::path& root)
    {
        mStats = {};
        mStats.attempted = true;
        mStats.backend = "jsonl-worlddb";
        mStats.rootPath = root;
        mLoadOrder.clear();
        mLoadOrderByContentFile.clear();
        mCellsByKey.clear();
        mReferencesByKey.clear();
        mReferenceKeysByEffectiveCellKey.clear();

        try
        {
            const std::filesystem::path manifestPath = root / "manifest.json";
            const boost::property_tree::ptree manifest = parseJsonFile(manifestPath);
            if (manifest.get<std::string>("schema", "") != manifestSchema)
                throw std::runtime_error(pathToLogString(manifestPath) + ": unexpected world database manifest schema");
            ++mStats.manifestCount;

            mStats.loadOrderEntryCount = readJsonlTable(root, "load_order.jsonl", loadOrderRowSchema,
                [&](const boost::property_tree::ptree& row) {
                    WorldLoadOrderEntry entry;
                    entry.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    entry.engineContentIndex = getSizeT(row, "engineContentIndex");
                    entry.contentFile = getString(row, "contentFile");
                    entry.extension = getString(row, "extension");
                    entry.builtin = getBool(row, "builtin");
                    entry.esmLike = getBool(row, "esmLike");
                    entry.dominanceRank = getLongLong(row, "dominanceRank", -1);

                    if (entry.builtin)
                        ++mStats.builtinContentFileCount;
                    if (entry.esmLike)
                        ++mStats.esmLikeContentFileCount;

                    if (!entry.contentFile.empty())
                        mLoadOrderByContentFile[normalizedLookupKey(entry.contentFile)] = entry;

                    mLoadOrder.push_back(std::move(entry));
                });

            mStats.cellRecordCount = readJsonlTable(root, "cells.jsonl", cellRecordRowSchema,
                [&](const boost::property_tree::ptree& row) {
                    WorldCellRecord cell;
                    cell.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    cell.engineContentIndex = getSizeT(row, "engineContentIndex");
                    cell.sourceFile = getString(row, "sourceFile");
                    cell.recordIndex = getSizeT(row, "recordIndex");
                    cell.recordFlags = getUnsigned(row, "recordFlags");
                    cell.deleted = getBool(row, "deleted");
                    cell.recordFlagDeleted = getBool(row, "recordFlagDeleted");
                    cell.deleteSubrecord = getBool(row, "deleteSubrecord");
                    cell.cellKey = getString(row, "cellKey");
                    cell.cellName = getString(row, "cellName");
                    cell.interior = getBool(row, "interior");
                    cell.gridX = getInt(row, "gridX");
                    cell.gridY = getInt(row, "gridY");
                    cell.cellFlags = getInt(row, "cellFlags");
                    cell.region = getString(row, "region");
                    cell.hasWaterHeight = getBool(row, "hasWaterHeight");
                    cell.water = getFloat(row, "water");
                    cell.mapColor = getUnsigned(row, "mapColor");
                    cell.refNumCounter = getInt(row, "refNumCounter");
                    cell.hasAmbient = getBool(row, "hasAmbient");
                    cell.ambientColor = getUnsigned(row, "ambientColor");
                    cell.sunlightColor = getUnsigned(row, "sunlightColor");
                    cell.fogColor = getUnsigned(row, "fogColor");
                    cell.fogDensity = getFloat(row, "fogDensity");

                    if (!cell.cellKey.empty())
                        mCellsByKey[cell.cellKey] = std::move(cell);
                });

            mStats.cellReferenceCount = readJsonlTable(root, "cell_reference_winners.jsonl",
                cellReferenceWinnerRowSchema, [&](const boost::property_tree::ptree& row) {
                    WorldCellReferenceRecord ref;
                    ref.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    ref.engineContentIndex = getSizeT(row, "engineContentIndex");
                    ref.sourceFile = getString(row, "sourceFile");
                    ref.cellRecordIndex = getSizeT(row, "cellRecordIndex");
                    ref.referenceOrder = getSizeT(row, "referenceOrder");
                    ref.refKey = getString(row, "refKey");
                    ref.refNumContentFile = getInt(row, "refNumContentFile", -1);
                    ref.refNumIndex = getUnsigned(row, "refNumIndex");
                    ref.refId = getString(row, "refId");
                    ref.sourceCellKey = getString(row, "sourceCellKey");
                    ref.effectiveCellKey = getString(row, "effectiveCellKey");
                    ref.moved = getBool(row, "moved");
                    ref.deleted = getBool(row, "deleted");
                    ref.tombstone = getBool(row, "tombstone");
                    ref.movedTargetX = getInt(row, "movedTargetX");
                    ref.movedTargetY = getInt(row, "movedTargetY");
                    ref.count = getInt(row, "count", 1);
                    ref.scale = getFloat(row, "scale", 1.f);
                    ref.posX = getFloat(row, "posX");
                    ref.posY = getFloat(row, "posY");
                    ref.posZ = getFloat(row, "posZ");
                    ref.rotX = getFloat(row, "rotX");
                    ref.rotY = getFloat(row, "rotY");
                    ref.rotZ = getFloat(row, "rotZ");
                    ref.owner = getString(row, "owner");
                    ref.globalVariable = getString(row, "globalVariable");
                    ref.soul = getString(row, "soul");
                    ref.faction = getString(row, "faction");
                    ref.factionRank = getInt(row, "factionRank", -1);
                    ref.chargeInt = getInt(row, "chargeInt", -1);
                    ref.chargeIntRemainder = getFloat(row, "chargeIntRemainder");
                    ref.enchantmentCharge = getFloat(row, "enchantmentCharge", -1.f);
                    ref.teleport = getBool(row, "teleport");
                    ref.destCell = getString(row, "destCell");
                    ref.doorDestPosX = getFloat(row, "doorDestPosX");
                    ref.doorDestPosY = getFloat(row, "doorDestPosY");
                    ref.doorDestPosZ = getFloat(row, "doorDestPosZ");
                    ref.doorDestRotX = getFloat(row, "doorDestRotX");
                    ref.doorDestRotY = getFloat(row, "doorDestRotY");
                    ref.doorDestRotZ = getFloat(row, "doorDestRotZ");
                    ref.lockLevel = getInt(row, "lockLevel");
                    ref.locked = getBool(row, "locked");
                    ref.key = getString(row, "key");
                    ref.trap = getString(row, "trap");
                    ref.referenceBlocked = getInt(row, "referenceBlocked", -1);
                    ref.loadOrderRule = getString(row, "loadOrderRule");

                    if (!ref.refKey.empty())
                        mReferencesByKey[ref.refKey] = std::move(ref);
                });

            for (const auto& [cellKey, cell] : mCellsByKey)
            {
                static_cast<void>(cellKey);
                if (!cell.deleted)
                    ++mStats.activeCellRecordCount;
            }

            rebuildReferenceIndexesLocked();
            mStats.loaded = mStats.manifestCount > 0;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Loaded CommunityMP world database root=%s loadOrder=%zu cells=%zu activeCells=%zu refs=%zu activeRefs=%zu indexedCells=%zu",
                pathToLogString(root).c_str(), mStats.loadOrderEntryCount, mStats.cellRecordCount,
                mStats.activeCellRecordCount, mStats.cellReferenceCount, mStats.activeCellReferenceCount,
                mStats.cellReferenceIndexedCellCount);
        }
        catch (const std::exception& e)
        {
            mStats.loaded = false;
            mStats.lastError = e.what();
            mLoadOrder.clear();
            mLoadOrderByContentFile.clear();
            mCellsByKey.clear();
            mReferencesByKey.clear();
            mReferenceKeysByEffectiveCellKey.clear();

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "CommunityMP world database unavailable root=%s error=%s",
                pathToLogString(root).c_str(), mStats.lastError.c_str());
        }
    }

    void WorldDatabaseStore::rebuildReferenceIndexesLocked()
    {
        mReferenceKeysByEffectiveCellKey.clear();
        mStats.activeCellReferenceCount = 0;
        mStats.cellReferenceDeletedCount = 0;
        mStats.cellReferenceMovedCount = 0;
        mStats.cellReferenceTeleportCount = 0;
        mStats.cellReferenceIndexedCellCount = 0;

        for (const auto& [refKey, ref] : mReferencesByKey)
        {
            if (ref.deleted || ref.tombstone)
                ++mStats.cellReferenceDeletedCount;
            else
                ++mStats.activeCellReferenceCount;

            if (ref.moved)
                ++mStats.cellReferenceMovedCount;
            if (ref.teleport)
                ++mStats.cellReferenceTeleportCount;

            if (!ref.effectiveCellKey.empty())
                mReferenceKeysByEffectiveCellKey[ref.effectiveCellKey].push_back(refKey);
        }

        for (auto& [cellKey, refKeys] : mReferenceKeysByEffectiveCellKey)
        {
            static_cast<void>(cellKey);
            std::sort(refKeys.begin(), refKeys.end());
        }

        mStats.cellReferenceIndexedCellCount = mReferenceKeysByEffectiveCellKey.size();
    }

    WorldDatabaseStatistics WorldDatabaseStore::statistics() const
    {
        std::lock_guard lock(mMutex);
        return mStats;
    }

    std::vector<WorldLoadOrderEntry> WorldDatabaseStore::loadOrder() const
    {
        std::lock_guard lock(mMutex);
        return mLoadOrder;
    }

    std::optional<WorldLoadOrderEntry> WorldDatabaseStore::findLoadOrderEntryByContentFile(
        std::string_view contentFile) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mLoadOrderByContentFile.find(normalizedLookupKey(contentFile));
        if (it == mLoadOrderByContentFile.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<WorldCellRecord> WorldDatabaseStore::findCellByKey(std::string_view cellKey) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mCellsByKey.find(std::string(cellKey));
        if (it == mCellsByKey.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<WorldCellReferenceRecord> WorldDatabaseStore::findReferenceByKey(std::string_view refKey) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mReferencesByKey.find(std::string(refKey));
        if (it == mReferencesByKey.end())
            return std::nullopt;

        return it->second;
    }

    std::vector<WorldCellReferenceRecord> WorldDatabaseStore::findReferencesByCellKey(
        std::string_view cellKey, bool includeDeleted) const
    {
        std::lock_guard lock(mMutex);
        const auto indexIt = mReferenceKeysByEffectiveCellKey.find(std::string(cellKey));
        if (indexIt == mReferenceKeysByEffectiveCellKey.end())
            return {};

        std::vector<WorldCellReferenceRecord> result;
        result.reserve(indexIt->second.size());
        for (const std::string& refKey : indexIt->second)
        {
            const auto refIt = mReferencesByKey.find(refKey);
            if (refIt == mReferencesByKey.end())
                continue;

            const WorldCellReferenceRecord& ref = refIt->second;
            if (!includeDeleted && (ref.deleted || ref.tombstone))
                continue;

            result.push_back(ref);
        }

        std::sort(result.begin(), result.end(), referenceSortLess);
        return result;
    }
}
