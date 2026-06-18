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
        std::size_t recordImportErrorCount = 0;
        std::size_t tableCount = 0;
    };

    class ServerContentDatabase
    {
    public:
        static ServerContentDatabase& get();

        void updateFromOpenMwContentPlan(const std::vector<std::filesystem::path>& dataDirs,
            const std::vector<std::string>& contentFiles,
            const std::vector<ServerDataFileRequirement>& dataFileRequirements);

        ServerContentDatabaseStatistics statistics() const;

    private:
        ServerContentDatabase() = default;

        mutable std::mutex mMutex;
        ServerContentDatabaseStatistics mStats;
    };
}

#endif // OPENMW_MP_SERVERCONTENTDATABASE_HPP
