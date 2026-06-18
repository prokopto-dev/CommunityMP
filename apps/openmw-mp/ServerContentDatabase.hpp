#ifndef OPENMW_MP_SERVERCONTENTDATABASE_HPP
#define OPENMW_MP_SERVERCONTENTDATABASE_HPP

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "ServerContentRegistry.hpp"

namespace mwmp
{
    struct ServerContentDatabaseStatistics
    {
        bool attempted = false;
        bool available = false;
        bool changed = false;
        std::string backend = "jsonl-package";
        std::filesystem::path rootPath;
        std::filesystem::path manifestPath;
        std::filesystem::path generatedQuestDatabasePath;
        std::string lastError;
        std::string loadOrderSource;
        std::string loadOrderRule;
        std::size_t dataDirCount = 0;
        std::size_t loadOrderEntryCount = 0;
        std::size_t contentFileCount = 0;
        std::size_t esmLikeContentFileCount = 0;
        std::size_t resolvedContentFileCount = 0;
        std::size_t unresolvedContentFileCount = 0;
        std::size_t checksumCount = 0;
        std::size_t recordIndexCount = 0;
        std::size_t recordKeyCount = 0;
        std::size_t recordUnkeyedCount = 0;
        std::size_t recordWinnerCount = 0;
        std::size_t recordWinnerDeletedCount = 0;
        std::size_t recordImportErrorCount = 0;
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
        std::size_t itemEquipmentRecordCount = 0;
        std::size_t actorEquipmentRecordCount = 0;
        std::size_t actorEquipmentItemCount = 0;
        std::size_t containerInventoryRecordCount = 0;
        std::size_t containerInventoryItemCount = 0;
        std::size_t pathgridRecordCount = 0;
        std::size_t pathgridPointCount = 0;
        std::size_t pathgridEdgeCount = 0;
        std::size_t cellRecordCount = 0;
        std::size_t cellReferenceCount = 0;
        std::size_t cellReferenceMovedCount = 0;
        std::size_t cellReferenceDeletedCount = 0;
        std::size_t cellReferenceWinnerCount = 0;
        std::size_t cellReferenceWinnerDeletedCount = 0;
        std::size_t cellImportErrorCount = 0;
        std::size_t questSourceRowCount = 0;
        std::size_t questSourcePackageCount = 0;
        std::size_t questSourceDialogueCount = 0;
        std::size_t questSourceInfoCount = 0;
        std::size_t questSourceImportErrorCount = 0;
        std::size_t generatedQuestDatabasePackageCount = 0;
        std::size_t generatedQuestDefinitionCount = 0;
        std::size_t generatedQuestStepCount = 0;
        std::size_t generatedDialogueTopicCount = 0;
        std::size_t generatedDialogueResponseCount = 0;
        std::size_t generatedConditionCount = 0;
        std::size_t generatedQuestEffectCount = 0;
        std::size_t generatedLegacyEffectCount = 0;
        std::size_t generatedQuestDatabaseImportErrorCount = 0;
        std::size_t archiveCount = 0;
        std::size_t resolvedArchiveCount = 0;
        std::size_t unresolvedArchiveCount = 0;
        std::size_t archiveFileCount = 0;
        std::size_t assetProviderCount = 0;
        std::size_t resolvedAssetCount = 0;
        std::size_t assetImportErrorCount = 0;
        std::size_t tableCount = 0;
    };

    class ServerContentDatabase
    {
    public:
        static ServerContentDatabase& get();

        void updateFromOpenMwContentPlan(const std::vector<std::filesystem::path>& dataDirs,
            const std::vector<std::string>& contentFiles,
            const std::vector<std::string>& archives,
            const std::string& encoding,
            const std::vector<ServerDataFileRequirement>& dataFileRequirements);

        ServerContentDatabaseStatistics statistics() const;

    private:
        ServerContentDatabase() = default;

        mutable std::mutex mMutex;
        ServerContentDatabaseStatistics mStats;
    };
}

#endif // OPENMW_MP_SERVERCONTENTDATABASE_HPP
