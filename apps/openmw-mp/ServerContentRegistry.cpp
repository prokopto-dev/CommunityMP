#include "ServerContentRegistry.hpp"

#include <components/files/collections.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/Utils.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace
{
    constexpr const char* dataFilesRelativePath = "saves/server/config/data-files.xml";
    constexpr const char* builtinOpenMwScripts = "builtin.omwscripts";

    std::string getAttribute(const boost::property_tree::ptree& node, const char* key)
    {
        return node.get<std::string>(std::string("<xmlattr>.") + key, "");
    }

    const boost::property_tree::ptree* findNamedNode(
        const boost::property_tree::ptree& parent, const char* key, const char* type = nullptr)
    {
        for (const auto& child : parent)
        {
            if (child.first != "node")
                continue;

            if (getAttribute(child.second, "key") != key)
                continue;

            if (type != nullptr && getAttribute(child.second, "type") != type)
                continue;

            return &child.second;
        }

        return nullptr;
    }

    std::size_t appendDataFiles(
        const boost::property_tree::ptree& dataNode, std::vector<mwmp::ServerDataFileRequirement>& dataFiles)
    {
        std::size_t checksumCount = 0;

        for (const auto& listEntry : dataNode)
        {
            if (listEntry.first != "node" || getAttribute(listEntry.second, "type") != "table")
                continue;

            for (const auto& pluginEntry : listEntry.second)
            {
                if (pluginEntry.first != "node"
                    || getAttribute(pluginEntry.second, "keyType") != "string"
                    || getAttribute(pluginEntry.second, "type") != "table")
                    continue;

                mwmp::ServerDataFileRequirement requirement;
                requirement.name = getAttribute(pluginEntry.second, "key");
                if (requirement.name.empty())
                    continue;

                for (const auto& checksumEntry : pluginEntry.second)
                {
                    if (checksumEntry.first != "node" || getAttribute(checksumEntry.second, "type") != "string")
                        continue;

                    std::string checksum = checksumEntry.second.get_value<std::string>();
                    if (!checksum.empty())
                    {
                        requirement.checksums.push_back(std::move(checksum));
                        ++checksumCount;
                    }
                }

                dataFiles.push_back(std::move(requirement));
            }
        }

        return checksumCount;
    }

    bool isBuiltinContentFile(const std::string& contentFile)
    {
        return Misc::StringUtils::ciEqual(contentFile, builtinOpenMwScripts);
    }

    mwmp::ServerDataFileRequirement* findRequirement(
        std::vector<mwmp::ServerDataFileRequirement>& dataFiles, const std::string& name)
    {
        auto found = std::find_if(dataFiles.begin(), dataFiles.end(),
            [&](const mwmp::ServerDataFileRequirement& requirement) {
                return Misc::StringUtils::ciEqual(requirement.name, name);
            });

        return found != dataFiles.end() ? &*found : nullptr;
    }

    bool appendUniqueChecksum(std::vector<std::string>& checksums, std::string checksum)
    {
        const auto found = std::find_if(checksums.begin(), checksums.end(), [&](const std::string& existing) {
            return Misc::StringUtils::ciEqual(existing, checksum);
        });

        if (found != checksums.end())
            return false;

        checksums.push_back(std::move(checksum));
        return true;
    }

    std::size_t countChecksums(const std::vector<mwmp::ServerDataFileRequirement>& dataFiles)
    {
        std::size_t result = 0;
        for (const mwmp::ServerDataFileRequirement& requirement : dataFiles)
            result += requirement.checksums.size();
        return result;
    }
}

namespace mwmp
{
    ServerContentRegistry& ServerContentRegistry::get()
    {
        static ServerContentRegistry registry;
        return registry;
    }

    void ServerContentRegistry::loadFromDataDirectory(const std::filesystem::path& dataDirectory)
    {
        mDataFiles.clear();
        mStats = {};
        mStats.attempted = true;
        mStats.path = dataDirectory / dataFilesRelativePath;

        try
        {
            if (!std::filesystem::exists(mStats.path))
                throw std::runtime_error("server data-file registry does not exist");

            boost::property_tree::ptree document;
            boost::property_tree::read_xml(
                mStats.path.string(), document, boost::property_tree::xml_parser::trim_whitespace);

            const boost::property_tree::ptree& save = document.get_child("save");
            if (save.get<std::string>("<xmlattr>.kind", "") != "server-data-files")
                throw std::runtime_error("unexpected server content registry kind");

            const boost::property_tree::ptree* dataNode = findNamedNode(save, "data", "table");
            if (dataNode == nullptr)
                throw std::runtime_error("server content registry has no data node");

            mStats.checksumCount = appendDataFiles(*dataNode, mDataFiles);
            mStats.dataFileCount = mDataFiles.size();
            mStats.loaded = true;
        }
        catch (const std::exception& e)
        {
            mDataFiles.clear();
            mStats.dataFileCount = 0;
            mStats.checksumCount = 0;
            mStats.loaded = false;
            mStats.lastError = e.what();
        }
    }

    void ServerContentRegistry::enrichFromOpenMwContentPlan(
        const std::vector<std::filesystem::path>& dataDirs, const std::vector<std::string>& contentFiles)
    {
        if (dataDirs.empty() || contentFiles.empty())
            return;

        const bool wasLoadedFromXml = mStats.loaded;
        mStats.enrichedFromOpenMwContentPlan = true;
        Files::Collections collections(dataDirs);

        for (const std::string& contentFile : contentFiles)
        {
            if (contentFile.empty() || isBuiltinContentFile(contentFile))
                continue;

            ++mStats.contentPlanFileCount;
            ServerDataFileRequirement* requirement = findRequirement(mDataFiles, contentFile);
            if (requirement == nullptr)
            {
                ServerDataFileRequirement addedRequirement;
                addedRequirement.name = contentFile;
                mDataFiles.push_back(std::move(addedRequirement));
                requirement = &mDataFiles.back();
            }

            try
            {
                const std::filesystem::path contentPath = collections.getPath(contentFile);
                const std::uint32_t checksum = Utils::crc32Checksum(contentPath.string());
                if (appendUniqueChecksum(requirement->checksums, Utils::intToHexStr(checksum)))
                    ++mStats.computedChecksumCount;
            }
            catch (const std::exception& e)
            {
                ++mStats.unresolvedContentFileCount;
                if (mStats.lastError.empty())
                    mStats.lastError = e.what();
            }
        }

        mStats.dataFileCount = mDataFiles.size();
        mStats.checksumCount = countChecksums(mDataFiles);
        if (!mDataFiles.empty())
        {
            mStats.loaded = true;
            if (wasLoadedFromXml && mStats.backend == "communitymp-server-data-files-xml")
                mStats.backend = "communitymp-server-data-files-xml+openmw-content-plan";
            else if (!wasLoadedFromXml)
                mStats.backend = "openmw-content-plan";

            if (!wasLoadedFromXml && mStats.unresolvedContentFileCount == 0)
                mStats.lastError.clear();
        }
    }

    const std::vector<ServerDataFileRequirement>& ServerContentRegistry::dataFiles() const
    {
        return mDataFiles;
    }

    const ServerContentRegistryStatistics& ServerContentRegistry::statistics() const
    {
        return mStats;
    }
}
