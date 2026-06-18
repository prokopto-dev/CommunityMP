#include "ServerContentDatabase.hpp"

#include <components/files/collections.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/algorithm.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
    constexpr const char* manifestSchema = "communitymp.worlddb.v1";
    constexpr const char* dataDirRowSchema = "communitymp.worlddb.data-dir.v1";
    constexpr const char* contentFileRowSchema = "communitymp.worlddb.content-file.v1";
    constexpr const char* builtinOpenMwScripts = "builtin.omwscripts";

    std::filesystem::path resolveDatabaseRoot()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_WORLDDB_DIR"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath);
        }

        return std::filesystem::current_path() / "server" / "data" / "worlddb";
    }

    std::string pathToLogString(const std::filesystem::path& path)
    {
        return Files::pathToUnicodeString(path);
    }

    bool isBuiltinContentFile(const std::string& contentFile)
    {
        return Misc::StringUtils::ciEqual(contentFile, builtinOpenMwScripts);
    }

    const mwmp::ServerDataFileRequirement* findRequirement(
        const std::vector<mwmp::ServerDataFileRequirement>& requirements, const std::string& name)
    {
        const auto found = std::find_if(requirements.begin(), requirements.end(),
            [&](const mwmp::ServerDataFileRequirement& requirement) {
                return Misc::StringUtils::ciEqual(requirement.name, name);
            });

        return found != requirements.end() ? &*found : nullptr;
    }

    void appendJsonString(std::string& result, std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";

        result.push_back('"');
        for (const unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        result += "\\u00";
                        result.push_back(hex[(c >> 4) & 0x0f]);
                        result.push_back(hex[c & 0x0f]);
                    }
                    else
                        result.push_back(static_cast<char>(c));
            }
        }
        result.push_back('"');
    }

    void appendJsonStringField(std::string& result, std::string_view name, std::string_view value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        appendJsonString(result, value);
    }

    template <class T>
    void appendJsonNumberField(std::string& result, std::string_view name, T value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        result += std::to_string(value);
    }

    void appendJsonBoolField(std::string& result, std::string_view name, bool value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        result += value ? "true" : "false";
    }

    void appendJsonStringArray(std::string& result, const std::vector<std::string>& values)
    {
        result.push_back('[');
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
                result.push_back(',');
            appendJsonString(result, values[i]);
        }
        result.push_back(']');
    }

    std::string readWholeFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::in | std::ios::binary);
        if (!stream.is_open())
            return {};

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    bool writeIfChanged(const std::filesystem::path& path, const std::string& contents)
    {
        if (readWholeFile(path) == contents)
            return false;

        if (const std::filesystem::path parent = path.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);

        std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            throw std::runtime_error("failed to open " + pathToLogString(path) + " for writing");

        stream << contents;
        if (!stream)
            throw std::runtime_error("failed to write " + pathToLogString(path));

        return true;
    }

    std::intmax_t fileSizeOrNegative(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
            return -1;

        return static_cast<std::intmax_t>(size);
    }

    long long fileTimeOrZero(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
        if (error)
            return 0;

        return static_cast<long long>(time.time_since_epoch().count());
    }

    std::string fileExtensionLower(std::string value)
    {
        const std::size_t dot = value.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= value.size())
            return {};

        value = value.substr(dot + 1);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::string buildManifestJson(const mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string result;
        result.reserve(800);
        result += "{\n  ";
        appendJsonStringField(result, "schema", manifestSchema);
        result += ",\n  ";
        appendJsonStringField(result, "backend", stats.backend);
        result += ",\n  \"tables\":[\"data_dirs.jsonl\",\"content_files.jsonl\"],\n  ";
        appendJsonNumberField(result, "dataDirCount", stats.dataDirCount);
        result += ",\n  ";
        appendJsonNumberField(result, "contentFileCount", stats.contentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "resolvedContentFileCount", stats.resolvedContentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "unresolvedContentFileCount", stats.unresolvedContentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "checksumCount", stats.checksumCount);
        result += "\n}\n";
        return result;
    }

    std::string buildDataDirsJsonl(const std::vector<std::filesystem::path>& dataDirs,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string result;
        result.reserve(dataDirs.size() * 160);
        for (std::size_t i = 0; i < dataDirs.size(); ++i)
        {
            ++stats.dataDirCount;
            result.push_back('{');
            appendJsonStringField(result, "schema", dataDirRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "index", i);
            result.push_back(',');
            appendJsonStringField(result, "path", pathToLogString(dataDirs[i]));
            result += "}\n";
        }
        return result;
    }

    std::string buildContentFilesJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles,
        const std::vector<mwmp::ServerDataFileRequirement>& dataFileRequirements,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        std::string result;
        result.reserve(contentFiles.size() * 300);

        std::size_t loadOrderIndex = 0;
        for (const std::string& contentFile : contentFiles)
        {
            if (contentFile.empty() || isBuiltinContentFile(contentFile))
                continue;

            ++stats.contentFileCount;
            const mwmp::ServerDataFileRequirement* requirement = findRequirement(dataFileRequirements, contentFile);
            const std::vector<std::string> checksums = requirement != nullptr ? requirement->checksums
                                                                              : std::vector<std::string>{};
            stats.checksumCount += checksums.size();

            std::filesystem::path resolvedPath;
            bool resolved = false;
            try
            {
                resolvedPath = collections.getPath(contentFile);
                resolved = true;
                ++stats.resolvedContentFileCount;
            }
            catch (const std::exception& e)
            {
                ++stats.unresolvedContentFileCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }

            result.push_back('{');
            appendJsonStringField(result, "schema", contentFileRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", loadOrderIndex++);
            result.push_back(',');
            appendJsonStringField(result, "name", contentFile);
            result.push_back(',');
            appendJsonStringField(result, "extension", fileExtensionLower(contentFile));
            result.push_back(',');
            appendJsonStringField(result, "resolvedPath", resolved ? pathToLogString(resolvedPath) : "");
            result.push_back(',');
            appendJsonBoolField(result, "resolved", resolved);
            result.push_back(',');
            appendJsonNumberField(result, "size", resolved ? fileSizeOrNegative(resolvedPath) : -1);
            result.push_back(',');
            appendJsonNumberField(result, "lastWriteTime", resolved ? fileTimeOrZero(resolvedPath) : 0);
            result += ",\"checksums\":";
            appendJsonStringArray(result, checksums);
            result += "}\n";
        }

        return result;
    }
}

