#ifndef OPENMW_MP_WORLDDATABASESTORE_HPP
#define OPENMW_MP_WORLDDATABASESTORE_HPP

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp
{
    struct WorldDatabaseStatistics
    {
        bool attempted = false;
        bool loaded = false;
        std::string backend = "jsonl-worlddb";
        std::filesystem::path rootPath;
        std::string lastError;
        std::string loadOrderSource = "openmw-application-settings-content-vector";
        std::string loadOrderRule = "higher-engineContentIndex-overrides-lower-engineContentIndex-for-the-same-record-key";
        std::size_t manifestCount = 0;
        std::size_t loadOrderEntryCount = 0;
        std::size_t builtinContentFileCount = 0;
        std::size_t esmLikeContentFileCount = 0;
        std::size_t cellRecordCount = 0;
        std::size_t activeCellRecordCount = 0;
        std::size_t cellReferenceCount = 0;
        std::size_t activeCellReferenceCount = 0;
        std::size_t cellReferenceDeletedCount = 0;
        std::size_t cellReferenceMovedCount = 0;
        std::size_t cellReferenceTeleportCount = 0;
        std::size_t cellReferenceIndexedCellCount = 0;
        std::size_t recordWinnerCount = 0;
        std::size_t recordWinnerDeletedCount = 0;
        std::size_t actorProfileRecordCount = 0;
        std::size_t actorProfileNpcCount = 0;
        std::size_t actorProfileCreatureCount = 0;
        std::size_t actorProfileAutocalcNpcCount = 0;
        std::size_t actorAiPackageRecordCount = 0;
        std::size_t actorAiPackageItemCount = 0;
        std::size_t actorInventoryRecordCount = 0;
        std::size_t actorInventoryItemCount = 0;
        std::size_t actorSpellbookRecordCount = 0;
        std::size_t actorSpellbookSpellCount = 0;
        std::size_t actorStatsDynamicRecordCount = 0;
        std::size_t actorStatsDynamicItemCount = 0;
        std::size_t actorEquipmentRecordCount = 0;
        std::size_t actorEquipmentItemCount = 0;
        std::size_t containerInventoryRecordCount = 0;
        std::size_t containerInventoryItemCount = 0;
        std::size_t pathgridRecordCount = 0;
        std::size_t pathgridPointCount = 0;
        std::size_t pathgridEdgeCount = 0;
        std::size_t pathgridIndexedCellCount = 0;
        std::size_t baseRecordResolvedReferenceCount = 0;
        std::size_t baseRecordUnresolvedReferenceCount = 0;
        std::size_t baseRecordAmbiguousReferenceCount = 0;
        std::size_t baseRecordDeletedReferenceCount = 0;
        std::size_t actorReferenceCount = 0;
        std::size_t containerReferenceCount = 0;
        std::size_t doorReferenceCount = 0;
        std::size_t itemReferenceCount = 0;
        std::size_t staticReferenceCount = 0;
        std::size_t activatorReferenceCount = 0;
        std::size_t levelledItemReferenceCount = 0;
        std::size_t otherReferenceCount = 0;
    };

    struct WorldLoadOrderEntry
    {
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::string contentFile;
        std::string extension;
        bool builtin = false;
        bool esmLike = false;
        long long dominanceRank = -1;
    };

    struct WorldCellRecord
    {
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::string sourceFile;
        std::size_t recordIndex = 0;
        unsigned int recordFlags = 0;
        bool deleted = false;
        bool recordFlagDeleted = false;
        bool deleteSubrecord = false;
        std::string cellKey;
        std::string cellName;
        bool interior = false;
        int gridX = 0;
        int gridY = 0;
        int cellFlags = 0;
        std::string region;
        bool hasWaterHeight = false;
        float water = 0.f;
        unsigned int mapColor = 0;
        int refNumCounter = 0;
        bool hasAmbient = false;
        unsigned int ambientColor = 0;
        unsigned int sunlightColor = 0;
        unsigned int fogColor = 0;
        float fogDensity = 0.f;
    };

    struct WorldRecordWinner
    {
        std::string winnerKey;
        std::string recordType;
        unsigned int recordTypeInt = 0;
        bool recordKeyAvailable = false;
        std::string recordKey;
        std::string recordId;
        std::string recordKeyKind;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        unsigned int flags = 0;
        bool deleted = false;
        bool recordFlagDeleted = false;
        bool deleteSubrecord = false;
        bool tombstone = false;
        std::size_t recordOffset = 0;
        std::size_t dataOffset = 0;
        std::size_t dataSize = 0;
        std::string loadOrderRule;
        std::string category;
        bool actorAiAvailable = false;
        std::size_t actorAiPackageCount = 0;
        unsigned int actorAiAction = 0;
        unsigned int actorAiDistance = 0;
        unsigned int actorAiDuration = 0;
        bool actorAiShouldRepeat = false;
        float actorAiCoordinateX = 0.f;
        float actorAiCoordinateY = 0.f;
        float actorAiCoordinateZ = 0.f;
        std::string actorAiTargetId;
        std::string actorAiCellName;
        unsigned int actorAiHello = 0;
        unsigned int actorAiFight = 0;
        unsigned int actorAiFlee = 0;
        unsigned int actorAiAlarm = 0;
        bool actorAiPackagesImported = false;
        bool actorProfileImported = false;
        bool actorProfileNpc = false;
        bool actorProfileAutocalc = false;
        bool actorInventoryImported = false;
        std::size_t actorInventoryItemCount = 0;
        bool actorSpellbookImported = false;
        std::size_t actorSpellbookSpellCount = 0;
        bool actorStatsDynamicImported = false;
        bool actorStatsDynamicAutocalc = false;
        std::size_t actorStatsDynamicItemCount = 0;
        bool actorEquipmentImported = false;
        std::size_t actorEquipmentItemCount = 0;
        bool containerInventoryImported = false;
        std::size_t containerInventoryItemCount = 0;
        bool pathgridImported = false;
        std::size_t pathgridPointCount = 0;
        std::size_t pathgridEdgeCount = 0;
    };

    struct WorldRecordInventoryItem
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        std::size_t itemOrder = 0;
        std::string itemRefId;
        int count = 0;
    };

    using WorldActorInventoryItem = WorldRecordInventoryItem;
    using WorldContainerInventoryItem = WorldRecordInventoryItem;

    struct WorldActorProfileRecord
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        std::string actorKind;
        bool npc = false;
        bool autocalc = false;
        int level = 0;
        int flags = 0;
        int bloodType = 0;
        int services = 0;
        std::string displayName;
        std::string model;
        std::string script;
        std::string race;
        std::string classId;
        std::string faction;
        std::string head;
        std::string hair;
        std::string original;
        int factionRank = -1;
        int disposition = -1;
        int reputation = -1;
        int gold = 0;
        int creatureType = -1;
        int soul = -1;
        int combat = -1;
        int magic = -1;
        int stealth = -1;
        float scale = 1.f;
        std::array<int, 8> attributes{};
        std::array<int, 27> skills{};
        std::array<int, 6> attacks{};
    };

    struct WorldActorAiPackageRecord
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        std::size_t packageOrder = 0;
        std::string packageType;
        unsigned int packageTypeInt = 0;
        unsigned int action = 0;
        int distance = 0;
        int duration = 0;
        int timeOfDay = -1;
        std::array<int, 8> idle{};
        bool shouldRepeat = false;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        std::string targetId;
        std::string cellName;
    };

    struct WorldActorSpellbookEntry
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        std::size_t spellOrder = 0;
        std::string spellId;
    };

    struct WorldActorStatsDynamicItem
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        int statIndex = -1;
        float base = 0.f;
        float mod = 0.f;
        float current = 0.f;
        float damage = 0.f;
        float progress = 0.f;
    };

    struct WorldActorEquipmentItem
    {
        std::string recordKey;
        std::string recordId;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        int slot = -1;
        std::string itemRefId;
        int count = 0;
        int charge = -1;
        float enchantmentCharge = -1.f;
    };

    struct WorldPathgridPointRecord
    {
        std::string cellKey;
        std::string cellName;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        int gridX = 0;
        int gridY = 0;
        int granularity = 0;
        std::size_t pointIndex = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        unsigned int autogenerated = 0;
        unsigned int connectionCount = 0;
    };

    struct WorldPathgridEdgeRecord
    {
        std::string cellKey;
        std::string cellName;
        std::string sourceFile;
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::size_t recordIndex = 0;
        std::size_t edgeOrder = 0;
        std::size_t fromPoint = 0;
        std::size_t toPoint = 0;
    };

    struct WorldCellReferenceRecord
    {
        std::size_t loadOrderIndex = 0;
        std::size_t engineContentIndex = 0;
        std::string sourceFile;
        std::size_t cellRecordIndex = 0;
        std::size_t referenceOrder = 0;
        std::string refKey;
        int refNumContentFile = -1;
        unsigned int refNumIndex = 0;
        std::string refId;
        std::string sourceCellKey;
        std::string effectiveCellKey;
        bool moved = false;
        bool deleted = false;
        bool tombstone = false;
        int movedTargetX = 0;
        int movedTargetY = 0;
        int count = 1;
        float scale = 1.f;
        float posX = 0.f;
        float posY = 0.f;
        float posZ = 0.f;
        float rotX = 0.f;
        float rotY = 0.f;
        float rotZ = 0.f;
        std::string owner;
        std::string globalVariable;
        std::string soul;
        std::string faction;
        int factionRank = -1;
        int chargeInt = -1;
        float chargeIntRemainder = 0.f;
        float enchantmentCharge = -1.f;
        bool teleport = false;
        std::string destCell;
        float doorDestPosX = 0.f;
        float doorDestPosY = 0.f;
        float doorDestPosZ = 0.f;
        float doorDestRotX = 0.f;
        float doorDestRotY = 0.f;
        float doorDestRotZ = 0.f;
        int lockLevel = 0;
        bool locked = false;
        std::string key;
        std::string trap;
        int referenceBlocked = -1;
        std::string loadOrderRule;
        bool baseRecordResolved = false;
        bool baseRecordAmbiguous = false;
        bool baseRecordDeleted = false;
        std::string baseRecordWinnerKey;
        std::string baseRecordKey;
        std::string baseRecordId;
        std::string baseRecordType;
        unsigned int baseRecordTypeInt = 0;
        std::string baseRecordCategory;
        std::string baseRecordSourceFile;
        std::size_t baseRecordLoadOrderIndex = 0;
        bool baseActorProfileImported = false;
        bool baseActorProfileNpc = false;
        bool baseActorProfileAutocalc = false;
        int baseActorProfileLevel = 0;
        bool baseActorAiPackagesImported = false;
        std::size_t baseActorAiPackageItemCount = 0;
        bool baseActorInventoryImported = false;
        std::size_t baseActorInventoryItemCount = 0;
        bool baseActorSpellbookImported = false;
        std::size_t baseActorSpellbookSpellCount = 0;
        bool baseActorStatsDynamicImported = false;
        bool baseActorStatsDynamicAutocalc = false;
        std::size_t baseActorStatsDynamicItemCount = 0;
        bool baseActorEquipmentImported = false;
        std::size_t baseActorEquipmentItemCount = 0;
        bool baseContainerInventoryImported = false;
        std::size_t baseContainerInventoryItemCount = 0;
        bool baseActorAiAvailable = false;
        std::size_t baseActorAiPackageCount = 0;
        unsigned int baseActorAiAction = 0;
        unsigned int baseActorAiDistance = 0;
        unsigned int baseActorAiDuration = 0;
        bool baseActorAiShouldRepeat = false;
        float baseActorAiCoordinateX = 0.f;
        float baseActorAiCoordinateY = 0.f;
        float baseActorAiCoordinateZ = 0.f;
        std::string baseActorAiTargetId;
        std::string baseActorAiCellName;
        unsigned int baseActorAiHello = 0;
        unsigned int baseActorAiFight = 0;
        unsigned int baseActorAiFlee = 0;
        unsigned int baseActorAiAlarm = 0;
    };

    class WorldDatabaseStore
    {
    public:
        static WorldDatabaseStore& get();

        void ensureLoaded();
        void loadFromDirectory(const std::filesystem::path& root);
        WorldDatabaseStatistics statistics() const;

        std::vector<WorldLoadOrderEntry> loadOrder() const;
        std::optional<WorldLoadOrderEntry> findLoadOrderEntryByContentFile(std::string_view contentFile) const;
        std::optional<WorldRecordWinner> findWinningRecordByTypeAndKey(
            std::string_view recordType, std::string_view recordKey) const;
        std::vector<WorldRecordWinner> findWinningRecordsByRecordKey(std::string_view recordKey) const;
        std::optional<WorldCellRecord> findCellByKey(std::string_view cellKey) const;
        std::optional<WorldCellReferenceRecord> findReferenceByKey(std::string_view refKey) const;
        std::optional<WorldRecordWinner> findBaseRecordForReference(std::string_view refKey) const;
        std::vector<WorldCellReferenceRecord> findReferencesByCellKey(
            std::string_view cellKey, bool includeDeleted = false) const;
        std::optional<WorldActorProfileRecord> findActorProfileByRecordKey(std::string_view recordKey) const;
        std::vector<WorldActorAiPackageRecord> findActorAiPackagesByRecordKey(std::string_view recordKey) const;
        std::vector<WorldActorInventoryItem> findActorInventoryByRecordKey(std::string_view recordKey) const;
        std::vector<WorldActorSpellbookEntry> findActorSpellbookByRecordKey(std::string_view recordKey) const;
        std::vector<WorldActorStatsDynamicItem> findActorStatsDynamicByRecordKey(std::string_view recordKey) const;
        std::vector<WorldActorEquipmentItem> findActorEquipmentByRecordKey(std::string_view recordKey) const;
        std::vector<WorldContainerInventoryItem> findContainerInventoryByRecordKey(std::string_view recordKey) const;
        std::vector<WorldPathgridPointRecord> findPathgridPointsByCellKey(std::string_view cellKey) const;
        std::vector<WorldPathgridEdgeRecord> findPathgridEdgesByCellKey(std::string_view cellKey) const;

    private:
        WorldDatabaseStore() = default;

        void loadFromDirectoryLocked(const std::filesystem::path& root);
        void resolveBaseRecordForReferenceLocked(WorldCellReferenceRecord& ref) const;
        void rebuildReferenceIndexesLocked();

        mutable std::mutex mMutex;
        bool mLoadAttempted = false;
        WorldDatabaseStatistics mStats;
        std::vector<WorldLoadOrderEntry> mLoadOrder;
        std::map<std::string, WorldLoadOrderEntry> mLoadOrderByContentFile;
        std::map<std::string, WorldRecordWinner> mRecordWinnersByWinnerKey;
        std::map<std::string, std::vector<std::string>> mRecordWinnerKeysByRecordKey;
        std::map<std::string, WorldActorProfileRecord> mActorProfilesByRecordKey;
        std::map<std::string, std::vector<WorldActorAiPackageRecord>> mActorAiPackagesByRecordKey;
        std::map<std::string, std::vector<WorldActorInventoryItem>> mActorInventoryByRecordKey;
        std::map<std::string, std::vector<WorldActorSpellbookEntry>> mActorSpellbookByRecordKey;
        std::map<std::string, std::vector<WorldActorStatsDynamicItem>> mActorStatsDynamicByRecordKey;
        std::map<std::string, std::vector<WorldActorEquipmentItem>> mActorEquipmentByRecordKey;
        std::map<std::string, std::vector<WorldContainerInventoryItem>> mContainerInventoryByRecordKey;
        std::map<std::string, std::vector<WorldPathgridPointRecord>> mPathgridPointsByCellKey;
        std::map<std::string, std::vector<WorldPathgridEdgeRecord>> mPathgridEdgesByCellKey;
        std::map<std::string, WorldCellRecord> mCellsByKey;
        std::map<std::string, WorldCellReferenceRecord> mReferencesByKey;
        std::map<std::string, std::vector<std::string>> mReferenceKeysByEffectiveCellKey;
    };
}

#endif // OPENMW_MP_WORLDDATABASESTORE_HPP
