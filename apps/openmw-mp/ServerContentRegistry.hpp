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
        std::size_t dataFileCount = 0;
        std::size_t checksumCount = 0;
    };

    class ServerContentRegistry
    {
    public:
        static ServerContentRegistry& get();

        void loadFromDataDirectory(const std::filesystem::path& dataDirectory);

        const std::vector<ServerDataFileRequirement>& dataFiles() const;
        const ServerContentRegistryStatistics& statistics() const;

    private:
        std::vector<ServerDataFileRequirement> mDataFiles;
        ServerContentRegistryStatistics mStats;
    };
}

#endif // OPENMW_MP_SERVERCONTENTREGISTRY_HPP
