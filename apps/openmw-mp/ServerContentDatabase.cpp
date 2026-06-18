#include "ServerContentDatabase.hpp"

#include <components/esm/esmcommon.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/files/collections.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/toutf8/toutf8.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace
{
    constexpr const char* manifestSchema = "communitymp.worlddb.v1";
    constexpr const char* dataDirRowSchema = "communitymp.worlddb.data-dir.v1";
    constexpr const char* loadOrderRowSchema = "communitymp.worlddb.load-order.v1";
    constexpr const char* contentFileRowSchema = "communitymp.worlddb.content-file.v1";
    constexpr const char* recordIndexRowSchema = "communitymp.worlddb.record-index.v1";
    constexpr const char* questSourceRowSchema = "communitymp.quest.source.v1";
    constexpr const char* questDatabaseRowSchema = "communitymp.questdb.v1";
    constexpr const char* builtinOpenMwScripts = "builtin.omwscripts";
    constexpr const char* openMwContentVectorLoadOrderSource
        = "openmw-application-settings-content-vector";
    constexpr const char* laterContentEntryDominatesLoadOrderRule
        = "higher-engineContentIndex-overrides-lower-engineContentIndex-for-the-same-record-key";

    std::string fileExtensionLower(std::string value);

    std::filesystem::path resolveDatabaseRoot()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_WORLDDB_DIR"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath);
        }

        return std::filesystem::current_path() / "server" / "data" / "worlddb";
    }

    std::filesystem::path resolveGeneratedQuestDatabasePath()
    {
        if (const char* envPath = std::getenv("COMMUNITYMP_QUESTDB_DIR"))
        {
            if (*envPath != '\0')
                return std::filesystem::path(envPath) / "imported-content";
        }

        return std::filesystem::current_path() / "server" / "data" / "questdb" / "imported-content";
    }

    std::string pathToLogString(const std::filesystem::path& path)
    {
        return Files::pathToUnicodeString(path);
    }

    bool isBuiltinContentFile(const std::string& contentFile)
    {
        return Misc::StringUtils::ciEqual(contentFile, builtinOpenMwScripts);
    }

    bool isEsmLikeContentFile(const std::string& contentFile)
    {
        const std::string extension = fileExtensionLower(contentFile);
        return extension == "esm" || extension == "esp" || extension == "omwgame" || extension == "omwaddon";
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

    void appendJsonRawField(std::string& result, std::string_view name, std::string_view rawJson)
    {
        appendJsonString(result, name);
        result.push_back(':');
        result += rawJson;
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

    std::string makeQuestPackageId(const std::string& contentFile)
    {
        std::filesystem::path path(contentFile);
        std::string result = Files::pathToUnicodeString(path.stem());
        for (char& ch : result)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch))
                ch = static_cast<char>(std::tolower(uch));
            else if (ch != '-' && ch != '_')
                ch = '_';
        }

        return result.empty() ? "content" : result;
    }

    std::string refIdToString(const ESM::RefId& refId)
    {
        if (refId.empty())
            return {};

        return refId.toString();
    }

    std::string_view dialogTypeLabel(const int value)
    {
        switch (value)
        {
            case ESM::Dialogue::Topic:
                return "Topic";
            case ESM::Dialogue::Voice:
                return "Voice";
            case ESM::Dialogue::Greeting:
                return "Greeting";
            case ESM::Dialogue::Persuasion:
                return "Persuasion";
            case ESM::Dialogue::Journal:
                return "Journal";
            case ESM::Dialogue::Unknown:
                return "Deleted";
        }

        return "Invalid";
    }

    std::string_view questStatusLabel(const int value)
    {
        switch (value)
        {
            case ESM::DialInfo::QS_None:
                return "None";
            case ESM::DialInfo::QS_Name:
                return "Name";
            case ESM::DialInfo::QS_Finished:
                return "Finished";
            case ESM::DialInfo::QS_Restart:
                return "Restart";
        }

        return "Invalid";
    }

    std::string_view comparisonLabel(const ESM::DialogueCondition::Comparison comparison)
    {
        switch (comparison)
        {
            case ESM::DialogueCondition::Comp_Eq:
                return "eq";
            case ESM::DialogueCondition::Comp_Ne:
                return "ne";
            case ESM::DialogueCondition::Comp_Gt:
                return "gt";
            case ESM::DialogueCondition::Comp_Ge:
                return "ge";
            case ESM::DialogueCondition::Comp_Ls:
                return "lt";
            case ESM::DialogueCondition::Comp_Le:
                return "le";
            case ESM::DialogueCondition::Comp_None:
                return "none";
        }

        return "invalid";
    }

    std::string_view ruleFunctionLabel(const int value)
    {
        if (value >= ESM::DialogueCondition::Function_FacReactionLowest
            && value <= ESM::DialogueCondition::Function_PcWerewolfKills)
        {
            static constexpr std::string_view ruleFunctions[] = {
                "Lowest Faction Reaction",
                "Highest Faction Reaction",
                "Rank Requirement",
                "NPC Reputation",
                "Health Percent",
                "Player Reputation",
                "NPC Level",
                "Player Health Percent",
                "Player Magicka",
                "Player Fatigue",
                "Player Attribute Strength",
                "Player Skill Block",
                "Player Skill Armorer",
                "Player Skill Medium Armor",
                "Player Skill Heavy Armor",
                "Player Skill Blunt Weapon",
                "Player Skill Long Blade",
                "Player Skill Axe",
                "Player Skill Spear",
                "Player Skill Athletics",
                "Player Skill Enchant",
                "Player Skill Destruction",
                "Player Skill Alteration",
                "Player Skill Illusion",
                "Player Skill Conjuration",
                "Player Skill Mysticism",
                "Player Skill Restoration",
                "Player Skill Alchemy",
                "Player Skill Unarmored",
                "Player Skill Security",
                "Player Skill Sneak",
                "Player Skill Acrobatics",
                "Player Skill Light Armor",
                "Player Skill Short Blade",
                "Player Skill Marksman",
                "Player Skill Mercantile",
                "Player Skill Speechcraft",
                "Player Skill Hand to Hand",
                "Player Gender",
                "Player Expelled from Faction",
                "Player Diseased (Common)",
                "Player Diseased (Blight)",
                "Player Clothing Modifier",
                "Player Crime Level",
                "Player Same Sex",
                "Player Same Race",
                "Player Same Faction",
                "Faction Rank Difference",
                "Player Detected",
                "Alarmed",
                "Choice Selected",
                "Player Attribute Intelligence",
                "Player Attribute Willpower",
                "Player Attribute Agility",
                "Player Attribute Speed",
                "Player Attribute Endurance",
                "Player Attribute Personality",
                "Player Attribute Luck",
                "Player Diseased (Corprus)",
                "Weather",
                "Player is a Vampire",
                "Player Level",
                "Attacked",
                "NPC Talked to Player",
                "Player Health",
                "Creature Target",
                "Friend Hit",
                "Fight",
                "Hello",
                "Alarm",
                "Flee",
                "Should Attack",
                "Werewolf",
                "Werewolf Kills",
            };
            return ruleFunctions[value];
        }

        return "Invalid";
    }

    std::string buildManifestJson(const mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string result;
        result.reserve(800);
        result += "{\n  ";
        appendJsonStringField(result, "schema", manifestSchema);
        result += ",\n  ";
        appendJsonStringField(result, "backend", stats.backend);
        result += ",\n  ";
        appendJsonStringField(result, "loadOrderSource", stats.loadOrderSource);
        result += ",\n  ";
        appendJsonStringField(result, "loadOrderRule", stats.loadOrderRule);
        result += ",\n  \"tables\":[\"data_dirs.jsonl\",\"load_order.jsonl\",\"content_files.jsonl\",\"record_index.jsonl\",\"quest_sources.jsonl\"],\n  ";
        appendJsonNumberField(result, "dataDirCount", stats.dataDirCount);
        result += ",\n  ";
        appendJsonNumberField(result, "loadOrderEntryCount", stats.loadOrderEntryCount);
        result += ",\n  ";
        appendJsonNumberField(result, "contentFileCount", stats.contentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "esmLikeContentFileCount", stats.esmLikeContentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "resolvedContentFileCount", stats.resolvedContentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "unresolvedContentFileCount", stats.unresolvedContentFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "checksumCount", stats.checksumCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordIndexCount", stats.recordIndexCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordImportErrorCount", stats.recordImportErrorCount);
        result += ",\n  ";
        appendJsonNumberField(result, "questSourceRowCount", stats.questSourceRowCount);
        result += ",\n  ";
        appendJsonNumberField(result, "questSourcePackageCount", stats.questSourcePackageCount);
        result += ",\n  ";
        appendJsonNumberField(result, "questSourceDialogueCount", stats.questSourceDialogueCount);
        result += ",\n  ";
        appendJsonNumberField(result, "questSourceInfoCount", stats.questSourceInfoCount);
        result += ",\n  ";
        appendJsonNumberField(result, "questSourceImportErrorCount", stats.questSourceImportErrorCount);
        result += ",\n  ";
        appendJsonStringField(result, "generatedQuestDatabasePath", pathToLogString(stats.generatedQuestDatabasePath));
        result += ",\n  ";
        appendJsonNumberField(result, "generatedQuestDatabasePackageCount", stats.generatedQuestDatabasePackageCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedQuestDefinitionCount", stats.generatedQuestDefinitionCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedQuestStepCount", stats.generatedQuestStepCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedDialogueTopicCount", stats.generatedDialogueTopicCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedDialogueResponseCount", stats.generatedDialogueResponseCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedConditionCount", stats.generatedConditionCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedQuestEffectCount", stats.generatedQuestEffectCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedLegacyEffectCount", stats.generatedLegacyEffectCount);
        result += ",\n  ";
        appendJsonNumberField(result, "generatedQuestDatabaseImportErrorCount",
            stats.generatedQuestDatabaseImportErrorCount);
        result += "\n}\n";
        return result;
    }

    std::string buildLoadOrderJsonl(const std::vector<std::string>& contentFiles,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string result;
        result.reserve(contentFiles.size() * 220);

        std::size_t dominanceRank = 0;
        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
            if (contentFile.empty())
                continue;

            const bool builtin = isBuiltinContentFile(contentFile);
            const bool esmLike = isEsmLikeContentFile(contentFile);
            const long long currentDominanceRank = !builtin && esmLike ? static_cast<long long>(dominanceRank++) : -1;

            ++stats.loadOrderEntryCount;
            if (esmLike)
                ++stats.esmLikeContentFileCount;

            result.push_back('{');
            appendJsonStringField(result, "schema", loadOrderRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
            result.push_back(',');
            appendJsonStringField(result, "contentFile", contentFile);
            result.push_back(',');
            appendJsonStringField(result, "extension", fileExtensionLower(contentFile));
            result.push_back(',');
            appendJsonBoolField(result, "builtin", builtin);
            result.push_back(',');
            appendJsonBoolField(result, "esmLike", esmLike);
            result.push_back(',');
            appendJsonNumberField(result, "dominanceRank", currentDominanceRank);
            result += "}\n";
        }

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

        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
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
            appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
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

    void appendRecordIndexRow(std::string& result, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::size_t recordIndex, const ESM::NAME recordName,
        const std::uint32_t flags, const std::size_t recordOffset, const std::size_t dataOffset,
        const std::size_t dataSize)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", recordIndexRowSchema);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", recordIndex);
        result.push_back(',');
        appendJsonStringField(result, "recordType", recordName.toStringView());
        result.push_back(',');
        appendJsonNumberField(result, "recordTypeInt", recordName.toInt());
        result.push_back(',');
        appendJsonNumberField(result, "flags", flags);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", (flags & ESM::FLAG_Deleted) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "ignored", (flags & ESM::FLAG_Ignored) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "persistent", (flags & ESM::FLAG_Persistent) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "blocked", (flags & ESM::FLAG_Blocked) != 0);
        result.push_back(',');
        appendJsonNumberField(result, "recordOffset", recordOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataOffset", dataOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataSize", dataSize);
        result += "}\n";
    }

    std::string buildRecordIndexJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles, mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        std::string result;
        result.reserve(contentFiles.size() * 2048);

        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
            if (contentFile.empty() || isBuiltinContentFile(contentFile))
                continue;

            if (!isEsmLikeContentFile(contentFile))
                continue;

            try
            {
                ESM::ESMReader esm;
                esm.setIndex(static_cast<int>(engineContentIndex));
                esm.open(collections.getPath(contentFile));

                std::size_t recordIndex = 1;
                while (esm.hasMoreRecs())
                {
                    const std::size_t recordOffset = esm.getFileOffset();
                    const ESM::NAME recordName = esm.getRecName();
                    std::uint32_t flags = 0;
                    esm.getRecHeader(flags);
                    const std::size_t dataOffset = esm.getFileOffset();
                    esm.skipRecord();
                    const std::size_t nextRecordOffset = esm.getFileOffset();
                    const std::size_t dataSize = nextRecordOffset >= dataOffset ? nextRecordOffset - dataOffset : 0;

                    appendRecordIndexRow(result, engineContentIndex, contentFile, recordIndex++, recordName, flags,
                        recordOffset, dataOffset, dataSize);
                    ++stats.recordIndexCount;
                }
            }
            catch (const std::exception& e)
            {
                ++stats.recordImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
        }

        return result;
    }

    void appendQuestSourcePackageRow(std::string& result, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::filesystem::path& resolvedPath, ESM::ESMReader& esm,
        const std::string& packageId, mwmp::ServerContentDatabaseStatistics& stats)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", questSourceRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "kind", "package");
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", pathToLogString(resolvedPath));
        result.push_back(',');
        appendJsonStringField(result, "sourceFileName", contentFile);
        result.push_back(',');
        appendJsonStringField(result, "author", esm.getAuthor());
        result.push_back(',');
        appendJsonStringField(result, "description", esm.getDesc());
        result.push_back(',');
        appendJsonNumberField(result, "esmVersion", esm.esmVersionF());
        result.push_back(',');
        appendJsonNumberField(result, "recordCount", esm.getRecordCount());
        result += ",\"masters\":[";

        bool firstMaster = true;
        for (const ESM::Header::MasterData& master : esm.getGameFiles())
        {
            if (!firstMaster)
                result.push_back(',');
            firstMaster = false;

            result.push_back('{');
            appendJsonStringField(result, "name", master.name);
            result.push_back(',');
            appendJsonNumberField(result, "size", master.size);
            result.push_back('}');
        }

        result += "]}\n";
        ++stats.questSourceRowCount;
        ++stats.questSourcePackageCount;
    }

    void appendQuestSourceDialogueRow(std::string& result, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::string& packageId, const std::size_t recordIndex,
        const std::uint32_t flags, const bool isDeleted, const ESM::Dialogue& dialogue,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", questSourceRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "kind", "dialogue");
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFileName", contentFile);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", recordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "recordFlags", flags);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", isDeleted);
        result.push_back(',');
        appendJsonStringField(result, "dialogueId", refIdToString(dialogue.mId));
        result.push_back(',');
        appendJsonStringField(result, "displayName", dialogue.mStringId);
        result.push_back(',');
        appendJsonNumberField(result, "dialogueTypeCode", static_cast<int>(dialogue.mType));
        result.push_back(',');
        appendJsonStringField(result, "dialogueType", dialogTypeLabel(static_cast<int>(dialogue.mType)));
        result += "}\n";
        ++stats.questSourceRowCount;
        ++stats.questSourceDialogueCount;
    }

    std::string conditionValueJson(const ESM::DialogueCondition& condition)
    {
        return std::visit(
            [](auto value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, float>)
                {
                    std::ostringstream stream;
                    stream << std::setprecision(9) << value;
                    return stream.str();
                }
                else
                    return std::to_string(value);
            },
            condition.mValue);
    }

    void appendQuestSourceCondition(std::string& result, const ESM::DialogueCondition& condition)
    {
        result.push_back('{');
        appendJsonNumberField(result, "index", static_cast<int>(condition.mIndex));
        result.push_back(',');
        appendJsonNumberField(result, "functionCode", static_cast<int>(condition.mFunction));
        result.push_back(',');
        appendJsonStringField(result, "function", ruleFunctionLabel(static_cast<int>(condition.mFunction)));
        result.push_back(',');
        appendJsonNumberField(result, "comparisonCode", static_cast<int>(condition.mComparison));
        result.push_back(',');
        appendJsonStringField(result, "comparison", comparisonLabel(condition.mComparison));
        result.push_back(',');
        appendJsonStringField(result, "variable", condition.mVariable);
        result.push_back(',');
        appendJsonStringField(result, "valueType", std::holds_alternative<float>(condition.mValue) ? "float" : "int");
        result.push_back(',');
        appendJsonRawField(result, "value", conditionValueJson(condition));
        result.push_back('}');
    }

    void appendQuestSourceInfoRow(std::string& result, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::string& packageId, const std::size_t recordIndex,
        const std::uint32_t flags, const bool isDeleted, const bool hasDialogue, const ESM::Dialogue& dialogue,
        const ESM::DialInfo& info, const std::size_t infoOrder, mwmp::ServerContentDatabaseStatistics& stats)
    {
        const bool isJournal = hasDialogue && dialogue.mType == ESM::Dialogue::Journal;
        const std::string dialogueId = hasDialogue ? refIdToString(dialogue.mId) : std::string{};

        result.push_back('{');
        appendJsonStringField(result, "schema", questSourceRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "kind", "info");
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFileName", contentFile);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", recordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "recordFlags", flags);
        result.push_back(',');
        appendJsonNumberField(result, "infoOrder", infoOrder);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", isDeleted);
        result.push_back(',');
        appendJsonBoolField(result, "orphaned", !hasDialogue);
        result.push_back(',');
        appendJsonStringField(result, "dialogueId", dialogueId);
        result.push_back(',');
        appendJsonStringField(result, "dialogueType", hasDialogue ? dialogTypeLabel(static_cast<int>(dialogue.mType)) : "");
        result.push_back(',');
        appendJsonNumberField(result, "dialogueTypeCode", hasDialogue ? static_cast<int>(dialogue.mType) : -1);
        result.push_back(',');
        appendJsonStringField(result, "infoId", refIdToString(info.mId));
        result.push_back(',');
        appendJsonStringField(result, "previousInfoId", refIdToString(info.mPrev));
        result.push_back(',');
        appendJsonStringField(result, "nextInfoId", refIdToString(info.mNext));
        result.push_back(',');
        appendJsonNumberField(result, "dataTypeCode", info.mData.mType);
        result.push_back(',');
        appendJsonNumberField(result, "rank", static_cast<int>(info.mData.mRank));
        result.push_back(',');
        appendJsonNumberField(result, "gender", static_cast<int>(info.mData.mGender));
        result.push_back(',');
        appendJsonNumberField(result, "pcRank", static_cast<int>(info.mData.mPCrank));
        result.push_back(',');
        appendJsonStringField(result, "dataValueKind", isJournal ? "journalIndex" : "disposition");
        result.push_back(',');
        appendJsonNumberField(result, "dataValue", isJournal ? info.mData.mJournalIndex : info.mData.mDisposition);
        result.push_back(',');
        appendJsonStringField(result, "actor", refIdToString(info.mActor));
        result.push_back(',');
        appendJsonStringField(result, "race", refIdToString(info.mRace));
        result.push_back(',');
        appendJsonStringField(result, "class", refIdToString(info.mClass));
        result.push_back(',');
        appendJsonStringField(result, "faction", refIdToString(info.mFaction));
        result.push_back(',');
        appendJsonStringField(result, "pcFaction", refIdToString(info.mPcFaction));
        result.push_back(',');
        appendJsonStringField(result, "cell", refIdToString(info.mCell));
        result.push_back(',');
        appendJsonBoolField(result, "factionLess", info.mFactionLess);
        result.push_back(',');
        appendJsonStringField(result, "sound", info.mSound);
        result.push_back(',');
        appendJsonStringField(result, "response", info.mResponse);
        result.push_back(',');
        appendJsonStringField(result, "resultScript", info.mResultScript);
        result.push_back(',');
        appendJsonNumberField(result, "questStatusCode", static_cast<int>(info.mQuestStatus));
        result.push_back(',');
        appendJsonStringField(result, "questStatus", questStatusLabel(static_cast<int>(info.mQuestStatus)));
        result += ",\"conditions\":[";

        bool firstCondition = true;
        for (const ESM::DialogueCondition& condition : info.mSelects)
        {
            if (!firstCondition)
                result.push_back(',');
            firstCondition = false;
            appendQuestSourceCondition(result, condition);
        }

        result += "]}\n";
        ++stats.questSourceRowCount;
        ++stats.questSourceInfoCount;
    }

    struct GeneratedQuestDbTables
    {
        std::string packages;
        std::string questDefinitions;
        std::string questSteps;
        std::string dialogueTopics;
        std::string dialogueResponses;
        std::string conditions;
        std::string questEffects;
        std::string legacyEffects;

        std::set<std::string> seenPackages;
        std::set<std::string> seenQuestDefinitions;
        std::set<std::string> seenDialogueTopics;
        std::map<std::string, std::string> questIdByDialogueKey;
        std::map<std::string, std::string> topicIdByDialogueKey;
    };

    std::string normalizedLookupKey(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    std::string trimmed(std::string_view value)
    {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
            ++first;

        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
            --last;

        return std::string(value.substr(first, last - first));
    }

    std::uint64_t stableHash(std::string_view value)
    {
        std::uint64_t result = 14695981039346656037ull;
        for (const unsigned char ch : value)
        {
            result ^= ch;
            result *= 1099511628211ull;
        }
        return result;
    }

    std::string stableKey(std::initializer_list<std::string_view> parts)
    {
        std::string raw;
        bool first = true;
        for (std::string_view part : parts)
        {
            if (!first)
                raw.push_back('|');
            first = false;
            raw += part;
        }

        std::string slug;
        slug.reserve(raw.size());
        for (const unsigned char ch : raw)
        {
            if (std::isalnum(ch))
                slug.push_back(static_cast<char>(std::tolower(ch)));
            else if (ch == '_' || ch == '.' || ch == '-')
                slug.push_back(static_cast<char>(ch));
            else
                slug.push_back('_');
        }

        while (!slug.empty() && slug.front() == '_')
            slug.erase(slug.begin());
        while (!slug.empty() && slug.back() == '_')
            slug.pop_back();
        if (slug.empty())
            slug = "row";
        if (slug.size() > 96)
            slug.resize(96);

        std::ostringstream suffix;
        suffix << std::hex << std::setfill('0') << std::setw(12) << (stableHash(raw) & 0xffffffffffffull);
        return slug + "_" + suffix.str();
    }

    std::string dialogueKey(std::string_view packageId, std::string_view dialogueId, std::string_view recordIndex)
    {
        return std::string(packageId) + "\x1f" + std::string(dialogueId) + "\x1f" + std::string(recordIndex);
    }

    std::string inferCompletionPolicy(const ESM::DialInfo& info)
    {
        switch (info.mQuestStatus)
        {
            case ESM::DialInfo::QS_Finished:
                return "complete-quest";
            case ESM::DialInfo::QS_Restart:
                return "restartable-step";
            case ESM::DialInfo::QS_Name:
                return "quest-title";
            case ESM::DialInfo::QS_None:
                break;
        }
        return "advance-step";
    }

    void appendQuestDbSourceObject(std::string& result, const std::string& packageId,
        const std::string& dialogueId, std::string_view dialogueType, const std::string& infoId,
        const std::size_t recordIndex)
    {
        result += ",\"source\":{";
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonStringField(result, "dialogueId", dialogueId);
        result.push_back(',');
        appendJsonStringField(result, "dialogueType", dialogueType);
        result.push_back(',');
        appendJsonStringField(result, "infoId", infoId);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", recordIndex);
        result.push_back('}');
    }

    void appendGeneratedQuestDbPackageRow(GeneratedQuestDbTables& tables, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::filesystem::path& resolvedPath, ESM::ESMReader& esm,
        const std::string& packageId, mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!tables.seenPackages.insert(packageId).second)
            return;

        std::string& result = tables.packages;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", pathToLogString(resolvedPath));
        result.push_back(',');
        appendJsonStringField(result, "sourceFileName", contentFile);
        result.push_back(',');
        appendJsonStringField(result, "author", esm.getAuthor());
        result.push_back(',');
        appendJsonStringField(result, "description", esm.getDesc());
        result += ",\"masters\":[";

        bool firstMaster = true;
        for (const ESM::Header::MasterData& master : esm.getGameFiles())
        {
            if (!firstMaster)
                result.push_back(',');
            firstMaster = false;
            result.push_back('{');
            appendJsonStringField(result, "name", master.name);
            result.push_back(',');
            appendJsonNumberField(result, "size", master.size);
            result.push_back('}');
        }

        result += "],";
        appendJsonStringField(result, "importPolicy", "content-source-only");
        result.push_back(',');
        appendJsonStringField(result, "runtimeFormat", questDatabaseRowSchema);
        result += "}\n";
        ++stats.generatedQuestDatabasePackageCount;
    }

    std::string getOrCreateGeneratedQuestId(GeneratedQuestDbTables& tables, const std::size_t engineContentIndex,
        const std::string& packageId, const std::string& dialogueId, const std::string& displayName,
        const std::size_t recordIndex, const bool deleted, mwmp::ServerContentDatabaseStatistics& stats)
    {
        const std::string recordIndexText = std::to_string(recordIndex);
        const std::string key = dialogueKey(packageId, dialogueId, recordIndexText);
        if (const auto found = tables.questIdByDialogueKey.find(key); found != tables.questIdByDialogueKey.end())
            return found->second;

        const std::string questId = stableKey({ packageId, "quest", dialogueId.empty() ? recordIndexText : dialogueId });
        tables.questIdByDialogueKey[key] = questId;
        if (!tables.seenQuestDefinitions.insert(questId).second)
            return questId;

        std::string& result = tables.questDefinitions;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "questId", questId);
        result.push_back(',');
        appendJsonStringField(result, "sourceQuestId", dialogueId);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "title", displayName.empty() ? dialogueId : displayName);
        result.push_back(',');
        appendJsonStringField(result, "scopePolicy", "player-default");
        result.push_back(',');
        appendJsonStringField(result, "sharingPolicy", "explicit-party-or-world-event");
        result.push_back(',');
        appendJsonStringField(result, "repeatPolicy", "imported-legacy-default");
        result.push_back(',');
        appendJsonStringField(result, "instancingPolicy", "server-owned-with-player-overrides");
        result.push_back(',');
        appendJsonStringField(result, "runtimeModel", "server-owned-multiplayer-quest-v1");
        result.push_back(',');
        appendJsonStringField(result, "authorityPolicy", "server-owned");
        result.push_back(',');
        appendJsonStringField(result, "stateScope", "player-default");
        result.push_back(',');
        appendJsonStringField(result, "transactionPolicy", "quest-compare-and-swap");
        result.push_back(',');
        appendJsonBoolField(result, "deleted", deleted);
        appendQuestDbSourceObject(result, packageId, dialogueId, "Journal", "", recordIndex);
        result += "}\n";
        ++stats.generatedQuestDefinitionCount;
        return questId;
    }

    std::string getOrCreateGeneratedTopicId(GeneratedQuestDbTables& tables, const std::size_t engineContentIndex,
        const std::string& packageId, const std::string& dialogueId, std::string_view dialogueType,
        const std::string& displayName, const std::size_t recordIndex, const bool deleted,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        const std::string recordIndexText = std::to_string(recordIndex);
        const std::string key = dialogueKey(packageId, dialogueId, recordIndexText);
        if (const auto found = tables.topicIdByDialogueKey.find(key); found != tables.topicIdByDialogueKey.end())
            return found->second;

        const std::string topicId = stableKey(
            { packageId, "dialogue", dialogueType, dialogueId.empty() ? recordIndexText : dialogueId });
        tables.topicIdByDialogueKey[key] = topicId;
        if (!tables.seenDialogueTopics.insert(topicId).second)
            return topicId;

        std::string& result = tables.dialogueTopics;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "topicId", topicId);
        result.push_back(',');
        appendJsonStringField(result, "sourceTopicId", dialogueId);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "dialogueType", dialogueType);
        result.push_back(',');
        appendJsonStringField(result, "displayName", displayName.empty() ? dialogueId : displayName);
        result.push_back(',');
        appendJsonStringField(result, "visibilityPolicy", "server-filtered-per-player");
        result.push_back(',');
        appendJsonStringField(result, "authorityPolicy", "server-filtered");
        result.push_back(',');
        appendJsonBoolField(result, "deleted", deleted);
        appendQuestDbSourceObject(result, packageId, dialogueId, dialogueType, "", recordIndex);
        result += "}\n";
        ++stats.generatedDialogueTopicCount;
        return topicId;
    }

    std::string inferConditionScope(const ESM::DialogueCondition& condition)
    {
        const std::string function = normalizedLookupKey(ruleFunctionLabel(static_cast<int>(condition.mFunction)));
        if (condition.mFunction == ESM::DialogueCondition::Function_Journal || function.starts_with("player"))
            return "player";
        if (condition.mFunction == ESM::DialogueCondition::Function_Global
            || condition.mFunction == ESM::DialogueCondition::Function_Dead)
            return "world";
        if (condition.mFunction == ESM::DialogueCondition::Function_Local
            || condition.mFunction == ESM::DialogueCondition::Function_NotLocal)
            return "actor";
        if (condition.mFunction == ESM::DialogueCondition::Function_NotId
            || condition.mFunction == ESM::DialogueCondition::Function_NotFaction
            || condition.mFunction == ESM::DialogueCondition::Function_NotClass
            || condition.mFunction == ESM::DialogueCondition::Function_NotRace
            || condition.mFunction == ESM::DialogueCondition::Function_NotCell)
            return "actor-filter";
        return "encounter";
    }

    void appendConditionAuthorityFields(std::string& result, const std::string& scope)
    {
        result.push_back(',');
        appendJsonStringField(result, "evaluationScope", scope);
        result.push_back(',');
        appendJsonStringField(result, "stateScope", scope);
        result.push_back(',');
        if (scope == "player")
        {
            appendJsonStringField(result, "authorityRequirement", "server-player-state");
            result.push_back(',');
            appendJsonStringField(result, "snapshotPolicy", "read-player-quest-inventory-snapshot");
        }
        else if (scope == "world")
        {
            appendJsonStringField(result, "authorityRequirement", "server-world-state");
            result.push_back(',');
            appendJsonStringField(result, "snapshotPolicy", "read-world-event-snapshot");
        }
        else if (scope == "actor" || scope == "actor-filter")
        {
            appendJsonStringField(result, "authorityRequirement", "cell-simulation-owner");
            result.push_back(',');
            appendJsonStringField(result, "snapshotPolicy", "read-actor-cell-snapshot");
        }
        else
        {
            appendJsonStringField(result, "authorityRequirement", "server-encounter-context");
            result.push_back(',');
            appendJsonStringField(result, "snapshotPolicy", "read-dialogue-encounter-snapshot");
        }
    }

    void appendGeneratedConditionRow(GeneratedQuestDbTables& tables, std::string_view ownerKind,
        const std::string& ownerId, const ESM::DialogueCondition& condition, const std::size_t order,
        const std::string& packageId, const std::string& dialogueId, std::string_view dialogueType,
        const std::string& infoId, const std::size_t recordIndex, mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string& result = tables.conditions;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "conditionId", stableKey({ ownerId, "condition", std::to_string(order) }));
        result.push_back(',');
        appendJsonStringField(result, "ownerKind", ownerKind);
        result.push_back(',');
        appendJsonStringField(result, "ownerId", ownerId);
        result.push_back(',');
        appendJsonNumberField(result, "order", order);
        result.push_back(',');
        appendJsonNumberField(result, "functionCode", static_cast<int>(condition.mFunction));
        result.push_back(',');
        appendJsonStringField(result, "function", ruleFunctionLabel(static_cast<int>(condition.mFunction)));
        result.push_back(',');
        appendJsonNumberField(result, "comparisonCode", static_cast<int>(condition.mComparison));
        result.push_back(',');
        appendJsonStringField(result, "comparison", comparisonLabel(condition.mComparison));
        result.push_back(',');
        appendJsonStringField(result, "variable", condition.mVariable);
        result.push_back(',');
        appendJsonStringField(result, "valueType", std::holds_alternative<float>(condition.mValue) ? "float" : "int");
        result.push_back(',');
        appendJsonRawField(result, "value", conditionValueJson(condition));
        appendConditionAuthorityFields(result, inferConditionScope(condition));
        appendQuestDbSourceObject(result, packageId, dialogueId, dialogueType, infoId, recordIndex);
        result += "}\n";
        ++stats.generatedConditionCount;
    }

    std::vector<std::pair<int, std::string>> splitScriptLines(std::string_view script)
    {
        std::vector<std::pair<int, std::string>> result;
        std::istringstream stream{ std::string(script) };
        std::string line;
        int lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            const std::string command = trimmed(line);
            if (!command.empty() && !command.starts_with(";"))
                result.emplace_back(lineNumber, command);
        }
        return result;
    }

    std::string stripQuotes(std::string value)
    {
        value = trimmed(value);
        if (value.size() >= 2 && value.front() == value.back() && (value.front() == '"' || value.front() == '\''))
            return value.substr(1, value.size() - 2);
        return value;
    }

    std::pair<std::string, std::string> splitTarget(std::string_view command)
    {
        const std::string text(command);
        const std::size_t arrow = text.find("->");
        if (arrow == std::string::npos)
            return { "", trimmed(command) };
        return { stripQuotes(text.substr(0, arrow)), trimmed(std::string_view(text).substr(arrow + 2)) };
    }

    std::vector<std::string> tokenizeCommand(std::string_view command)
    {
        std::vector<std::string> result;
        std::string current;
        char quote = '\0';
        for (const char ch : command)
        {
            if (quote != '\0')
            {
                if (ch == quote)
                    quote = '\0';
                else
                    current.push_back(ch);
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                quote = ch;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                if (!current.empty())
                {
                    result.push_back(current);
                    current.clear();
                }
                continue;
            }

            current.push_back(ch);
        }
        if (!current.empty())
            result.push_back(current);
        return result;
    }

    bool parseInt(std::string_view value, int& result)
    {
        const std::string text = trimmed(value);
        if (text.empty())
            return false;
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0')
            return false;
        result = static_cast<int>(parsed);
        return true;
    }

    struct ParsedQuestEffect
    {
        std::string effectKind = "unsupported";
        std::string rawCommand;
        std::string target;
        std::string targetKind = "dialogue-actor";
        std::string quest;
        std::string topic;
        std::string item;
        std::string combatTarget;
        int index = 0;
        int count = 0;
        int value = 0;
        int choiceCount = 0;
    };

    std::string targetKindForTarget(const std::string& target)
    {
        if (target.empty())
            return "dialogue-actor";
        if (normalizedLookupKey(target) == "player")
            return "player";
        return "actor";
    }

    ParsedQuestEffect parseResultCommand(const std::string& command)
    {
        ParsedQuestEffect effect;
        effect.rawCommand = command;
        const auto [target, body] = splitTarget(command);
        effect.target = target;
        effect.targetKind = targetKindForTarget(target);
        const std::vector<std::string> tokens = tokenizeCommand(body);
        if (tokens.empty())
            return effect;

        const std::string verb = normalizedLookupKey(tokens.front());
        if ((verb == "journal" || verb == "setjournalindex") && tokens.size() >= 3)
        {
            effect.effectKind = "journal.set";
            effect.quest = tokens[1];
            parseInt(tokens[2], effect.index);
        }
        else if (verb == "addtopic" && tokens.size() >= 2)
        {
            effect.effectKind = "topic.add";
            effect.topic = tokens[1];
        }
        else if ((verb == "additem" || verb == "removeitem") && tokens.size() >= 3)
        {
            effect.effectKind = verb == "additem" ? "inventory.add" : "inventory.remove";
            effect.item = tokens[1];
            parseInt(tokens[2], effect.count);
        }
        else if (verb == "goodbye")
            effect.effectKind = "dialogue.goodbye";
        else if (verb == "choice")
        {
            effect.effectKind = "dialogue.choice";
            effect.choiceCount = static_cast<int>((tokens.size() - 1) / 2);
        }
        else if (verb == "setfight" && tokens.size() >= 2)
        {
            effect.effectKind = "actor.setFight";
            parseInt(tokens[1], effect.value);
        }
        else if (verb == "startcombat" && tokens.size() >= 2)
        {
            effect.effectKind = "actor.startCombat";
            effect.combatTarget = tokens[1];
        }
        else if (verb == "moddisposition" && tokens.size() >= 2)
        {
            effect.effectKind = "actor.modDisposition";
            parseInt(tokens[1], effect.value);
        }

        return effect;
    }

    void appendEffectRuntimeMetadata(std::string& result, const std::string& effectKind)
    {
        if (effectKind.starts_with("journal."))
        {
            appendJsonStringField(result, "executionPolicy", "server-executable");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "player-quest");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "quest-state");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "server-quest-state");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "monotonic-journal-index");
        }
        else if (effectKind.starts_with("topic."))
        {
            appendJsonStringField(result, "executionPolicy", "server-executable");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "player-dialogue");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "topic-state");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "server-topic-state");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "set-union");
        }
        else if (effectKind.starts_with("dialogue."))
        {
            appendJsonStringField(result, "executionPolicy", "server-executable");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "dialogue-session");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "dialogue-session");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "server-dialogue-session");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "session-ordered");
        }
        else if (effectKind.starts_with("inventory."))
        {
            appendJsonStringField(result, "executionPolicy", "inventory-transaction-required");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "player-or-actor-inventory");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "inventory");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "server-inventory-ledger");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "transactional-compare-and-swap");
        }
        else if (effectKind.starts_with("actor."))
        {
            appendJsonStringField(result, "executionPolicy", "actor-authority-required");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "cell-actor");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "actor-cell");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "cell-simulation-owner");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "cell-authority-sequence");
        }
        else
        {
            appendJsonStringField(result, "executionPolicy", "server-review-required");
            result.push_back(',');
            appendJsonStringField(result, "stateScope", "unknown");
            result.push_back(',');
            appendJsonStringField(result, "transactionKind", "manual-review");
            result.push_back(',');
            appendJsonStringField(result, "authorityRequirement", "server-review");
            result.push_back(',');
            appendJsonStringField(result, "conflictPolicy", "manual-review");
        }
    }

    void appendGeneratedQuestEffectRow(GeneratedQuestDbTables& tables, std::string_view ownerKind,
        const std::string& ownerId, const ParsedQuestEffect& effect, const std::size_t order, const int sourceLine,
        const std::string& packageId, const std::string& dialogueId, std::string_view dialogueType,
        const std::string& infoId, const std::size_t recordIndex, mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string& result = tables.questEffects;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "effectId", stableKey({ ownerId, "effect", std::to_string(order), effect.rawCommand }));
        result.push_back(',');
        appendJsonStringField(result, "ownerKind", ownerKind);
        result.push_back(',');
        appendJsonStringField(result, "ownerId", ownerId);
        result.push_back(',');
        appendJsonNumberField(result, "order", order);
        result.push_back(',');
        appendJsonNumberField(result, "sourceLine", sourceLine);
        result.push_back(',');
        appendJsonStringField(result, "effectKind", effect.effectKind);
        result.push_back(',');
        appendJsonStringField(result, "rawCommand", effect.rawCommand);
        result.push_back(',');
        appendJsonStringField(result, "target", effect.target);
        result.push_back(',');
        appendJsonStringField(result, "targetKind", effect.targetKind);
        result.push_back(',');
        appendJsonStringField(result, "quest", effect.quest);
        result.push_back(',');
        appendJsonStringField(result, "topic", effect.topic);
        result.push_back(',');
        appendJsonStringField(result, "item", effect.item);
        result.push_back(',');
        appendJsonStringField(result, "combatTarget", effect.combatTarget);
        result.push_back(',');
        appendJsonNumberField(result, "index", effect.index);
        result.push_back(',');
        appendJsonNumberField(result, "count", effect.count);
        result.push_back(',');
        appendJsonNumberField(result, "value", effect.value);
        result.push_back(',');
        appendJsonNumberField(result, "choiceCount", effect.choiceCount);
        result.push_back(',');
        appendEffectRuntimeMetadata(result, effect.effectKind);
        result.push_back(',');
        appendJsonStringField(result, "idempotencyKey", stableKey({ ownerId, "effect-idempotency",
                                                        std::to_string(order), effect.effectKind, effect.quest,
                                                        effect.topic, effect.item, effect.rawCommand }));
        appendQuestDbSourceObject(result, packageId, dialogueId, dialogueType, infoId, recordIndex);
        result += "}\n";
        ++stats.generatedQuestEffectCount;
    }

    void appendGeneratedLegacyEffectRow(GeneratedQuestDbTables& tables, std::string_view ownerKind,
        const std::string& ownerId, const ESM::DialInfo& info, const std::string& packageId,
        const std::string& dialogueId, std::string_view dialogueType, const std::string& infoId,
        const std::size_t recordIndex, mwmp::ServerContentDatabaseStatistics& stats)
    {
        std::string& result = tables.legacyEffects;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "effectId", stableKey({ ownerId, "legacy-script" }));
        result.push_back(',');
        appendJsonStringField(result, "ownerKind", ownerKind);
        result.push_back(',');
        appendJsonStringField(result, "ownerId", ownerId);
        result.push_back(',');
        appendJsonStringField(result, "effectKind", "legacy-mwscript");
        result.push_back(',');
        appendJsonStringField(result, "executionPolicy", "server-review-required");
        result.push_back(',');
        appendJsonStringField(result, "script", info.mResultScript);
        appendQuestDbSourceObject(result, packageId, dialogueId, dialogueType, infoId, recordIndex);
        result += "}\n";
        ++stats.generatedLegacyEffectCount;
    }

    void appendGeneratedEffects(GeneratedQuestDbTables& tables, std::string_view ownerKind,
        const std::string& ownerId, const ESM::DialInfo& info, const std::string& packageId,
        const std::string& dialogueId, std::string_view dialogueType, const std::string& infoId,
        const std::size_t recordIndex, mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (trimmed(info.mResultScript).empty())
            return;

        appendGeneratedLegacyEffectRow(tables, ownerKind, ownerId, info, packageId, dialogueId, dialogueType, infoId,
            recordIndex, stats);

        std::size_t order = 0;
        for (const auto& [lineNumber, command] : splitScriptLines(info.mResultScript))
            appendGeneratedQuestEffectRow(tables, ownerKind, ownerId, parseResultCommand(command), order++,
                lineNumber, packageId, dialogueId, dialogueType, infoId, recordIndex, stats);
    }

    void appendGeneratedQuestStep(GeneratedQuestDbTables& tables, const std::size_t engineContentIndex,
        const std::string& packageId, const ESM::Dialogue& dialogue, const ESM::DialInfo& info,
        const std::size_t recordIndex, const bool deleted, mwmp::ServerContentDatabaseStatistics& stats)
    {
        const std::string dialogueId = refIdToString(dialogue.mId);
        const std::string infoId = refIdToString(info.mId);
        const std::string questId = getOrCreateGeneratedQuestId(tables, engineContentIndex, packageId, dialogueId,
            dialogue.mStringId, recordIndex, false, stats);
        const std::string stepId = stableKey(
            { questId, "step", std::to_string(info.mData.mJournalIndex), infoId.empty() ? std::to_string(recordIndex) : infoId });

        std::string& result = tables.questSteps;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "stepId", stepId);
        result.push_back(',');
        appendJsonStringField(result, "questId", questId);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonStringField(result, "sourceInfoId", infoId);
        result.push_back(',');
        appendJsonNumberField(result, "index", info.mData.mJournalIndex);
        result.push_back(',');
        appendJsonStringField(result, "status", questStatusLabel(static_cast<int>(info.mQuestStatus)));
        result.push_back(',');
        appendJsonStringField(result, "text", info.mResponse);
        result.push_back(',');
        appendJsonStringField(result, "completionPolicy", inferCompletionPolicy(info));
        result.push_back(',');
        appendJsonBoolField(result, "deleted", deleted);
        appendQuestDbSourceObject(result, packageId, dialogueId, "Journal", infoId, recordIndex);
        result += "}\n";
        ++stats.generatedQuestStepCount;

        for (std::size_t i = 0; i < info.mSelects.size(); ++i)
            appendGeneratedConditionRow(tables, "quest_step", stepId, info.mSelects[i], i, packageId, dialogueId,
                "Journal", infoId, recordIndex, stats);
        appendGeneratedEffects(tables, "quest_step", stepId, info, packageId, dialogueId, "Journal", infoId,
            recordIndex, stats);
    }

    void appendGeneratedDialogueResponse(GeneratedQuestDbTables& tables, const std::size_t engineContentIndex,
        const std::string& packageId, const ESM::Dialogue& dialogue, const ESM::DialInfo& info,
        const std::size_t recordIndex, const std::size_t infoOrder, const bool deleted,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        const std::string dialogueId = refIdToString(dialogue.mId);
        const std::string infoId = refIdToString(info.mId);
        const std::string dialogueType = std::string(dialogTypeLabel(static_cast<int>(dialogue.mType)));
        const std::string topicId = getOrCreateGeneratedTopicId(tables, engineContentIndex, packageId, dialogueId,
            dialogueType, dialogue.mStringId, recordIndex, false, stats);
        const std::string responseId
            = stableKey({ topicId, "response", infoId.empty() ? std::to_string(recordIndex) : infoId });

        std::string& result = tables.dialogueResponses;
        result.push_back('{');
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "responseId", responseId);
        result.push_back(',');
        appendJsonStringField(result, "topicId", topicId);
        result.push_back(',');
        appendJsonStringField(result, "packageId", packageId);
        result.push_back(',');
        appendJsonStringField(result, "sourceInfoId", infoId);
        result.push_back(',');
        appendJsonNumberField(result, "order", infoOrder);
        result.push_back(',');
        appendJsonStringField(result, "actor", refIdToString(info.mActor));
        result.push_back(',');
        appendJsonStringField(result, "race", refIdToString(info.mRace));
        result.push_back(',');
        appendJsonStringField(result, "class", refIdToString(info.mClass));
        result.push_back(',');
        appendJsonStringField(result, "faction", refIdToString(info.mFaction));
        result.push_back(',');
        appendJsonStringField(result, "cell", refIdToString(info.mCell));
        result.push_back(',');
        appendJsonNumberField(result, "rank", static_cast<int>(info.mData.mRank));
        result.push_back(',');
        appendJsonNumberField(result, "gender", static_cast<int>(info.mData.mGender));
        result.push_back(',');
        appendJsonNumberField(result, "pcRank", static_cast<int>(info.mData.mPCrank));
        result.push_back(',');
        appendJsonNumberField(result, "disposition", info.mData.mDisposition);
        result.push_back(',');
        appendJsonStringField(result, "text", info.mResponse);
        result.push_back(',');
        appendJsonStringField(result, "resultPolicy", "transactional-server-effect");
        result.push_back(',');
        appendJsonStringField(result, "authorityPolicy", "server-evaluated");
        result.push_back(',');
        appendJsonStringField(result, "transactionPolicy", "dialogue-response-effect-plan");
        result.push_back(',');
        appendJsonBoolField(result, "deleted", deleted);
        appendQuestDbSourceObject(result, packageId, dialogueId, dialogueType, infoId, recordIndex);
        result += "}\n";
        ++stats.generatedDialogueResponseCount;

        for (std::size_t i = 0; i < info.mSelects.size(); ++i)
            appendGeneratedConditionRow(tables, "dialogue_response", responseId, info.mSelects[i], i, packageId,
                dialogueId, dialogueType, infoId, recordIndex, stats);
        appendGeneratedEffects(tables, "dialogue_response", responseId, info, packageId, dialogueId, dialogueType,
            infoId, recordIndex, stats);
    }

    GeneratedQuestDbTables buildGeneratedQuestDatabasePackage(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        GeneratedQuestDbTables tables;

        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
            if (contentFile.empty() || isBuiltinContentFile(contentFile) || !isEsmLikeContentFile(contentFile))
                continue;

            try
            {
                const std::filesystem::path resolvedPath = collections.getPath(contentFile);
                ESM::ESMReader esm;
                esm.setEncoder(&encoder);
                esm.setIndex(static_cast<int>(engineContentIndex));
                esm.open(resolvedPath);

                const std::string packageId = makeQuestPackageId(contentFile);
                appendGeneratedQuestDbPackageRow(
                    tables, engineContentIndex, contentFile, resolvedPath, esm, packageId, stats);

                ESM::Dialogue currentDialogue;
                bool hasCurrentDialogue = false;
                std::size_t recordIndex = 1;
                std::size_t infoOrder = 0;

                while (esm.hasMoreRecs())
                {
                    const ESM::NAME name = esm.getRecName();
                    std::uint32_t flags = 0;
                    esm.getRecHeader(flags);

                    if (name.toInt() == ESM::REC_DIAL)
                    {
                        bool isDeleted = false;
                        currentDialogue.blank();
                        currentDialogue.load(esm, isDeleted);
                        hasCurrentDialogue = true;

                        const std::string dialogueId = refIdToString(currentDialogue.mId);
                        const std::string dialogueType = std::string(dialogTypeLabel(static_cast<int>(currentDialogue.mType)));
                        if (currentDialogue.mType == ESM::Dialogue::Journal)
                            getOrCreateGeneratedQuestId(tables, engineContentIndex, packageId, dialogueId,
                                currentDialogue.mStringId, recordIndex, isDeleted, stats);
                        else
                            getOrCreateGeneratedTopicId(tables, engineContentIndex, packageId, dialogueId,
                                dialogueType, currentDialogue.mStringId, recordIndex, isDeleted, stats);
                    }
                    else if (name.toInt() == ESM::REC_INFO)
                    {
                        ESM::DialInfo info;
                        bool isDeleted = false;
                        info.load(esm, isDeleted);
                        ++infoOrder;

                        if (hasCurrentDialogue && currentDialogue.mType == ESM::Dialogue::Journal)
                            appendGeneratedQuestStep(
                                tables, engineContentIndex, packageId, currentDialogue, info, recordIndex, isDeleted, stats);
                        else if (hasCurrentDialogue)
                            appendGeneratedDialogueResponse(tables, engineContentIndex, packageId, currentDialogue, info,
                                recordIndex, infoOrder, isDeleted, stats);
                        else
                        {
                            currentDialogue.blank();
                            currentDialogue.mStringId.clear();
                            currentDialogue.mId = ESM::RefId();
                            currentDialogue.mType = ESM::Dialogue::Unknown;
                            appendGeneratedDialogueResponse(tables, engineContentIndex, packageId, currentDialogue, info,
                                recordIndex, infoOrder, isDeleted, stats);
                        }
                    }
                    else
                    {
                        hasCurrentDialogue = false;
                        esm.skipRecord();
                    }

                    ++recordIndex;
                }
            }
            catch (const std::exception& e)
            {
                ++stats.generatedQuestDatabaseImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
        }

        return tables;
    }

    std::string buildGeneratedQuestDbManifestJson(const GeneratedQuestDbTables& tables)
    {
        std::string result;
        result += "{\n  ";
        appendJsonStringField(result, "schema", questDatabaseRowSchema);
        result += ",\n  \"tables\":{";
        appendJsonNumberField(result, "packages", tables.seenPackages.size());
        result.push_back(',');
        appendJsonNumberField(result, "quest_definitions", tables.seenQuestDefinitions.size());
        result.push_back(',');
        appendJsonNumberField(result, "quest_steps", std::count(tables.questSteps.begin(), tables.questSteps.end(), '\n'));
        result.push_back(',');
        appendJsonNumberField(result, "dialogue_topics", tables.seenDialogueTopics.size());
        result.push_back(',');
        appendJsonNumberField(result, "dialogue_responses", std::count(tables.dialogueResponses.begin(), tables.dialogueResponses.end(), '\n'));
        result.push_back(',');
        appendJsonNumberField(result, "conditions", std::count(tables.conditions.begin(), tables.conditions.end(), '\n'));
        result.push_back(',');
        appendJsonNumberField(result, "quest_effects", std::count(tables.questEffects.begin(), tables.questEffects.end(), '\n'));
        result.push_back(',');
        appendJsonNumberField(result, "legacy_effects", std::count(tables.legacyEffects.begin(), tables.legacyEffects.end(), '\n'));
        result += "},\n  \"stateModel\":{";
        appendJsonStringField(result, "playerQuestState", "append-only quest events plus compacted per-player view");
        result.push_back(',');
        appendJsonStringField(result, "worldQuestState", "server-owned shared events keyed by questId and scope");
        result.push_back(',');
        appendJsonStringField(result, "locks", "dialogue/container/quest transactions should acquire server leases before mutation");
        result.push_back(',');
        appendJsonStringField(result, "runtimeAuthority", "server-owned records with explicit transaction and authority metadata");
        result += "}\n}\n";
        return result;
    }

    bool writeGeneratedQuestDatabasePackage(const std::filesystem::path& packagePath,
        const GeneratedQuestDbTables& tables)
    {
        bool changed = false;
        changed = writeIfChanged(packagePath / "packages.jsonl", tables.packages) || changed;
        changed = writeIfChanged(packagePath / "quest_definitions.jsonl", tables.questDefinitions) || changed;
        changed = writeIfChanged(packagePath / "quest_steps.jsonl", tables.questSteps) || changed;
        changed = writeIfChanged(packagePath / "dialogue_topics.jsonl", tables.dialogueTopics) || changed;
        changed = writeIfChanged(packagePath / "dialogue_responses.jsonl", tables.dialogueResponses) || changed;
        changed = writeIfChanged(packagePath / "conditions.jsonl", tables.conditions) || changed;
        changed = writeIfChanged(packagePath / "quest_effects.jsonl", tables.questEffects) || changed;
        changed = writeIfChanged(packagePath / "legacy_effects.jsonl", tables.legacyEffects) || changed;
        changed = writeIfChanged(packagePath / "manifest.json", buildGeneratedQuestDbManifestJson(tables)) || changed;
        return changed;
    }

    std::string buildQuestSourcesJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        std::string result;
        result.reserve(contentFiles.size() * 8192);

        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
            if (contentFile.empty() || isBuiltinContentFile(contentFile) || !isEsmLikeContentFile(contentFile))
                continue;

            try
            {
                const std::filesystem::path resolvedPath = collections.getPath(contentFile);
                ESM::ESMReader esm;
                esm.setEncoder(&encoder);
                esm.setIndex(static_cast<int>(engineContentIndex));
                esm.open(resolvedPath);

                const std::string packageId = makeQuestPackageId(contentFile);
                appendQuestSourcePackageRow(result, engineContentIndex, contentFile, resolvedPath, esm, packageId, stats);

                ESM::Dialogue currentDialogue;
                bool hasCurrentDialogue = false;
                std::size_t recordIndex = 1;
                std::size_t infoOrder = 0;
                while (esm.hasMoreRecs())
                {
                    const ESM::NAME name = esm.getRecName();
                    std::uint32_t flags = 0;
                    esm.getRecHeader(flags);

                    if (name.toInt() == ESM::REC_DIAL)
                    {
                        bool isDeleted = false;
                        currentDialogue.blank();
                        currentDialogue.load(esm, isDeleted);
                        hasCurrentDialogue = true;

                        appendQuestSourceDialogueRow(result, engineContentIndex, contentFile, packageId, recordIndex,
                            flags, isDeleted, currentDialogue, stats);
                    }
                    else if (name.toInt() == ESM::REC_INFO)
                    {
                        ESM::DialInfo info;
                        bool isDeleted = false;
                        info.load(esm, isDeleted);
                        ++infoOrder;

                        appendQuestSourceInfoRow(result, engineContentIndex, contentFile, packageId, recordIndex, flags,
                            isDeleted, hasCurrentDialogue, currentDialogue, info, infoOrder, stats);
                    }
                    else
                    {
                        hasCurrentDialogue = false;
                        esm.skipRecord();
                    }

                    ++recordIndex;
                }
            }
            catch (const std::exception& e)
            {
                ++stats.questSourceImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
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
        const std::vector<std::string>& contentFiles, const std::string& encoding,
        const std::vector<ServerDataFileRequirement>& dataFileRequirements)
    {
        std::lock_guard lock(mMutex);
        mStats = {};
        mStats.attempted = true;
        mStats.loadOrderSource = openMwContentVectorLoadOrderSource;
        mStats.loadOrderRule = laterContentEntryDominatesLoadOrderRule;
        mStats.rootPath = resolveDatabaseRoot();
        mStats.manifestPath = mStats.rootPath / "manifest.json";
        mStats.generatedQuestDatabasePath = resolveGeneratedQuestDatabasePath();
        mStats.tableCount = 5;

        try
        {
            ServerContentDatabaseStatistics newStats = mStats;
            const std::string dataDirsJsonl = buildDataDirsJsonl(dataDirs, newStats);
            const std::string loadOrderJsonl = buildLoadOrderJsonl(contentFiles, newStats);
            const std::string contentFilesJsonl
                = buildContentFilesJsonl(dataDirs, contentFiles, dataFileRequirements, newStats);
            const std::string recordIndexJsonl = buildRecordIndexJsonl(dataDirs, contentFiles, newStats);
            const std::string questSourcesJsonl = buildQuestSourcesJsonl(dataDirs, contentFiles, encoding, newStats);
            const GeneratedQuestDbTables generatedQuestDb
                = buildGeneratedQuestDatabasePackage(dataDirs, contentFiles, encoding, newStats);
            const std::string manifestJson = buildManifestJson(newStats);

            bool changed = false;
            changed = writeIfChanged(newStats.rootPath / "data_dirs.jsonl", dataDirsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "load_order.jsonl", loadOrderJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "content_files.jsonl", contentFilesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "record_index.jsonl", recordIndexJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "quest_sources.jsonl", questSourcesJsonl) || changed;
            changed = writeIfChanged(newStats.manifestPath, manifestJson) || changed;
            changed = writeGeneratedQuestDatabasePackage(newStats.generatedQuestDatabasePath, generatedQuestDb) || changed;

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
