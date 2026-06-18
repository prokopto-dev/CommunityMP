#ifndef OPENMW_MP_WORLDDATABASESTORE_HPP
#define OPENMW_MP_WORLDDATABASESTORE_HPP

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
        std::optional<WorldCellRecord> findCellByKey(std::string_view cellKey) const;
        std::optional<WorldCellReferenceRecord> findReferenceByKey(std::string_view refKey) const;
        std::vector<WorldCellReferenceRecord> findReferencesByCellKey(
            std::string_view cellKey, bool includeDeleted = false) const;

    private:
        WorldDatabaseStore() = default;

        void loadFromDirectoryLocked(const std::filesystem::path& root);
        void rebuildReferenceIndexesLocked();

        mutable std::mutex mMutex;
        bool mLoadAttempted = false;
        WorldDatabaseStatistics mStats;
        std::vector<WorldLoadOrderEntry> mLoadOrder;
        std::map<std::string, WorldLoadOrderEntry> mLoadOrderByContentFile;
        std::map<std::string, WorldCellRecord> mCellsByKey;
        std::map<std::string, WorldCellReferenceRecord> mReferencesByKey;
        std::map<std::string, std::vector<std::string>> mReferenceKeysByEffectiveCellKey;
    };
}

#endif // OPENMW_MP_WORLDDATABASESTORE_HPP
