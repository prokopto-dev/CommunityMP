#include "ServerContentRegistry.hpp"

#include <components/files/collections.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/Utils.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace
{
    constexpr const char* dataFilesRelativePath = "saves/server/config/data-files.xml";
    constexpr const char* loadOrderRelativePath = "saves/server/config/load-order.cfg";
    constexpr const char* builtinOpenMwScripts = "builtin.omwscripts";
    constexpr const char* openMwContentVectorLoadOrderSource = "openmw-application-settings-content-vector";

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

    std::string trimAscii(std::string_view value)
    {
        std::size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
            ++begin;

        std::size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;

        return std::string(value.substr(begin, end - begin));
    }

    std::string stripMatchingQuotes(std::string value)
    {
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
                                     || (value.front() == '\'' && value.back() == '\'')))
            return value.substr(1, value.size() - 2);

        return value;
    }

    bool startsWithComment(std::string_view value)
    {
        return !value.empty() && (value.front() == '#' || value.front() == ';');
    }

    std::string parseLoadOrderLine(std::string_view line)
    {
        std::string trimmed = trimAscii(line);
        if (trimmed.empty() || startsWithComment(trimmed))
            return {};

        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos)
            return stripMatchingQuotes(trimAscii(trimmed));

        const std::string key = trimAscii(std::string_view(trimmed).substr(0, equals));
        if (!Misc::StringUtils::ciEqual(key, "content"))
            return {};

        return stripMatchingQuotes(trimAscii(std::string_view(trimmed).substr(equals + 1)));
    }

    std::vector<std::string> readServerLoadOrder(
        const std::filesystem::path& path, mwmp::ServerContentRegistryStatistics& stats)
    {
        stats.loadOrderPath = path;
        stats.loadOrderAttempted = true;

        if (!std::filesystem::is_regular_file(path))
            return {};

        std::ifstream stream(path);
        if (!stream.is_open())
            throw std::runtime_error("server load-order config could not be opened");

        std::vector<std::string> result;
        std::set<std::string, Misc::StringUtils::CiComp> seen;
        std::string line;
        while (std::getline(stream, line))
        {
            std::string contentFile = parseLoadOrderLine(line);
            if (contentFile.empty() || isBuiltinContentFile(contentFile))
                continue;

            ++stats.loadOrderEntryCount;
            if (!seen.insert(contentFile).second)
            {
                ++stats.loadOrderDuplicateCount;
                continue;
            }

            result.push_back(std::move(contentFile));
        }

        return result;
    }

    void applyServerLoadOrder(std::vector<mwmp::ServerDataFileRequirement>& dataFiles,
        const std::vector<std::string>& loadOrder, mwmp::ServerContentRegistryStatistics& stats)
    {
        if (dataFiles.empty() || loadOrder.empty())
            return;

        std::vector<mwmp::ServerDataFileRequirement> reordered;
        reordered.reserve(dataFiles.size());
        std::vector<bool> used(dataFiles.size(), false);

        for (const std::string& contentFile : loadOrder)
        {
            auto found = std::find_if(dataFiles.begin(), dataFiles.end(),
                [&](const mwmp::ServerDataFileRequirement& requirement) {
                    return Misc::StringUtils::ciEqual(requirement.name, contentFile);
                });

            if (found == dataFiles.end())
            {
                ++stats.loadOrderMissingRegistryCount;
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(std::distance(dataFiles.begin(), found));
            if (used[index])
                continue;

            used[index] = true;
            reordered.push_back(*found);
            ++stats.loadOrderAppliedCount;
        }

        for (std::size_t index = 0; index < dataFiles.size(); ++index)
        {
            if (used[index])
                continue;

            reordered.push_back(dataFiles[index]);
            ++stats.loadOrderMissingConfigCount;
        }

        if (stats.loadOrderAppliedCount != 0)
        {
            dataFiles = std::move(reordered);
            stats.loadOrderLoaded = true;
            stats.serverLoadOrderLoaded = true;
            stats.loadOrderSource = "server-config-load-order-cfg";
        }
    }

    void applyOpenMwContentPlanOrder(std::vector<mwmp::ServerDataFileRequirement>& dataFiles,
        const std::vector<std::string>& contentFiles, mwmp::ServerContentRegistryStatistics& stats)
    {
        if (dataFiles.empty() || contentFiles.empty())
            return;

        std::vector<mwmp::ServerDataFileRequirement> reordered;
        reordered.reserve(dataFiles.size());
        std::vector<bool> used(dataFiles.size(), false);
        std::set<std::string, Misc::StringUtils::CiComp> seen;
        std::size_t appliedCount = 0;

        for (const std::string& contentFile : contentFiles)
        {
            if (contentFile.empty() || isBuiltinContentFile(contentFile) || !seen.insert(contentFile).second)
                continue;

            auto found = std::find_if(dataFiles.begin(), dataFiles.end(),
                [&](const mwmp::ServerDataFileRequirement& requirement) {
                    return Misc::StringUtils::ciEqual(requirement.name, contentFile);
                });

            if (found == dataFiles.end())
                continue;

            const std::size_t index = static_cast<std::size_t>(std::distance(dataFiles.begin(), found));
            if (used[index])
                continue;

            used[index] = true;
            reordered.push_back(*found);
            ++appliedCount;
        }

        if (appliedCount == 0)
            return;

        for (std::size_t index = 0; index < dataFiles.size(); ++index)
        {
            if (!used[index])
                reordered.push_back(dataFiles[index]);
        }

        dataFiles = std::move(reordered);
        stats.loadOrderLoaded = true;
        stats.loadOrderSource = openMwContentVectorLoadOrderSource;
        stats.contentPlanOrderAppliedCount = appliedCount;
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
            const std::vector<std::string> loadOrder = readServerLoadOrder(dataDirectory / loadOrderRelativePath, mStats);
            applyServerLoadOrder(mDataFiles, loadOrder, mStats);
            mStats.dataFileCount = mDataFiles.size();
            mStats.loaded = true;
            if (mStats.loadOrderLoaded)
                mStats.backend += "+server-load-order-cfg";
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

        if (!mStats.loadOrderLoaded)
            applyOpenMwContentPlanOrder(mDataFiles, contentFiles, mStats);

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
