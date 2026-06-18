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
    constexpr std::string_view recordWinnerRowSchema = "communitymp.worlddb.record-winner.v1";
    constexpr std::string_view actorInventoryRowSchema = "communitymp.worlddb.actor-inventory.v1";
    constexpr std::string_view containerInventoryRowSchema = "communitymp.worlddb.container-inventory.v1";
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

    std::string winnerLookupKey(std::string_view recordType, std::string_view recordKey)
    {
        std::string result(recordType);
        result.push_back('\x1f');
        result += recordKey;
        return result;
    }

    bool isActorRecordType(std::string_view recordType)
    {
        return recordType == "NPC_" || recordType == "CREA" || recordType == "LEVC";
    }

    bool isItemRecordType(std::string_view recordType)
    {
        return recordType == "ALCH" || recordType == "APPA" || recordType == "ARMO" || recordType == "BOOK"
            || recordType == "CLOT" || recordType == "INGR" || recordType == "LIGH" || recordType == "LOCK"
            || recordType == "MISC" || recordType == "PROB" || recordType == "REPA" || recordType == "WEAP";
    }

    std::string recordCategoryForType(std::string_view recordType)
    {
        if (recordType == "NPC_" || recordType == "CREA")
            return "actor";
        if (recordType == "LEVC")
            return "levelledActor";
        if (recordType == "CONT")
            return "container";
        if (recordType == "DOOR")
            return "door";
        if (recordType == "ACTI")
            return "activator";
        if (recordType == "STAT")
            return "static";
        if (recordType == "LEVI")
            return "levelledItem";
        if (isItemRecordType(recordType))
            return "item";
        return "other";
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
        mRecordWinnersByWinnerKey.clear();
        mRecordWinnerKeysByRecordKey.clear();
        mActorInventoryByRecordKey.clear();
        mContainerInventoryByRecordKey.clear();
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

            mStats.recordWinnerCount = readJsonlTable(root, "record_winners.jsonl", recordWinnerRowSchema,
                [&](const boost::property_tree::ptree& row) {
                    WorldRecordWinner winner;
                    winner.recordType = getString(row, "recordType");
                    winner.recordTypeInt = getUnsigned(row, "recordTypeInt");
                    winner.recordKeyAvailable = getBool(row, "recordKeyAvailable");
                    winner.recordKey = normalizedLookupKey(getString(row, "recordKey"));
                    winner.recordId = getString(row, "recordId");
                    winner.recordKeyKind = getString(row, "recordKeyKind");
                    winner.sourceFile = getString(row, "sourceFile");
                    winner.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    winner.engineContentIndex = getSizeT(row, "engineContentIndex");
                    winner.recordIndex = getSizeT(row, "recordIndex");
                    winner.flags = getUnsigned(row, "flags");
                    winner.deleted = getBool(row, "deleted");
                    winner.recordFlagDeleted = getBool(row, "recordFlagDeleted");
                    winner.deleteSubrecord = getBool(row, "deleteSubrecord");
                    winner.tombstone = getBool(row, "tombstone");
                    winner.recordOffset = getSizeT(row, "recordOffset");
                    winner.dataOffset = getSizeT(row, "dataOffset");
                    winner.dataSize = getSizeT(row, "dataSize");
                    winner.loadOrderRule = getString(row, "loadOrderRule");
                    winner.category = recordCategoryForType(winner.recordType);
                    winner.actorAiAvailable = getBool(row, "actorAiAvailable");
                    winner.actorAiPackageCount = getSizeT(row, "actorAiPackageCount");
                    winner.actorAiAction = getUnsigned(row, "actorAiAction");
                    winner.actorAiDistance = getUnsigned(row, "actorAiDistance");
                    winner.actorAiDuration = getUnsigned(row, "actorAiDuration");
                    winner.actorAiShouldRepeat = getBool(row, "actorAiShouldRepeat");
                    winner.actorAiCoordinateX = getFloat(row, "actorAiCoordinateX");
                    winner.actorAiCoordinateY = getFloat(row, "actorAiCoordinateY");
                    winner.actorAiCoordinateZ = getFloat(row, "actorAiCoordinateZ");
                    winner.actorAiTargetId = getString(row, "actorAiTargetId");
                    winner.actorAiCellName = getString(row, "actorAiCellName");
                    winner.actorAiHello = getUnsigned(row, "actorAiHello");
                    winner.actorAiFight = getUnsigned(row, "actorAiFight");
                    winner.actorAiFlee = getUnsigned(row, "actorAiFlee");
                    winner.actorAiAlarm = getUnsigned(row, "actorAiAlarm");
                    winner.actorInventoryImported = getBool(row, "actorInventoryImported");
                    winner.actorInventoryItemCount = getSizeT(row, "actorInventoryItemCount");
                    winner.containerInventoryImported = getBool(row, "containerInventoryImported");
                    winner.containerInventoryItemCount = getSizeT(row, "containerInventoryItemCount");

                    const bool deletedWinner = winner.deleted || winner.tombstone;
                    if (deletedWinner)
                        ++mStats.recordWinnerDeletedCount;
                    if (!deletedWinner && winner.actorInventoryImported && !winner.recordKey.empty())
                        ++mStats.actorInventoryRecordCount;
                    if (!deletedWinner && winner.containerInventoryImported && !winner.recordKey.empty())
                        ++mStats.containerInventoryRecordCount;

                    if (winner.recordType.empty() || winner.recordKey.empty())
                        return;

                    winner.winnerKey = winnerLookupKey(winner.recordType, winner.recordKey);
                    mRecordWinnerKeysByRecordKey[winner.recordKey].push_back(winner.winnerKey);
                    mRecordWinnersByWinnerKey[winner.winnerKey] = std::move(winner);
                });

            mStats.actorInventoryItemCount = readJsonlTable(root, "actor_inventory.jsonl",
                actorInventoryRowSchema, [&](const boost::property_tree::ptree& row) {
                    WorldActorInventoryItem item;
                    item.recordKey = normalizedLookupKey(getString(row, "recordKey"));
                    item.recordId = getString(row, "recordId");
                    item.sourceFile = getString(row, "sourceFile");
                    item.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    item.engineContentIndex = getSizeT(row, "engineContentIndex");
                    item.recordIndex = getSizeT(row, "recordIndex");
                    item.itemOrder = getSizeT(row, "itemOrder");
                    item.itemRefId = getString(row, "itemRefId");
                    item.count = getInt(row, "count");

                    if (!item.recordKey.empty() && !item.itemRefId.empty() && item.count > 0)
                        mActorInventoryByRecordKey[item.recordKey].push_back(std::move(item));
                });
            for (auto& [recordKey, items] : mActorInventoryByRecordKey)
            {
                static_cast<void>(recordKey);
                std::sort(items.begin(), items.end(),
                    [](const WorldActorInventoryItem& left, const WorldActorInventoryItem& right) {
                        if (left.itemOrder != right.itemOrder)
                            return left.itemOrder < right.itemOrder;
                        if (left.itemRefId != right.itemRefId)
                            return left.itemRefId < right.itemRefId;
                        return left.count < right.count;
                    });
            }

            mStats.containerInventoryItemCount = readJsonlTable(root, "container_inventory.jsonl",
                containerInventoryRowSchema, [&](const boost::property_tree::ptree& row) {
                    WorldContainerInventoryItem item;
                    item.recordKey = normalizedLookupKey(getString(row, "recordKey"));
                    item.recordId = getString(row, "recordId");
                    item.sourceFile = getString(row, "sourceFile");
                    item.loadOrderIndex = getSizeT(row, "loadOrderIndex");
                    item.engineContentIndex = getSizeT(row, "engineContentIndex");
                    item.recordIndex = getSizeT(row, "recordIndex");
                    item.itemOrder = getSizeT(row, "itemOrder");
                    item.itemRefId = getString(row, "itemRefId");
                    item.count = getInt(row, "count");

                    if (!item.recordKey.empty() && !item.itemRefId.empty() && item.count > 0)
                        mContainerInventoryByRecordKey[item.recordKey].push_back(std::move(item));
                });
            for (auto& [recordKey, items] : mContainerInventoryByRecordKey)
            {
                static_cast<void>(recordKey);
                std::sort(items.begin(), items.end(),
                    [](const WorldContainerInventoryItem& left, const WorldContainerInventoryItem& right) {
                        if (left.itemOrder != right.itemOrder)
                            return left.itemOrder < right.itemOrder;
                        if (left.itemRefId != right.itemRefId)
                            return left.itemRefId < right.itemRefId;
                        return left.count < right.count;
                    });
            }

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

                    resolveBaseRecordForReferenceLocked(ref);

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
                "Loaded CommunityMP world database root=%s loadOrder=%zu records=%zu actorInventories=%zu actorItems=%zu containerInventories=%zu containerItems=%zu cells=%zu activeCells=%zu refs=%zu activeRefs=%zu actors=%zu containers=%zu doors=%zu indexedCells=%zu",
                pathToLogString(root).c_str(), mStats.loadOrderEntryCount, mStats.recordWinnerCount,
                mStats.actorInventoryRecordCount, mStats.actorInventoryItemCount,
                mStats.containerInventoryRecordCount, mStats.containerInventoryItemCount,
                mStats.cellRecordCount, mStats.activeCellRecordCount, mStats.cellReferenceCount,
                mStats.activeCellReferenceCount, mStats.actorReferenceCount, mStats.containerReferenceCount,
                mStats.doorReferenceCount,
                mStats.cellReferenceIndexedCellCount);
        }
        catch (const std::exception& e)
        {
            mStats.loaded = false;
            mStats.lastError = e.what();
            mLoadOrder.clear();
            mLoadOrderByContentFile.clear();
            mRecordWinnersByWinnerKey.clear();
            mRecordWinnerKeysByRecordKey.clear();
            mActorInventoryByRecordKey.clear();
            mContainerInventoryByRecordKey.clear();
            mCellsByKey.clear();
            mReferencesByKey.clear();
            mReferenceKeysByEffectiveCellKey.clear();

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "CommunityMP world database unavailable root=%s error=%s",
                pathToLogString(root).c_str(), mStats.lastError.c_str());
        }
    }

    void WorldDatabaseStore::resolveBaseRecordForReferenceLocked(WorldCellReferenceRecord& ref) const
    {
        ref.baseRecordResolved = false;
        ref.baseRecordAmbiguous = false;
        ref.baseRecordDeleted = false;
        ref.baseRecordWinnerKey.clear();
        ref.baseRecordKey = normalizedLookupKey(ref.refId);
        ref.baseRecordId.clear();
        ref.baseRecordType.clear();
        ref.baseRecordTypeInt = 0;
        ref.baseRecordCategory.clear();
        ref.baseRecordSourceFile.clear();
        ref.baseRecordLoadOrderIndex = 0;
        ref.baseActorInventoryImported = false;
        ref.baseActorInventoryItemCount = 0;
        ref.baseContainerInventoryImported = false;
        ref.baseContainerInventoryItemCount = 0;
        ref.baseActorAiAvailable = false;
        ref.baseActorAiPackageCount = 0;
        ref.baseActorAiAction = 0;
        ref.baseActorAiDistance = 0;
        ref.baseActorAiDuration = 0;
        ref.baseActorAiShouldRepeat = false;
        ref.baseActorAiCoordinateX = 0.f;
        ref.baseActorAiCoordinateY = 0.f;
        ref.baseActorAiCoordinateZ = 0.f;
        ref.baseActorAiTargetId.clear();
        ref.baseActorAiCellName.clear();
        ref.baseActorAiHello = 0;
        ref.baseActorAiFight = 0;
        ref.baseActorAiFlee = 0;
        ref.baseActorAiAlarm = 0;

        if (ref.baseRecordKey.empty())
            return;

        const auto keysIt = mRecordWinnerKeysByRecordKey.find(ref.baseRecordKey);
        if (keysIt == mRecordWinnerKeysByRecordKey.end())
            return;

        std::vector<const WorldRecordWinner*> nonDeleted;
        std::vector<const WorldRecordWinner*> deleted;
        for (const std::string& winnerKey : keysIt->second)
        {
            const auto winnerIt = mRecordWinnersByWinnerKey.find(winnerKey);
            if (winnerIt == mRecordWinnersByWinnerKey.end())
                continue;

            const WorldRecordWinner& winner = winnerIt->second;
            if (winner.deleted || winner.tombstone)
                deleted.push_back(&winner);
            else
                nonDeleted.push_back(&winner);
        }

        const WorldRecordWinner* selected = nullptr;
        if (nonDeleted.size() == 1)
            selected = nonDeleted.front();
        else if (nonDeleted.empty() && deleted.size() == 1)
            selected = deleted.front();
        else
        {
            ref.baseRecordAmbiguous = !nonDeleted.empty() || deleted.size() > 1;
            return;
        }

        ref.baseRecordResolved = true;
        ref.baseRecordDeleted = selected->deleted || selected->tombstone;
        ref.baseRecordWinnerKey = selected->winnerKey;
        ref.baseRecordKey = selected->recordKey;
        ref.baseRecordId = selected->recordId;
        ref.baseRecordType = selected->recordType;
        ref.baseRecordTypeInt = selected->recordTypeInt;
        ref.baseRecordCategory = selected->category;
        ref.baseRecordSourceFile = selected->sourceFile;
        ref.baseRecordLoadOrderIndex = selected->loadOrderIndex;
        const auto actorInventoryIt = mActorInventoryByRecordKey.find(selected->recordKey);
        const std::size_t importedActorInventoryItemCount
            = actorInventoryIt != mActorInventoryByRecordKey.end() ? actorInventoryIt->second.size() : 0;
        ref.baseActorInventoryImported = selected->actorInventoryImported
            && importedActorInventoryItemCount == selected->actorInventoryItemCount;
        ref.baseActorInventoryItemCount
            = ref.baseActorInventoryImported ? importedActorInventoryItemCount : 0;
        const auto inventoryIt = mContainerInventoryByRecordKey.find(selected->recordKey);
        const std::size_t importedInventoryItemCount
            = inventoryIt != mContainerInventoryByRecordKey.end() ? inventoryIt->second.size() : 0;
        ref.baseContainerInventoryImported = selected->containerInventoryImported
            && importedInventoryItemCount == selected->containerInventoryItemCount;
        ref.baseContainerInventoryItemCount
            = ref.baseContainerInventoryImported ? importedInventoryItemCount : 0;
        ref.baseActorAiAvailable = selected->actorAiAvailable;
        ref.baseActorAiPackageCount = selected->actorAiPackageCount;
        ref.baseActorAiAction = selected->actorAiAction;
        ref.baseActorAiDistance = selected->actorAiDistance;
        ref.baseActorAiDuration = selected->actorAiDuration;
        ref.baseActorAiShouldRepeat = selected->actorAiShouldRepeat;
        ref.baseActorAiCoordinateX = selected->actorAiCoordinateX;
        ref.baseActorAiCoordinateY = selected->actorAiCoordinateY;
        ref.baseActorAiCoordinateZ = selected->actorAiCoordinateZ;
        ref.baseActorAiTargetId = selected->actorAiTargetId;
        ref.baseActorAiCellName = selected->actorAiCellName;
        ref.baseActorAiHello = selected->actorAiHello;
        ref.baseActorAiFight = selected->actorAiFight;
        ref.baseActorAiFlee = selected->actorAiFlee;
        ref.baseActorAiAlarm = selected->actorAiAlarm;
    }

    void WorldDatabaseStore::rebuildReferenceIndexesLocked()
    {
        mReferenceKeysByEffectiveCellKey.clear();
        mStats.activeCellReferenceCount = 0;
        mStats.cellReferenceDeletedCount = 0;
        mStats.cellReferenceMovedCount = 0;
        mStats.cellReferenceTeleportCount = 0;
        mStats.cellReferenceIndexedCellCount = 0;
        mStats.baseRecordResolvedReferenceCount = 0;
        mStats.baseRecordUnresolvedReferenceCount = 0;
        mStats.baseRecordAmbiguousReferenceCount = 0;
        mStats.baseRecordDeletedReferenceCount = 0;
        mStats.actorReferenceCount = 0;
        mStats.containerReferenceCount = 0;
        mStats.doorReferenceCount = 0;
        mStats.itemReferenceCount = 0;
        mStats.staticReferenceCount = 0;
        mStats.activatorReferenceCount = 0;
        mStats.levelledItemReferenceCount = 0;
        mStats.otherReferenceCount = 0;

        for (const auto& [refKey, ref] : mReferencesByKey)
        {
            const bool deletedRef = ref.deleted || ref.tombstone;
            if (deletedRef)
                ++mStats.cellReferenceDeletedCount;
            else
            {
                ++mStats.activeCellReferenceCount;

                if (ref.baseRecordAmbiguous)
                    ++mStats.baseRecordAmbiguousReferenceCount;
                else if (!ref.baseRecordResolved)
                    ++mStats.baseRecordUnresolvedReferenceCount;
                else
                {
                    ++mStats.baseRecordResolvedReferenceCount;
                    if (ref.baseRecordDeleted)
                        ++mStats.baseRecordDeletedReferenceCount;
                    else if (isActorRecordType(ref.baseRecordType))
                        ++mStats.actorReferenceCount;
                    else if (ref.baseRecordCategory == "container")
                        ++mStats.containerReferenceCount;
                    else if (ref.baseRecordCategory == "door")
                        ++mStats.doorReferenceCount;
                    else if (ref.baseRecordCategory == "item")
                        ++mStats.itemReferenceCount;
                    else if (ref.baseRecordCategory == "static")
                        ++mStats.staticReferenceCount;
                    else if (ref.baseRecordCategory == "activator")
                        ++mStats.activatorReferenceCount;
                    else if (ref.baseRecordCategory == "levelledItem")
                        ++mStats.levelledItemReferenceCount;
                    else
                        ++mStats.otherReferenceCount;
                }
            }

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

    std::optional<WorldRecordWinner> WorldDatabaseStore::findWinningRecordByTypeAndKey(
        std::string_view recordType, std::string_view recordKey) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mRecordWinnersByWinnerKey.find(winnerLookupKey(recordType, normalizedLookupKey(recordKey)));
        if (it == mRecordWinnersByWinnerKey.end())
            return std::nullopt;

        return it->second;
    }

    std::vector<WorldRecordWinner> WorldDatabaseStore::findWinningRecordsByRecordKey(std::string_view recordKey) const
    {
        std::lock_guard lock(mMutex);
        const auto keysIt = mRecordWinnerKeysByRecordKey.find(normalizedLookupKey(recordKey));
        if (keysIt == mRecordWinnerKeysByRecordKey.end())
            return {};

        std::vector<WorldRecordWinner> result;
        result.reserve(keysIt->second.size());
        for (const std::string& winnerKey : keysIt->second)
        {
            const auto winnerIt = mRecordWinnersByWinnerKey.find(winnerKey);
            if (winnerIt != mRecordWinnersByWinnerKey.end())
                result.push_back(winnerIt->second);
        }

        std::sort(result.begin(), result.end(), [](const WorldRecordWinner& left, const WorldRecordWinner& right) {
            if (left.loadOrderIndex != right.loadOrderIndex)
                return left.loadOrderIndex < right.loadOrderIndex;
            if (left.recordType != right.recordType)
                return left.recordType < right.recordType;
            return left.recordKey < right.recordKey;
        });
        return result;
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

    std::optional<WorldRecordWinner> WorldDatabaseStore::findBaseRecordForReference(std::string_view refKey) const
    {
        std::lock_guard lock(mMutex);
        const auto refIt = mReferencesByKey.find(std::string(refKey));
        if (refIt == mReferencesByKey.end() || !refIt->second.baseRecordResolved
            || refIt->second.baseRecordAmbiguous || refIt->second.baseRecordWinnerKey.empty())
            return std::nullopt;

        const auto winnerIt = mRecordWinnersByWinnerKey.find(refIt->second.baseRecordWinnerKey);
        if (winnerIt == mRecordWinnersByWinnerKey.end())
            return std::nullopt;

        return winnerIt->second;
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

    std::vector<WorldActorInventoryItem> WorldDatabaseStore::findActorInventoryByRecordKey(
        std::string_view recordKey) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mActorInventoryByRecordKey.find(normalizedLookupKey(recordKey));
        if (it == mActorInventoryByRecordKey.end())
            return {};

        return it->second;
    }

    std::vector<WorldContainerInventoryItem> WorldDatabaseStore::findContainerInventoryByRecordKey(
        std::string_view recordKey) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mContainerInventoryByRecordKey.find(normalizedLookupKey(recordKey));
        if (it == mContainerInventoryByRecordKey.end())
            return {};

        return it->second;
    }
}
