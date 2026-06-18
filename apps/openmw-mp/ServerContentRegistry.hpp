#ifndef OPENMW_MP_SERVERCONTENTREGISTRY_HPP
#define OPENMW_MP_SERVERCONTENTREGISTRY_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mwmp
{
    struct ServerDataFileRequirement
    {
        std::string name;
        std::vector<std::string> checksums;
    };

    struct ServerContentRegistryStatistics
    {
        std::string backend = "communitymp-server-data-files-xml";
        bool attempted = false;
        bool loaded = false;
        std::filesystem::path path;
        std::string lastError;
        std::filesystem::path loadOrderPath;
        std::string loadOrderSource = "data-files-xml-order";
        bool loadOrderAttempted = false;
        bool loadOrderLoaded = false;
        bool enrichedFromOpenMwContentPlan = false;
        std::size_t dataFileCount = 0;
        std::size_t checksumCount = 0;
        std::size_t loadOrderEntryCount = 0;
        std::size_t loadOrderAppliedCount = 0;
        std::size_t loadOrderDuplicateCount = 0;
        std::size_t loadOrderMissingRegistryCount = 0;
        std::size_t loadOrderMissingConfigCount = 0;
        std::size_t contentPlanFileCount = 0;
        std::size_t computedChecksumCount = 0;
        std::size_t unresolvedContentFileCount = 0;
    };

    class ServerContentRegistry
    {
    public:
        static ServerContentRegistry& get();

        void loadFromDataDirectory(const std::filesystem::path& dataDirectory);
        void enrichFromOpenMwContentPlan(
            const std::vector<std::filesystem::path>& dataDirs, const std::vector<std::string>& contentFiles);

        const std::vector<ServerDataFileRequirement>& dataFiles() const;
        const ServerContentRegistryStatistics& statistics() const;

    private:
        std::vector<ServerDataFileRequirement> mDataFiles;
        ServerContentRegistryStatistics mStats;
    };
}

#endif // OPENMW_MP_SERVERCONTENTREGISTRY_HPP
