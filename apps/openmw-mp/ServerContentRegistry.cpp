#include "ServerContentRegistry.hpp"

#include <stdexcept>
#include <string>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace
{
    constexpr const char* dataFilesRelativePath = "saves/server/config/data-files.xml";

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

    const std::vector<ServerDataFileRequirement>& ServerContentRegistry::dataFiles() const
    {
        return mDataFiles;
    }

    const ServerContentRegistryStatistics& ServerContentRegistry::statistics() const
    {
        return mStats;
    }
}