namespace mwmp
{
    ServerContentDatabase& ServerContentDatabase::get()
    {
        static ServerContentDatabase database;
        return database;
    }

    void ServerContentDatabase::updateFromOpenMwContentPlan(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles,
        const std::vector<ServerDataFileRequirement>& dataFileRequirements)
    {
        std::lock_guard lock(mMutex);
        mStats = {};
        mStats.attempted = true;
        mStats.rootPath = resolveDatabaseRoot();
        mStats.manifestPath = mStats.rootPath / "manifest.json";
        mStats.tableCount = 2;

        try
        {
            ServerContentDatabaseStatistics newStats = mStats;
            const std::string dataDirsJsonl = buildDataDirsJsonl(dataDirs, newStats);
            const std::string contentFilesJsonl
                = buildContentFilesJsonl(dataDirs, contentFiles, dataFileRequirements, newStats);
            const std::string manifestJson = buildManifestJson(newStats);

            bool changed = false;
            changed = writeIfChanged(newStats.rootPath / "data_dirs.jsonl", dataDirsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "content_files.jsonl", contentFilesJsonl) || changed;
            changed = writeIfChanged(newStats.manifestPath, manifestJson) || changed;

            newStats.changed = changed;
            newStats.available = true;
            mStats = std::move(newStats);
        }
        catch (const std::exception& e)
        {
            mStats.available = false;
            mStats.lastError = e.what();
        }
    }

    ServerContentDatabaseStatistics ServerContentDatabase::statistics() const
    {
        std::lock_guard lock(mMutex);
        return mStats;
    }
}
