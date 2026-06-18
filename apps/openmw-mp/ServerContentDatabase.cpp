#include "ServerContentDatabase.hpp"
#include "WorldDatabaseStore.hpp"

#include <components/esm/defs.hpp>
#include <components/esm/esmcommon.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadpgrd.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/spelllist.hpp>
#include <components/bsa/ba2dx10file.hpp>
#include <components/bsa/ba2gnrlfile.hpp>
#include <components/bsa/bsafile.hpp>
#include <components/bsa/compressedbsafile.hpp>
#include <components/files/collections.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/vfs/registerarchives.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
    constexpr const char* assetProviderRowSchema = "communitymp.worlddb.asset-provider.v1";
    constexpr const char* archiveFileRowSchema = "communitymp.worlddb.archive-file.v1";
    constexpr const char* resolvedAssetRowSchema = "communitymp.worlddb.resolved-asset.v1";
    constexpr const char* recordIndexRowSchema = "communitymp.worlddb.record-index.v1";
    constexpr const char* recordWinnerRowSchema = "communitymp.worlddb.record-winner.v1";
    constexpr const char* actorProfileRowSchema = "communitymp.worlddb.actor-profile.v1";
    constexpr const char* actorAiPackageRowSchema = "communitymp.worlddb.actor-ai-package.v1";
    constexpr const char* actorInventoryRowSchema = "communitymp.worlddb.actor-inventory.v1";
    constexpr const char* actorSpellbookRowSchema = "communitymp.worlddb.actor-spellbook.v1";
    constexpr const char* actorStatsDynamicRowSchema = "communitymp.worlddb.actor-stats-dynamic.v1";
    constexpr const char* actorEquipmentRowSchema = "communitymp.worlddb.actor-equipment.v1";
    constexpr const char* containerInventoryRowSchema = "communitymp.worlddb.container-inventory.v1";
    constexpr const char* pathgridPointRowSchema = "communitymp.worlddb.pathgrid-point.v1";
    constexpr const char* pathgridEdgeRowSchema = "communitymp.worlddb.pathgrid-edge.v1";
    constexpr const char* cellRecordRowSchema = "communitymp.worlddb.cell-record.v1";
    constexpr const char* cellReferenceRowSchema = "communitymp.worlddb.cell-reference.v1";
    constexpr const char* cellReferenceWinnerRowSchema = "communitymp.worlddb.cell-reference-winner.v1";
    constexpr const char* questSourceRowSchema = "communitymp.quest.source.v1";
    constexpr const char* questDatabaseRowSchema = "communitymp.questdb.v1";
    constexpr const char* builtinOpenMwScripts = "builtin.omwscripts";
    constexpr const char* openMwContentVectorLoadOrderSource
        = "openmw-application-settings-content-vector";
    constexpr const char* laterContentEntryDominatesLoadOrderRule
        = "higher-engineContentIndex-overrides-lower-engineContentIndex-for-the-same-record-key";

    std::string fileExtensionLower(std::string value);
    std::string normalizedLookupKey(std::string_view value);

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
        const auto appendByteEscape = [&](const unsigned char c) {
            result += "\\u00";
            result.push_back(hex[(c >> 4) & 0x0f]);
            result.push_back(hex[c & 0x0f]);
        };
        const auto isContinuation = [](const unsigned char c) {
            return (c & 0xc0) == 0x80;
        };
        const auto utf8SequenceLength = [](const unsigned char c) -> std::size_t {
            if (c < 0x80)
                return 1;
            if (c >= 0xc2 && c <= 0xdf)
                return 2;
            if (c >= 0xe0 && c <= 0xef)
                return 3;
            if (c >= 0xf0 && c <= 0xf4)
                return 4;
            return 0;
        };
        const auto isValidUtf8Sequence = [&](const std::size_t offset, const std::size_t length) {
            if (offset + length > value.size())
                return false;

            const unsigned char first = static_cast<unsigned char>(value[offset]);
            if (length == 1)
                return first < 0x80;

            const unsigned char second = static_cast<unsigned char>(value[offset + 1]);
            if (!isContinuation(second))
                return false;
            if (length == 3)
            {
                if (!isContinuation(static_cast<unsigned char>(value[offset + 2])))
                    return false;
                if (first == 0xe0 && second < 0xa0)
                    return false;
                if (first == 0xed && second >= 0xa0)
                    return false;
            }
            else if (length == 4)
            {
                if (!isContinuation(static_cast<unsigned char>(value[offset + 2]))
                    || !isContinuation(static_cast<unsigned char>(value[offset + 3])))
                    return false;
                if (first == 0xf0 && second < 0x90)
                    return false;
                if (first == 0xf4 && second > 0x8f)
                    return false;
            }

            return length == 2 || length == 3 || length == 4;
        };

        result.push_back('"');
        for (std::size_t i = 0; i < value.size();)
        {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    ++i;
                    break;
                case '\\':
                    result += "\\\\";
                    ++i;
                    break;
                case '\b':
                    result += "\\b";
                    ++i;
                    break;
                case '\f':
                    result += "\\f";
                    ++i;
                    break;
                case '\n':
                    result += "\\n";
                    ++i;
                    break;
                case '\r':
                    result += "\\r";
                    ++i;
                    break;
                case '\t':
                    result += "\\t";
                    ++i;
                    break;
                default:
                    if (c < 0x20)
                    {
                        appendByteEscape(c);
                        ++i;
                    }
                    else if (c < 0x80)
                    {
                        result.push_back(static_cast<char>(c));
                        ++i;
                    }
                    else
                    {
                        const std::size_t length = utf8SequenceLength(c);
                        if (length != 0 && isValidUtf8Sequence(i, length))
                        {
                            result.append(value.substr(i, length));
                            i += length;
                        }
                        else
                        {
                            appendByteEscape(c);
                            ++i;
                        }
                    }
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

    std::string_view bsaVersionLabel(Bsa::BsaVersion version)
    {
        switch (version)
        {
            case Bsa::BsaVersion::Unknown:
                return "unknown";
            case Bsa::BsaVersion::Uncompressed:
                return "bsa-uncompressed";
            case Bsa::BsaVersion::Compressed:
                return "bsa-compressed";
            case Bsa::BsaVersion::BA2GNRL:
                return "ba2-general";
            case Bsa::BsaVersion::BA2DX10:
                return "ba2-dx10";
        }

        return "unknown";
    }

    std::string utf8ArchiveName(std::string_view name, const ToUTF8::StatelessUtf8Encoder& encoder)
    {
        std::string buffer;
        const std::string_view utf8 = encoder.getUtf8(name, ToUTF8::BufferAllocationPolicy::UseGrowFactor, buffer);
        return std::string(utf8);
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
        result += ",\n  \"tables\":[\"data_dirs.jsonl\",\"load_order.jsonl\",\"content_files.jsonl\",\"asset_providers.jsonl\",\"archive_files.jsonl\",\"resolved_assets.jsonl\",\"record_index.jsonl\",\"record_winners.jsonl\",\"actor_profile.jsonl\",\"actor_ai_packages.jsonl\",\"actor_inventory.jsonl\",\"actor_spellbook.jsonl\",\"actor_stats_dynamic.jsonl\",\"actor_equipment.jsonl\",\"container_inventory.jsonl\",\"pathgrid_points.jsonl\",\"pathgrid_edges.jsonl\",\"cells.jsonl\",\"cell_references.jsonl\",\"cell_reference_winners.jsonl\",\"quest_sources.jsonl\"],\n  ";
        appendJsonNumberField(result, "tableCount", stats.tableCount);
        result += ",\n  ";
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
        appendJsonNumberField(result, "recordKeyCount", stats.recordKeyCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordUnkeyedCount", stats.recordUnkeyedCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordWinnerCount", stats.recordWinnerCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordWinnerDeletedCount", stats.recordWinnerDeletedCount);
        result += ",\n  ";
        appendJsonNumberField(result, "recordImportErrorCount", stats.recordImportErrorCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorProfileRecordCount", stats.actorProfileRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorProfileNpcCount", stats.actorProfileNpcCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorProfileCreatureCount", stats.actorProfileCreatureCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorProfileAutocalcNpcCount", stats.actorProfileAutocalcNpcCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorAiPackageRecordCount", stats.actorAiPackageRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorAiPackageItemCount", stats.actorAiPackageItemCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorInventoryRecordCount", stats.actorInventoryRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorInventoryItemCount", stats.actorInventoryItemCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorSpellbookRecordCount", stats.actorSpellbookRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorSpellbookSpellCount", stats.actorSpellbookSpellCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorStatsDynamicRecordCount", stats.actorStatsDynamicRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorStatsDynamicItemCount", stats.actorStatsDynamicItemCount);
        result += ",\n  ";
        appendJsonNumberField(result, "itemEquipmentRecordCount", stats.itemEquipmentRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorEquipmentRecordCount", stats.actorEquipmentRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "actorEquipmentItemCount", stats.actorEquipmentItemCount);
        result += ",\n  ";
        appendJsonNumberField(result, "containerInventoryRecordCount", stats.containerInventoryRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "containerInventoryItemCount", stats.containerInventoryItemCount);
        result += ",\n  ";
        appendJsonNumberField(result, "pathgridRecordCount", stats.pathgridRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "pathgridPointCount", stats.pathgridPointCount);
        result += ",\n  ";
        appendJsonNumberField(result, "pathgridEdgeCount", stats.pathgridEdgeCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellRecordCount", stats.cellRecordCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellReferenceCount", stats.cellReferenceCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellReferenceMovedCount", stats.cellReferenceMovedCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellReferenceDeletedCount", stats.cellReferenceDeletedCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellReferenceWinnerCount", stats.cellReferenceWinnerCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellReferenceWinnerDeletedCount", stats.cellReferenceWinnerDeletedCount);
        result += ",\n  ";
        appendJsonNumberField(result, "cellImportErrorCount", stats.cellImportErrorCount);
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
        result += ",\n  ";
        appendJsonNumberField(result, "archiveCount", stats.archiveCount);
        result += ",\n  ";
        appendJsonNumberField(result, "resolvedArchiveCount", stats.resolvedArchiveCount);
        result += ",\n  ";
        appendJsonNumberField(result, "unresolvedArchiveCount", stats.unresolvedArchiveCount);
        result += ",\n  ";
        appendJsonNumberField(result, "archiveFileCount", stats.archiveFileCount);
        result += ",\n  ";
        appendJsonNumberField(result, "assetProviderCount", stats.assetProviderCount);
        result += ",\n  ";
        appendJsonNumberField(result, "resolvedAssetCount", stats.resolvedAssetCount);
        result += ",\n  ";
        appendJsonNumberField(result, "assetImportErrorCount", stats.assetImportErrorCount);
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

    std::vector<std::string> existingArchives(
        const Files::Collections& collections, const std::vector<std::string>& archives)
    {
        std::vector<std::string> result;
        result.reserve(archives.size());
        for (const std::string& archive : archives)
        {
            if (!archive.empty() && collections.doesExist(archive))
                result.push_back(archive);
        }
        return result;
    }

    std::string buildAssetProvidersJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& archives, mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        std::string result;
        result.reserve((dataDirs.size() + archives.size()) * 260);

        std::size_t providerIndex = 0;
        for (std::size_t archiveIndex = 0; archiveIndex < archives.size(); ++archiveIndex)
        {
            const std::string& archive = archives[archiveIndex];
            if (archive.empty())
                continue;

            ++stats.archiveCount;
            std::filesystem::path resolvedPath;
            bool resolved = false;
            Bsa::BsaVersion version = Bsa::BsaVersion::Unknown;
            try
            {
                resolvedPath = collections.getPath(archive);
                resolved = true;
                version = Bsa::BSAFile::detectVersion(resolvedPath);
                ++stats.resolvedArchiveCount;
            }
            catch (const std::exception& e)
            {
                ++stats.unresolvedArchiveCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }

            ++stats.assetProviderCount;
            result.push_back('{');
            appendJsonStringField(result, "schema", assetProviderRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "providerIndex", providerIndex++);
            result.push_back(',');
            appendJsonNumberField(result, "priorityOrder", archiveIndex);
            result.push_back(',');
            appendJsonStringField(result, "providerType", "bsa");
            result.push_back(',');
            appendJsonStringField(result, "name", archive);
            result.push_back(',');
            appendJsonStringField(result, "resolvedPath", resolved ? pathToLogString(resolvedPath) : "");
            result.push_back(',');
            appendJsonBoolField(result, "resolved", resolved);
            result.push_back(',');
            appendJsonStringField(result, "archiveVersion", bsaVersionLabel(version));
            result.push_back(',');
            appendJsonNumberField(result, "size", resolved ? fileSizeOrNegative(resolvedPath) : -1);
            result.push_back(',');
            appendJsonNumberField(result, "lastWriteTime", resolved ? fileTimeOrZero(resolvedPath) : 0);
            result += "}\n";
        }

        std::set<std::filesystem::path> seenDataDirs;
        for (std::size_t dataDirIndex = 0; dataDirIndex < dataDirs.size(); ++dataDirIndex)
        {
            const std::filesystem::path& dataDir = dataDirs[dataDirIndex];
            if (!seenDataDirs.insert(dataDir).second)
                continue;

            ++stats.assetProviderCount;
            result.push_back('{');
            appendJsonStringField(result, "schema", assetProviderRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "providerIndex", providerIndex++);
            result.push_back(',');
            appendJsonNumberField(result, "priorityOrder", archives.size() + dataDirIndex);
            result.push_back(',');
            appendJsonStringField(result, "providerType", "loose-data-dir");
            result.push_back(',');
            appendJsonStringField(result, "name", pathToLogString(dataDir));
            result.push_back(',');
            appendJsonStringField(result, "resolvedPath", pathToLogString(dataDir));
            result.push_back(',');
            appendJsonBoolField(result, "resolved", true);
            result.push_back(',');
            appendJsonStringField(result, "archiveVersion", "");
            result.push_back(',');
            appendJsonNumberField(result, "size", -1);
            result.push_back(',');
            appendJsonNumberField(result, "lastWriteTime", fileTimeOrZero(dataDir));
            result += "}\n";
        }

        return result;
    }

    template <class BSAFileType>
    void appendArchiveFileRowsForType(std::string& result, const std::size_t archiveIndex,
        const std::string& archiveName, const std::filesystem::path& archivePath, Bsa::BsaVersion version,
        const ToUTF8::StatelessUtf8Encoder& encoder, mwmp::ServerContentDatabaseStatistics& stats)
    {
        BSAFileType file;
        file.open(archivePath);
        std::size_t fileIndex = 0;
        for (const Bsa::BSAFile::FileStruct& entry : file.getList())
        {
            const std::string originalName = utf8ArchiveName(entry.name(), encoder);
            result.push_back('{');
            appendJsonStringField(result, "schema", archiveFileRowSchema);
            result.push_back(',');
            appendJsonNumberField(result, "archiveIndex", archiveIndex);
            result.push_back(',');
            appendJsonNumberField(result, "fileIndex", fileIndex++);
            result.push_back(',');
            appendJsonStringField(result, "archiveName", archiveName);
            result.push_back(',');
            appendJsonStringField(result, "archivePath", pathToLogString(archivePath));
            result.push_back(',');
            appendJsonStringField(result, "archiveVersion", bsaVersionLabel(version));
            result.push_back(',');
            appendJsonStringField(result, "path", VFS::Path::normalizeFilename(originalName));
            result.push_back(',');
            appendJsonStringField(result, "originalPath", originalName);
            result.push_back(',');
            appendJsonStringField(result, "extension", fileExtensionLower(originalName));
            result.push_back(',');
            appendJsonNumberField(result, "size", entry.mFileSize);
            result.push_back(',');
            appendJsonNumberField(result, "offset", entry.mOffset);
            result.push_back(',');
            appendJsonNumberField(result, "hashLow", entry.mHash.mLow);
            result.push_back(',');
            appendJsonNumberField(result, "hashHigh", entry.mHash.mHigh);
            result += "}\n";
            ++stats.archiveFileCount;
        }
    }

    void appendArchiveFileRows(std::string& result, const std::size_t archiveIndex,
        const std::string& archiveName, const std::filesystem::path& archivePath,
        const ToUTF8::StatelessUtf8Encoder& encoder, mwmp::ServerContentDatabaseStatistics& stats)
    {
        const Bsa::BsaVersion version = Bsa::BSAFile::detectVersion(archivePath);
        switch (version)
        {
            case Bsa::BsaVersion::Uncompressed:
                appendArchiveFileRowsForType<Bsa::BSAFile>(
                    result, archiveIndex, archiveName, archivePath, version, encoder, stats);
                return;
            case Bsa::BsaVersion::Compressed:
                appendArchiveFileRowsForType<Bsa::CompressedBSAFile>(
                    result, archiveIndex, archiveName, archivePath, version, encoder, stats);
                return;
            case Bsa::BsaVersion::BA2GNRL:
                appendArchiveFileRowsForType<Bsa::BA2GNRLFile>(
                    result, archiveIndex, archiveName, archivePath, version, encoder, stats);
                return;
            case Bsa::BsaVersion::BA2DX10:
                appendArchiveFileRowsForType<Bsa::BA2DX10File>(
                    result, archiveIndex, archiveName, archivePath, version, encoder, stats);
                return;
            case Bsa::BsaVersion::Unknown:
                throw std::runtime_error("unknown archive type " + pathToLogString(archivePath));
        }
    }

    std::string buildArchiveFilesJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& archives, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        std::string result;
        result.reserve(archives.size() * 4096);

        for (std::size_t archiveIndex = 0; archiveIndex < archives.size(); ++archiveIndex)
        {
            const std::string& archive = archives[archiveIndex];
            if (archive.empty())
                continue;

            try
            {
                appendArchiveFileRows(result, archiveIndex, archive, collections.getPath(archive),
                    encoder.getStatelessEncoder(), stats);
            }
            catch (const std::exception& e)
            {
                ++stats.assetImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
        }

        return result;
    }

    std::string buildResolvedAssetsJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& archives, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        std::string result;
        result.reserve((dataDirs.size() + archives.size()) * 4096);

        try
        {
            VFS::Manager vfs;
            VFS::registerArchives(
                &vfs, collections, existingArchives(collections, archives), true, &encoder.getStatelessEncoder());

            std::size_t assetIndex = 0;
            for (const VFS::Path::Normalized& asset : vfs.getRecursiveDirectoryIterator())
            {
                const std::string_view normalizedPath = asset.view();
                result.push_back('{');
                appendJsonStringField(result, "schema", resolvedAssetRowSchema);
                result.push_back(',');
                appendJsonNumberField(result, "assetIndex", assetIndex++);
                result.push_back(',');
                appendJsonStringField(result, "path", normalizedPath);
                result.push_back(',');
                appendJsonStringField(result, "extension", fileExtensionLower(std::string(normalizedPath)));
                result.push_back(',');
                appendJsonStringField(result, "provider", vfs.getArchive(asset));
                result.push_back(',');
                appendJsonStringField(result, "stem", vfs.getStem(asset));
                result.push_back(',');
                appendJsonNumberField(result, "lastWriteTime", static_cast<long long>(
                    vfs.getLastModified(asset).time_since_epoch().count()));
                result += "}\n";
                ++stats.resolvedAssetCount;
            }
        }
        catch (const std::exception& e)
        {
            ++stats.assetImportErrorCount;
            if (stats.lastError.empty())
                stats.lastError = e.what();
        }

        return result;
    }

    struct RecordIdentity
    {
        bool available = false;
        bool deletedSubrecord = false;
        std::string recordId;
        std::string recordKey;
        std::string keyKind;
    };

    struct ImportedInventoryItem
    {
        std::string refId;
        int count = 0;
    };

    struct ImportedActorSpell
    {
        std::string spellId;
    };

    struct ImportedActorProfile
    {
        bool npc = false;
        bool autocalc = false;
        int level = 0;
        int flags = 0;
        int bloodType = 0;
        int services = 0;
        std::string displayName;
        std::string model;
        std::string script;
        std::string race;
        std::string classId;
        std::string faction;
        std::string head;
        std::string hair;
        std::string original;
        int factionRank = -1;
        int disposition = -1;
        int reputation = -1;
        int gold = 0;
        int creatureType = -1;
        int soul = -1;
        int combat = -1;
        int magic = -1;
        int stealth = -1;
        float scale = 1.f;
        std::array<int, ESM::Attribute::Length> attributes{};
        std::array<int, ESM::Skill::Length> skills{};
        std::array<int, 6> attacks{};
    };

    struct ImportedActorAiPackage
    {
        unsigned int packageTypeInt = 0;
        std::string packageType;
        unsigned int action = 0;
        int distance = 0;
        int duration = 0;
        int timeOfDay = -1;
        std::array<int, 8> idle{};
        bool shouldRepeat = false;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        std::string targetId;
        std::string cellName;
    };

    struct ImportedDynamicStat
    {
        float base = 0.f;
        float mod = 0.f;
        float current = 0.f;
        float damage = 0.f;
        float progress = 0.f;
    };

    struct ImportedEquipmentItem
    {
        std::string refId;
        int count = 0;
        int charge = -1;
        float enchantmentCharge = -1.f;
    };

    enum class ImportedEquipmentKind
    {
        None,
        Weapon,
        Armor,
        Clothing,
    };

    struct ImportedEquipmentMetadata
    {
        ImportedEquipmentKind kind = ImportedEquipmentKind::None;
        std::vector<int> slots;
        bool stacks = false;
        int value = 0;
        int health = -1;
        int armor = 0;
        int weaponType = ESM::Weapon::None;
        int weaponMaxDamage = 0;
        int weaponAmmoType = ESM::Weapon::None;
    };

    struct ImportedPathgridPoint
    {
        std::size_t pointIndex = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        unsigned int autogenerated = 0;
        unsigned int connectionCount = 0;
    };

    struct ImportedPathgridEdge
    {
        std::size_t edgeOrder = 0;
        std::size_t fromPoint = 0;
        std::size_t toPoint = 0;
    };

    struct ImportedPathgrid
    {
        bool interior = false;
        std::string cellKey;
        std::string cellName;
        int gridX = 0;
        int gridY = 0;
        int granularity = 0;
        std::vector<ImportedPathgridPoint> points;
        std::vector<ImportedPathgridEdge> edges;
    };

    struct IndexedRecordRow
    {
        std::size_t engineContentIndex = 0;
        std::string contentFile;
        std::size_t recordIndex = 0;
        ESM::NAME recordName;
        std::uint32_t flags = 0;
        std::size_t recordOffset = 0;
        std::size_t dataOffset = 0;
        std::size_t dataSize = 0;
        RecordIdentity identity;
        bool actorAiAvailable = false;
        std::size_t actorAiPackageCount = 0;
        unsigned int actorAiAction = 0;
        unsigned int actorAiDistance = 0;
        unsigned int actorAiDuration = 0;
        bool actorAiShouldRepeat = false;
        float actorAiCoordinateX = 0.f;
        float actorAiCoordinateY = 0.f;
        float actorAiCoordinateZ = 0.f;
        std::string actorAiTargetId;
        std::string actorAiCellName;
        unsigned int actorAiHello = 0;
        unsigned int actorAiFight = 0;
        unsigned int actorAiFlee = 0;
        unsigned int actorAiAlarm = 0;
        bool actorAiPackagesImported = false;
        std::vector<ImportedActorAiPackage> actorAiPackages;
        bool actorProfileImported = false;
        ImportedActorProfile actorProfile;
        bool actorInventoryImported = false;
        std::vector<ImportedInventoryItem> actorInventory;
        bool actorSpellbookImported = false;
        std::vector<ImportedActorSpell> actorSpellbook;
        bool actorStatsDynamicImported = false;
        bool actorStatsDynamicAutocalc = false;
        std::array<ImportedDynamicStat, 3> actorStatsDynamic;
        bool itemEquipmentImported = false;
        ImportedEquipmentMetadata itemEquipment;
        bool actorEquipmentImported = false;
        std::array<ImportedEquipmentItem, mwmp::equipmentSlotCount> actorEquipment;
        std::size_t actorEquipmentItemCount = 0;
        bool containerInventoryImported = false;
        std::vector<ImportedInventoryItem> containerInventory;
        bool pathgridImported = false;
        ImportedPathgrid pathgrid;
    };

    struct RecordIndexTables
    {
        std::string recordIndexJsonl;
        std::string recordWinnersJsonl;
        std::string actorProfileJsonl;
        std::string actorAiPackagesJsonl;
        std::string actorInventoryJsonl;
        std::string actorSpellbookJsonl;
        std::string actorStatsDynamicJsonl;
        std::string actorEquipmentJsonl;
        std::string containerInventoryJsonl;
        std::string pathgridPointsJsonl;
        std::string pathgridEdgesJsonl;
    };

    RecordIdentity makeRecordIdentity(std::string recordId, std::string recordKey, std::string keyKind,
        const bool deletedSubrecord = false)
    {
        RecordIdentity result;
        result.available = !recordKey.empty();
        result.deletedSubrecord = deletedSubrecord;
        result.recordId = std::move(recordId);
        result.recordKey = std::move(recordKey);
        result.keyKind = std::move(keyKind);
        return result;
    }

    RecordIdentity makeUnkeyedRecordIdentity(const bool deletedSubrecord)
    {
        RecordIdentity result;
        result.deletedSubrecord = deletedSubrecord;
        return result;
    }

    bool isDeletedRecord(const IndexedRecordRow& row)
    {
        return (row.flags & ESM::FLAG_Deleted) != 0 || row.identity.deletedSubrecord;
    }

    namespace EquipmentSlot
    {
        constexpr int Helmet = 0;
        constexpr int Cuirass = 1;
        constexpr int Greaves = 2;
        constexpr int LeftPauldron = 3;
        constexpr int RightPauldron = 4;
        constexpr int LeftGauntlet = 5;
        constexpr int RightGauntlet = 6;
        constexpr int Boots = 7;
        constexpr int Shirt = 8;
        constexpr int Pants = 9;
        constexpr int Skirt = 10;
        constexpr int Robe = 11;
        constexpr int LeftRing = 12;
        constexpr int RightRing = 13;
        constexpr int Amulet = 14;
        constexpr int Belt = 15;
        constexpr int CarriedRight = 16;
        constexpr int CarriedLeft = 17;
        constexpr int Ammunition = 18;
    }

    bool isValidEquipmentSlot(const int slot)
    {
        return slot >= 0 && slot < mwmp::equipmentSlotCount;
    }

    std::vector<int> armorEquipmentSlots(const int armorType)
    {
        switch (armorType)
        {
            case ESM::Armor::Helmet:
                return { EquipmentSlot::Helmet };
            case ESM::Armor::Cuirass:
                return { EquipmentSlot::Cuirass };
            case ESM::Armor::LPauldron:
                return { EquipmentSlot::LeftPauldron };
            case ESM::Armor::RPauldron:
                return { EquipmentSlot::RightPauldron };
            case ESM::Armor::Greaves:
                return { EquipmentSlot::Greaves };
            case ESM::Armor::Boots:
                return { EquipmentSlot::Boots };
            case ESM::Armor::LGauntlet:
            case ESM::Armor::LBracer:
                return { EquipmentSlot::LeftGauntlet };
            case ESM::Armor::RGauntlet:
            case ESM::Armor::RBracer:
                return { EquipmentSlot::RightGauntlet };
            case ESM::Armor::Shield:
                return { EquipmentSlot::CarriedLeft };
        }

        return {};
    }

    std::vector<int> clothingEquipmentSlots(const int clothingType)
    {
        switch (clothingType)
        {
            case ESM::Clothing::Pants:
                return { EquipmentSlot::Pants };
            case ESM::Clothing::Shoes:
                return { EquipmentSlot::Boots };
            case ESM::Clothing::Shirt:
                return { EquipmentSlot::Shirt };
            case ESM::Clothing::Belt:
                return { EquipmentSlot::Belt };
            case ESM::Clothing::Robe:
                return { EquipmentSlot::Robe };
            case ESM::Clothing::RGlove:
                return { EquipmentSlot::RightGauntlet };
            case ESM::Clothing::LGlove:
                return { EquipmentSlot::LeftGauntlet };
            case ESM::Clothing::Skirt:
                return { EquipmentSlot::Skirt };
            case ESM::Clothing::Ring:
                return { EquipmentSlot::LeftRing, EquipmentSlot::RightRing };
            case ESM::Clothing::Amulet:
                return { EquipmentSlot::Amulet };
        }

        return {};
    }

    std::vector<int> weaponEquipmentSlots(const int weaponType)
    {
        switch (weaponType)
        {
            case ESM::Weapon::Arrow:
            case ESM::Weapon::Bolt:
                return { EquipmentSlot::Ammunition };
            case ESM::Weapon::MarksmanThrown:
                return { EquipmentSlot::CarriedRight };
            case ESM::Weapon::ShortBladeOneHand:
            case ESM::Weapon::LongBladeOneHand:
            case ESM::Weapon::LongBladeTwoHand:
            case ESM::Weapon::BluntOneHand:
            case ESM::Weapon::BluntTwoClose:
            case ESM::Weapon::BluntTwoWide:
            case ESM::Weapon::SpearTwoWide:
            case ESM::Weapon::AxeOneHand:
            case ESM::Weapon::AxeTwoHand:
            case ESM::Weapon::MarksmanBow:
            case ESM::Weapon::MarksmanCrossbow:
                return { EquipmentSlot::CarriedRight };
        }

        return {};
    }

    int requiredAmmoTypeForWeapon(const int weaponType)
    {
        if (weaponType == ESM::Weapon::MarksmanBow)
            return ESM::Weapon::Arrow;
        if (weaponType == ESM::Weapon::MarksmanCrossbow)
            return ESM::Weapon::Bolt;
        return ESM::Weapon::None;
    }

    void skipUnreadSubrecordBytes(ESM::ESMReader& esm, const std::size_t bytesRead)
    {
        const std::size_t size = esm.getSubSize();
        if (size > bytesRead)
            esm.skip(size - bytesRead);
    }

    RecordIdentity extractDialogueIdentity(ESM::ESMReader& esm, std::string& currentDialogueId,
        const bool updatesCurrentDialogue)
    {
        std::string id;
        std::string fallbackName;
        bool deletedSubrecord = false;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::fourCC("ID__"))
                id = refIdToString(esm.getRefId());
            else if (subName.toInt() == ESM::SREC_NAME)
            {
                if (esm.getFormatVersion() <= ESM::MaxStringRefIdFormatVersion)
                    fallbackName = esm.getHString();
                else if (esm.getFormatVersion() <= ESM::MaxNameIsRefIdOnlyFormatVersion)
                    fallbackName = refIdToString(esm.getRefId());
                else
                {
                    const std::string displayName = esm.getHString();
                    if (id.empty())
                        fallbackName = displayName;
                }
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (id.empty())
            id = fallbackName;

        if (id.empty())
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        if (updatesCurrentDialogue && !deletedSubrecord)
            currentDialogueId = id;

        return makeRecordIdentity(id, normalizedLookupKey(id), "dialogue-id", deletedSubrecord);
    }

    RecordIdentity extractInfoIdentity(ESM::ESMReader& esm, const std::string& currentDialogueId)
    {
        std::string id;
        bool deletedSubrecord = false;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::fourCC("INAM"))
                id = refIdToString(esm.getRefId());
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (id.empty())
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        if (currentDialogueId.empty())
            return makeRecordIdentity(id, "orphan-info:" + normalizedLookupKey(id), "dialogue-info-id",
                deletedSubrecord);

        return makeRecordIdentity(currentDialogueId + "/" + id,
            "dialogue-info:" + normalizedLookupKey(currentDialogueId) + "/" + normalizedLookupKey(id),
            "dialogue-info-id", deletedSubrecord);
    }

    RecordIdentity extractCellIdentity(ESM::ESMReader& esm)
    {
        std::string name;
        bool hasData = false;
        bool deletedSubrecord = false;
        std::int32_t flags = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::SREC_NAME)
                name = esm.getHString();
            else if (subName.toInt() == ESM::fourCC("DATA"))
            {
                esm.getSubHeader();
                if (esm.getSubSize() >= sizeof(flags) + sizeof(x) + sizeof(y))
                {
                    esm.getT(flags);
                    esm.getT(x);
                    esm.getT(y);
                    skipUnreadSubrecordBytes(esm, sizeof(flags) + sizeof(x) + sizeof(y));
                    hasData = true;
                }
                else
                    esm.skip(esm.getSubSize());
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!hasData)
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        if ((flags & 0x01) != 0)
        {
            if (name.empty())
                return makeUnkeyedRecordIdentity(deletedSubrecord);
            return makeRecordIdentity(name, "interior:" + normalizedLookupKey(name), "cell-id", deletedSubrecord);
        }

        const std::string id = "exterior:" + std::to_string(x) + "," + std::to_string(y);
        return makeRecordIdentity(id, id, "cell-id", deletedSubrecord);
    }

    RecordIdentity extractLandIdentity(ESM::ESMReader& esm)
    {
        bool hasLocation = false;
        bool deletedSubrecord = false;
        std::int32_t x = 0;
        std::int32_t y = 0;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::fourCC("INTV"))
            {
                esm.getHT(x, y);
                hasLocation = true;
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!hasLocation)
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        const std::string id = "exterior:" + std::to_string(x) + "," + std::to_string(y);
        return makeRecordIdentity(id, id, "land-cell", deletedSubrecord);
    }

    RecordIdentity extractPathgridIdentity(ESM::ESMReader& esm)
    {
        std::string cellName;
        bool hasData = false;
        bool deletedSubrecord = false;
        std::int32_t x = 0;
        std::int32_t y = 0;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::SREC_NAME)
                cellName = refIdToString(esm.getRefId());
            else if (subName.toInt() == ESM::fourCC("DATA"))
            {
                esm.getSubHeader();
                if (esm.getSubSize() >= sizeof(x) + sizeof(y))
                {
                    esm.getT(x);
                    esm.getT(y);
                    skipUnreadSubrecordBytes(esm, sizeof(x) + sizeof(y));
                    hasData = true;
                }
                else
                    esm.skip(esm.getSubSize());
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!hasData)
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        if (x == 0 && y == 0 && !cellName.empty())
            return makeRecordIdentity(cellName, "interior:" + normalizedLookupKey(cellName), "pathgrid-cell",
                deletedSubrecord);

        const std::string id = "exterior:" + std::to_string(x) + "," + std::to_string(y);
        return makeRecordIdentity(id, id, "pathgrid-cell", deletedSubrecord);
    }

    RecordIdentity extractIndexIdentity(ESM::ESMReader& esm, const char* keyKind)
    {
        bool hasIndex = false;
        bool deletedSubrecord = false;
        std::int32_t index = 0;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::fourCC("INDX"))
            {
                esm.getHT(index);
                hasIndex = true;
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!hasIndex)
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        const std::string id = "index:" + std::to_string(index);
        return makeRecordIdentity(id, id, keyKind, deletedSubrecord);
    }

    RecordIdentity extractLandTextureIdentity(ESM::ESMReader& esm)
    {
        std::string id;
        bool hasIndex = false;
        bool deletedSubrecord = false;
        std::int32_t index = 0;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::SREC_NAME)
                id = refIdToString(esm.getRefId());
            else if (subName.toInt() == ESM::fourCC("INTV"))
            {
                esm.getHT(index);
                hasIndex = true;
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!id.empty())
            return makeRecordIdentity(id, normalizedLookupKey(id), "land-texture-id", deletedSubrecord);

        if (hasIndex)
        {
            const std::string fallback = "index:" + std::to_string(index);
            return makeRecordIdentity(fallback, fallback, "land-texture-index", deletedSubrecord);
        }

        return makeUnkeyedRecordIdentity(deletedSubrecord);
    }

    RecordIdentity extractGenericRecordIdentity(ESM::ESMReader& esm)
    {
        std::string id;
        bool deletedSubrecord = false;

        while (esm.hasMoreSubs())
        {
            esm.getSubName();
            const ESM::NAME subName = esm.retSubName();
            if (subName.toInt() == ESM::fourCC("ID__") || subName.toInt() == ESM::fourCC("INAM"))
            {
                const std::string candidate = refIdToString(esm.getRefId());
                if (!candidate.empty())
                    id = candidate;
            }
            else if (subName.toInt() == ESM::SREC_NAME)
            {
                const std::string candidate = refIdToString(esm.getRefId());
                if (id.empty() && !candidate.empty())
                    id = candidate;
            }
            else if (subName.toInt() == ESM::SREC_DELE)
            {
                esm.skipHSub();
                deletedSubrecord = true;
            }
            else
                esm.skipHSub();
        }

        if (!id.empty())
            return makeRecordIdentity(id, normalizedLookupKey(id), "record-id", deletedSubrecord);

        return makeUnkeyedRecordIdentity(deletedSubrecord);
    }

    RecordIdentity extractRecordIdentity(ESM::ESMReader& esm, const ESM::NAME recordName, const std::uint32_t flags,
        std::string& currentDialogueId)
    {
        if ((flags & ESM::FLAG_Ignored) != 0)
            return {};

        const bool deleted = (flags & ESM::FLAG_Deleted) != 0;

        switch (recordName.toInt())
        {
            case ESM::REC_DIAL:
                return extractDialogueIdentity(esm, currentDialogueId, !deleted);
            case ESM::REC_INFO:
                return extractInfoIdentity(esm, currentDialogueId);
            case ESM::REC_CELL:
                currentDialogueId.clear();
                return extractCellIdentity(esm);
            case ESM::REC_LAND:
                currentDialogueId.clear();
                return extractLandIdentity(esm);
            case ESM::REC_PGRD:
                currentDialogueId.clear();
                return extractPathgridIdentity(esm);
            case ESM::REC_LTEX:
                currentDialogueId.clear();
                return extractLandTextureIdentity(esm);
            case ESM::REC_MGEF:
                currentDialogueId.clear();
                return extractIndexIdentity(esm, "magic-effect-index");
            case ESM::REC_SKIL:
                currentDialogueId.clear();
                return extractIndexIdentity(esm, "skill-index");
            default:
                currentDialogueId.clear();
                return extractGenericRecordIdentity(esm);
        }
    }

    unsigned int clampNonNegativeShort(const int value)
    {
        return static_cast<unsigned int>(std::max(0, value));
    }

    void applyAiData(IndexedRecordRow& row, const ESM::AIData& aiData)
    {
        row.actorAiHello = aiData.mHello;
        row.actorAiFight = aiData.mFight;
        row.actorAiFlee = aiData.mFlee;
        row.actorAiAlarm = aiData.mAlarm;
    }

    const char* aiPackageTypeName(const ESM::AiPackageType type)
    {
        switch (type)
        {
            case ESM::AI_Wander:
                return "wander";
            case ESM::AI_Travel:
                return "travel";
            case ESM::AI_Follow:
                return "follow";
            case ESM::AI_Escort:
                return "escort";
            case ESM::AI_Activate:
                return "activate";
        }

        return "unknown";
    }

    unsigned int aiActionForPackageType(const ESM::AiPackageType type)
    {
        switch (type)
        {
            case ESM::AI_Wander:
                return mwmp::BaseActorList::WANDER;
            case ESM::AI_Travel:
                return mwmp::BaseActorList::TRAVEL;
            case ESM::AI_Follow:
                return mwmp::BaseActorList::FOLLOW;
            case ESM::AI_Escort:
                return mwmp::BaseActorList::ESCORT;
            case ESM::AI_Activate:
                return mwmp::BaseActorList::ACTIVATE;
        }

        return 0;
    }

    ImportedActorAiPackage importAiPackage(const ESM::AIPackage& package)
    {
        ImportedActorAiPackage imported;
        imported.packageTypeInt = static_cast<unsigned int>(package.mType);
        imported.packageType = aiPackageTypeName(package.mType);
        imported.action = aiActionForPackageType(package.mType);
        imported.idle.fill(0);

        switch (package.mType)
        {
            case ESM::AI_Wander:
                imported.distance = package.mWander.mDistance;
                imported.duration = package.mWander.mDuration;
                imported.timeOfDay = package.mWander.mTimeOfDay;
                imported.shouldRepeat = package.mWander.mShouldRepeat != 0;
                for (std::size_t i = 0; i < imported.idle.size(); ++i)
                    imported.idle[i] = package.mWander.mIdle[i];
                break;

            case ESM::AI_Travel:
                imported.x = package.mTravel.mX;
                imported.y = package.mTravel.mY;
                imported.z = package.mTravel.mZ;
                imported.shouldRepeat = package.mTravel.mShouldRepeat != 0;
                break;

            case ESM::AI_Follow:
            case ESM::AI_Escort:
                imported.duration = package.mTarget.mDuration;
                imported.x = package.mTarget.mX;
                imported.y = package.mTarget.mY;
                imported.z = package.mTarget.mZ;
                imported.targetId = package.mTarget.mId.toString();
                imported.cellName = package.mCellName;
                imported.shouldRepeat = package.mTarget.mShouldRepeat != 0;
                break;

            case ESM::AI_Activate:
                imported.targetId = package.mActivate.mName.toString();
                imported.shouldRepeat = package.mActivate.mShouldRepeat != 0;
                break;
        }

        return imported;
    }

    void importAiPackageList(const ESM::AIPackageList& packages, std::vector<ImportedActorAiPackage>& result)
    {
        result.reserve(packages.mList.size());
        for (const ESM::AIPackage& package : packages.mList)
            result.push_back(importAiPackage(package));
    }

    void applyPrimaryAiPackage(IndexedRecordRow& row, const ESM::AIPackageList& packages)
    {
        row.actorAiPackageCount = packages.mList.size();
        if (packages.mList.empty())
            return;

        const ESM::AIPackage& package = packages.mList.front();
        switch (package.mType)
        {
            case ESM::AI_Wander:
                row.actorAiAvailable = true;
                row.actorAiAction = mwmp::BaseActorList::WANDER;
                row.actorAiDistance = clampNonNegativeShort(package.mWander.mDistance);
                row.actorAiDuration = clampNonNegativeShort(package.mWander.mDuration);
                row.actorAiShouldRepeat = package.mWander.mShouldRepeat != 0;
                break;

            case ESM::AI_Travel:
                row.actorAiAvailable = true;
                row.actorAiAction = mwmp::BaseActorList::TRAVEL;
                row.actorAiCoordinateX = package.mTravel.mX;
                row.actorAiCoordinateY = package.mTravel.mY;
                row.actorAiCoordinateZ = package.mTravel.mZ;
                row.actorAiShouldRepeat = package.mTravel.mShouldRepeat != 0;
                break;

            case ESM::AI_Follow:
            case ESM::AI_Escort:
                row.actorAiAvailable = true;
                row.actorAiAction = package.mType == ESM::AI_Follow ? mwmp::BaseActorList::FOLLOW
                                                                     : mwmp::BaseActorList::ESCORT;
                row.actorAiDuration = clampNonNegativeShort(package.mTarget.mDuration);
                row.actorAiCoordinateX = package.mTarget.mX;
                row.actorAiCoordinateY = package.mTarget.mY;
                row.actorAiCoordinateZ = package.mTarget.mZ;
                row.actorAiTargetId = package.mTarget.mId.toString();
                row.actorAiCellName = package.mCellName;
                row.actorAiShouldRepeat = package.mTarget.mShouldRepeat != 0;
                break;

            case ESM::AI_Activate:
                row.actorAiAvailable = true;
                row.actorAiAction = mwmp::BaseActorList::ACTIVATE;
                row.actorAiTargetId = package.mActivate.mName.toString();
                row.actorAiShouldRepeat = package.mActivate.mShouldRepeat != 0;
                break;
        }
    }

    RecordIdentity makeActorRecordIdentity(const std::string& id, const bool deletedSubrecord)
    {
        if (id.empty())
            return makeUnkeyedRecordIdentity(deletedSubrecord);

        return makeRecordIdentity(id, normalizedLookupKey(id), "record-id", deletedSubrecord);
    }

    RecordIdentity makePathgridRecordIdentity(const ImportedPathgrid& pathgrid, const bool deletedSubrecord)
    {
        if (pathgrid.interior)
            return makeRecordIdentity(pathgrid.cellName, pathgrid.cellKey, "pathgrid-cell", deletedSubrecord);

        const std::string id = "exterior:" + std::to_string(pathgrid.gridX) + "," + std::to_string(pathgrid.gridY);
        return makeRecordIdentity(id, pathgrid.cellKey, "pathgrid-cell", deletedSubrecord);
    }

    void importInventoryList(const ESM::InventoryList& inventory, std::vector<ImportedInventoryItem>& result)
    {
        result.reserve(inventory.mList.size());
        for (const ESM::ContItem& item : inventory.mList)
        {
            if (item.mCount <= 0 || item.mItem.empty())
                continue;

            ImportedInventoryItem imported;
            imported.refId = refIdToString(item.mItem);
            imported.count = item.mCount;
            result.push_back(std::move(imported));
        }
    }

    void importSpellList(const ESM::SpellList& spells, std::vector<ImportedActorSpell>& result)
    {
        result.reserve(spells.mList.size());
        for (const ESM::RefId& spell : spells.mList)
        {
            if (spell.empty())
                continue;

            ImportedActorSpell imported;
            imported.spellId = refIdToString(spell);
            result.push_back(std::move(imported));
        }
    }

    ImportedDynamicStat makeImportedDynamicStat(const float value)
    {
        ImportedDynamicStat stat;
        stat.base = value;
        stat.mod = 0.f;
        stat.current = value;
        stat.damage = 0.f;
        stat.progress = 0.f;
        return stat;
    }

    void applyDirectActorStatsDynamic(IndexedRecordRow& row, const float health, const float magicka,
        const float fatigue)
    {
        row.actorStatsDynamicImported = true;
        row.actorStatsDynamic[0] = makeImportedDynamicStat(health);
        row.actorStatsDynamic[1] = makeImportedDynamicStat(magicka);
        row.actorStatsDynamic[2] = makeImportedDynamicStat(fatigue);
    }

    void applyNpcActorProfile(IndexedRecordRow& row, const ESM::NPC& npc)
    {
        row.actorProfileImported = true;
        ImportedActorProfile& profile = row.actorProfile;
        profile = {};
        profile.npc = true;
        profile.autocalc = npc.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS;
        profile.level = npc.mNpdt.mLevel;
        profile.flags = npc.mFlags;
        profile.bloodType = npc.mBloodType;
        profile.services = npc.mAiData.mServices;
        profile.displayName = npc.mName;
        profile.model = npc.mModel;
        profile.script = refIdToString(npc.mScript);
        profile.race = refIdToString(npc.mRace);
        profile.classId = refIdToString(npc.mClass);
        profile.faction = refIdToString(npc.mFaction);
        profile.head = refIdToString(npc.mHead);
        profile.hair = refIdToString(npc.mHair);
        profile.factionRank = npc.getFactionRank();
        profile.disposition = npc.mNpdt.mDisposition;
        profile.reputation = npc.mNpdt.mReputation;
        profile.gold = npc.mNpdt.mGold;
        profile.skills.fill(-1);
        profile.attacks.fill(0);

        if (!profile.autocalc)
        {
            for (std::size_t i = 0; i < profile.attributes.size(); ++i)
                profile.attributes[i] = npc.mNpdt.mAttributes[i];
            for (std::size_t i = 0; i < profile.skills.size(); ++i)
                profile.skills[i] = npc.mNpdt.mSkills[i];
        }
        else
        {
            profile.attributes.fill(-1);
        }
    }

    void applyCreatureActorProfile(IndexedRecordRow& row, const ESM::Creature& creature)
    {
        row.actorProfileImported = true;
        ImportedActorProfile& profile = row.actorProfile;
        profile = {};
        profile.npc = false;
        profile.level = creature.mData.mLevel;
        profile.flags = creature.mFlags;
        profile.bloodType = creature.mBloodType;
        profile.services = creature.mAiData.mServices;
        profile.displayName = creature.mName;
        profile.model = creature.mModel;
        profile.script = refIdToString(creature.mScript);
        profile.original = refIdToString(creature.mOriginal);
        profile.creatureType = creature.mData.mType;
        profile.soul = creature.mData.mSoul;
        profile.combat = creature.mData.mCombat;
        profile.magic = creature.mData.mMagic;
        profile.stealth = creature.mData.mStealth;
        profile.gold = creature.mData.mGold;
        profile.scale = creature.mScale;
        profile.skills.fill(-1);

        for (std::size_t i = 0; i < profile.attributes.size(); ++i)
            profile.attributes[i] = creature.mData.mAttributes[i];
        for (std::size_t i = 0; i < profile.attacks.size(); ++i)
            profile.attacks[i] = creature.mData.mAttack[i];
    }

    bool loadEquippableItemRecordRowData(ESM::ESMReader& esm, const ESM::NAME recordName, IndexedRecordRow& row)
    {
        if (recordName.toInt() == ESM::REC_WEAP)
        {
            ESM::Weapon weapon;
            bool deletedSubrecord = false;
            weapon.load(esm, deletedSubrecord);
            row.identity = makeRecordIdentity(refIdToString(weapon.mId), normalizedLookupKey(refIdToString(weapon.mId)),
                "record-id", deletedSubrecord);
            row.itemEquipmentImported = true;
            row.itemEquipment.kind = ImportedEquipmentKind::Weapon;
            row.itemEquipment.slots = weaponEquipmentSlots(weapon.mData.mType);
            row.itemEquipment.stacks = weapon.mData.mType == ESM::Weapon::Arrow
                || weapon.mData.mType == ESM::Weapon::Bolt
                || weapon.mData.mType == ESM::Weapon::MarksmanThrown;
            row.itemEquipment.value = weapon.mData.mValue;
            row.itemEquipment.health = weapon.mData.mHealth;
            row.itemEquipment.weaponType = weapon.mData.mType;
            row.itemEquipment.weaponMaxDamage = std::max<int>(
                { weapon.mData.mChop[1], weapon.mData.mSlash[1], weapon.mData.mThrust[1] });
            row.itemEquipment.weaponAmmoType = weapon.mData.mType == ESM::Weapon::Arrow
                || weapon.mData.mType == ESM::Weapon::Bolt
                ? weapon.mData.mType
                : requiredAmmoTypeForWeapon(weapon.mData.mType);
            return true;
        }

        if (recordName.toInt() == ESM::REC_ARMO)
        {
            ESM::Armor armor;
            bool deletedSubrecord = false;
            armor.load(esm, deletedSubrecord);
            row.identity = makeRecordIdentity(refIdToString(armor.mId), normalizedLookupKey(refIdToString(armor.mId)),
                "record-id", deletedSubrecord);
            row.itemEquipmentImported = true;
            row.itemEquipment.kind = ImportedEquipmentKind::Armor;
            row.itemEquipment.slots = armorEquipmentSlots(armor.mData.mType);
            row.itemEquipment.value = armor.mData.mValue;
            row.itemEquipment.health = armor.mData.mHealth;
            row.itemEquipment.armor = armor.mData.mArmor;
            return true;
        }

        if (recordName.toInt() == ESM::REC_CLOT)
        {
            ESM::Clothing clothing;
            bool deletedSubrecord = false;
            clothing.load(esm, deletedSubrecord);
            row.identity = makeRecordIdentity(refIdToString(clothing.mId),
                normalizedLookupKey(refIdToString(clothing.mId)), "record-id", deletedSubrecord);
            row.itemEquipmentImported = true;
            row.itemEquipment.kind = ImportedEquipmentKind::Clothing;
            row.itemEquipment.slots = clothingEquipmentSlots(clothing.mData.mType);
            row.itemEquipment.value = clothing.mData.mValue;
            return true;
        }

        return false;
    }

    bool loadActorRecordRowData(ESM::ESMReader& esm, const ESM::NAME recordName, IndexedRecordRow& row)
    {
        if (recordName.toInt() == ESM::REC_NPC_)
        {
            ESM::NPC npc;
            bool deletedSubrecord = false;
            npc.load(esm, deletedSubrecord);
            row.identity = makeActorRecordIdentity(refIdToString(npc.mId), deletedSubrecord);
            applyAiData(row, npc.mAiData);
            applyPrimaryAiPackage(row, npc.mAiPackage);
            row.actorAiPackagesImported = true;
            importAiPackageList(npc.mAiPackage, row.actorAiPackages);
            applyNpcActorProfile(row, npc);
            row.actorStatsDynamicAutocalc = npc.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS;
            if (!row.actorStatsDynamicAutocalc)
                applyDirectActorStatsDynamic(row, static_cast<float>(npc.mNpdt.mHealth),
                    static_cast<float>(npc.mNpdt.mMana), static_cast<float>(npc.mNpdt.mFatigue));
            row.actorInventoryImported = true;
            importInventoryList(npc.mInventory, row.actorInventory);
            row.actorSpellbookImported = true;
            importSpellList(npc.mSpells, row.actorSpellbook);
            return true;
        }

        if (recordName.toInt() == ESM::REC_CREA)
        {
            ESM::Creature creature;
            bool deletedSubrecord = false;
            creature.load(esm, deletedSubrecord);
            row.identity = makeActorRecordIdentity(refIdToString(creature.mId), deletedSubrecord);
            applyAiData(row, creature.mAiData);
            applyPrimaryAiPackage(row, creature.mAiPackage);
            row.actorAiPackagesImported = true;
            importAiPackageList(creature.mAiPackage, row.actorAiPackages);
            applyCreatureActorProfile(row, creature);
            applyDirectActorStatsDynamic(row, static_cast<float>(creature.mData.mHealth),
                static_cast<float>(creature.mData.mMana), static_cast<float>(creature.mData.mFatigue));
            row.actorInventoryImported = true;
            importInventoryList(creature.mInventory, row.actorInventory);
            row.actorSpellbookImported = true;
            importSpellList(creature.mSpells, row.actorSpellbook);
            return true;
        }

        return false;
    }

    bool loadContainerRecordRowData(ESM::ESMReader& esm, const ESM::NAME recordName, IndexedRecordRow& row)
    {
        if (recordName.toInt() != ESM::REC_CONT)
            return false;

        ESM::Container container;
        bool deletedSubrecord = false;
        container.load(esm, deletedSubrecord);
        row.identity = makeRecordIdentity(
            refIdToString(container.mId), normalizedLookupKey(refIdToString(container.mId)), "record-id",
            deletedSubrecord);
        row.containerInventoryImported = true;
        importInventoryList(container.mInventory, row.containerInventory);
        return true;
    }

    bool loadPathgridRecordRowData(ESM::ESMReader& esm, const ESM::NAME recordName, IndexedRecordRow& row)
    {
        if (recordName.toInt() != ESM::REC_PGRD)
            return false;

        ESM::Pathgrid pathgrid;
        bool deletedSubrecord = false;
        pathgrid.load(esm, deletedSubrecord);

        row.pathgridImported = true;
        row.pathgrid = {};
        row.pathgrid.cellName = refIdToString(pathgrid.mCell);
        row.pathgrid.gridX = pathgrid.mData.mX;
        row.pathgrid.gridY = pathgrid.mData.mY;
        row.pathgrid.granularity = pathgrid.mData.mGranularity;
        row.pathgrid.interior = row.pathgrid.gridX == 0 && row.pathgrid.gridY == 0 && !row.pathgrid.cellName.empty();
        row.pathgrid.cellKey = row.pathgrid.interior
            ? "interior:" + normalizedLookupKey(row.pathgrid.cellName)
            : "exterior:" + std::to_string(row.pathgrid.gridX) + "," + std::to_string(row.pathgrid.gridY);
        row.pathgrid.points.reserve(pathgrid.mPoints.size());
        row.pathgrid.edges.reserve(pathgrid.mEdges.size());

        for (std::size_t pointIndex = 0; pointIndex < pathgrid.mPoints.size(); ++pointIndex)
        {
            const ESM::Pathgrid::Point& point = pathgrid.mPoints[pointIndex];
            ImportedPathgridPoint imported;
            imported.pointIndex = pointIndex;
            imported.x = point.mX;
            imported.y = point.mY;
            imported.z = point.mZ;
            imported.autogenerated = point.mAutogenerated;
            imported.connectionCount = point.mConnectionNum;
            row.pathgrid.points.push_back(imported);
        }

        for (std::size_t edgeOrder = 0; edgeOrder < pathgrid.mEdges.size(); ++edgeOrder)
        {
            const ESM::Pathgrid::Edge& edge = pathgrid.mEdges[edgeOrder];
            ImportedPathgridEdge imported;
            imported.edgeOrder = edgeOrder;
            imported.fromPoint = edge.mV0;
            imported.toPoint = edge.mV1;
            row.pathgrid.edges.push_back(imported);
        }

        row.identity = makePathgridRecordIdentity(row.pathgrid, deletedSubrecord);
        return true;
    }

    void appendRecordKeyFields(std::string& result, const RecordIdentity& identity)
    {
        appendJsonBoolField(result, "recordKeyAvailable", identity.available);
        result.push_back(',');
        appendJsonStringField(result, "recordKey", identity.available ? std::string_view(identity.recordKey) : std::string_view{});
        result.push_back(',');
        appendJsonStringField(result, "recordId", identity.available ? std::string_view(identity.recordId) : std::string_view{});
        result.push_back(',');
        appendJsonStringField(result, "recordKeyKind", identity.available ? std::string_view(identity.keyKind) : std::string_view{});
    }

    void appendRecordIndexRow(std::string& result, const IndexedRecordRow& row)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", recordIndexRowSchema);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", row.contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", row.recordIndex);
        result.push_back(',');
        appendJsonStringField(result, "recordType", row.recordName.toStringView());
        result.push_back(',');
        appendJsonNumberField(result, "recordTypeInt", row.recordName.toInt());
        result.push_back(',');
        appendRecordKeyFields(result, row.identity);
        result.push_back(',');
        appendJsonNumberField(result, "flags", row.flags);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", isDeletedRecord(row));
        result.push_back(',');
        appendJsonBoolField(result, "recordFlagDeleted", (row.flags & ESM::FLAG_Deleted) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "deleteSubrecord", row.identity.deletedSubrecord);
        result.push_back(',');
        appendJsonBoolField(result, "ignored", (row.flags & ESM::FLAG_Ignored) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "persistent", (row.flags & ESM::FLAG_Persistent) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "blocked", (row.flags & ESM::FLAG_Blocked) != 0);
        result.push_back(',');
        appendJsonNumberField(result, "recordOffset", row.recordOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataOffset", row.dataOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataSize", row.dataSize);
        result += "}\n";
    }

    void appendRecordWinnerRow(std::string& result, const IndexedRecordRow& row)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", recordWinnerRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "recordType", row.recordName.toStringView());
        result.push_back(',');
        appendJsonNumberField(result, "recordTypeInt", row.recordName.toInt());
        result.push_back(',');
        appendRecordKeyFields(result, row.identity);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", row.contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", row.recordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "flags", row.flags);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", isDeletedRecord(row));
        result.push_back(',');
        appendJsonBoolField(result, "recordFlagDeleted", (row.flags & ESM::FLAG_Deleted) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "deleteSubrecord", row.identity.deletedSubrecord);
        result.push_back(',');
        appendJsonBoolField(result, "tombstone", isDeletedRecord(row));
        result.push_back(',');
        appendJsonNumberField(result, "recordOffset", row.recordOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataOffset", row.dataOffset);
        result.push_back(',');
        appendJsonNumberField(result, "dataSize", row.dataSize);
        result.push_back(',');
        appendJsonStringField(result, "loadOrderRule", laterContentEntryDominatesLoadOrderRule);
        result.push_back(',');
        appendJsonBoolField(result, "actorAiAvailable", row.actorAiAvailable);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiPackageCount", row.actorAiPackageCount);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiAction", row.actorAiAction);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiDistance", row.actorAiDistance);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiDuration", row.actorAiDuration);
        result.push_back(',');
        appendJsonBoolField(result, "actorAiShouldRepeat", row.actorAiShouldRepeat);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiCoordinateX", row.actorAiCoordinateX);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiCoordinateY", row.actorAiCoordinateY);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiCoordinateZ", row.actorAiCoordinateZ);
        result.push_back(',');
        appendJsonStringField(result, "actorAiTargetId", row.actorAiTargetId);
        result.push_back(',');
        appendJsonStringField(result, "actorAiCellName", row.actorAiCellName);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiHello", row.actorAiHello);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiFight", row.actorAiFight);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiFlee", row.actorAiFlee);
        result.push_back(',');
        appendJsonNumberField(result, "actorAiAlarm", row.actorAiAlarm);
        result.push_back(',');
        appendJsonBoolField(result, "actorAiPackagesImported", row.actorAiPackagesImported);
        result.push_back(',');
        appendJsonBoolField(result, "actorProfileImported", row.actorProfileImported);
        result.push_back(',');
        appendJsonBoolField(result, "actorProfileNpc", row.actorProfile.npc);
        result.push_back(',');
        appendJsonBoolField(result, "actorProfileAutocalc", row.actorProfile.autocalc);
        result.push_back(',');
        appendJsonBoolField(result, "actorInventoryImported", row.actorInventoryImported);
        result.push_back(',');
        appendJsonNumberField(result, "actorInventoryItemCount", row.actorInventory.size());
        result.push_back(',');
        appendJsonBoolField(result, "actorSpellbookImported", row.actorSpellbookImported);
        result.push_back(',');
        appendJsonNumberField(result, "actorSpellbookSpellCount", row.actorSpellbook.size());
        result.push_back(',');
        appendJsonBoolField(result, "actorStatsDynamicImported", row.actorStatsDynamicImported);
        result.push_back(',');
        appendJsonBoolField(result, "actorStatsDynamicAutocalc", row.actorStatsDynamicAutocalc);
        result.push_back(',');
        appendJsonNumberField(result, "actorStatsDynamicItemCount", row.actorStatsDynamicImported ? 3 : 0);
        result.push_back(',');
        appendJsonBoolField(result, "itemEquipmentImported", row.itemEquipmentImported);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentSlotCount", row.itemEquipment.slots.size());
        result.push_back(',');
        appendJsonBoolField(result, "itemEquipmentStacks", row.itemEquipment.stacks);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentKind", static_cast<int>(row.itemEquipment.kind));
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentValue", row.itemEquipment.value);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentHealth", row.itemEquipment.health);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentArmor", row.itemEquipment.armor);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentWeaponType", row.itemEquipment.weaponType);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentWeaponMaxDamage", row.itemEquipment.weaponMaxDamage);
        result.push_back(',');
        appendJsonNumberField(result, "itemEquipmentWeaponAmmoType", row.itemEquipment.weaponAmmoType);
        result.push_back(',');
        appendJsonBoolField(result, "actorEquipmentImported", row.actorEquipmentImported);
        result.push_back(',');
        appendJsonNumberField(result, "actorEquipmentItemCount", row.actorEquipmentItemCount);
        result.push_back(',');
        appendJsonBoolField(result, "containerInventoryImported", row.containerInventoryImported);
        result.push_back(',');
        appendJsonNumberField(result, "containerInventoryItemCount", row.containerInventory.size());
        result.push_back(',');
        appendJsonBoolField(result, "pathgridImported", row.pathgridImported);
        result.push_back(',');
        appendJsonNumberField(result, "pathgridPointCount", row.pathgrid.points.size());
        result.push_back(',');
        appendJsonNumberField(result, "pathgridEdgeCount", row.pathgrid.edges.size());
        result += "}\n";
    }

    void appendInventoryRows(std::string& result, const IndexedRecordRow& row,
        const std::vector<ImportedInventoryItem>& inventory, const char* schema,
        std::size_t& recordCount, std::size_t& itemCount)
    {
        if (isDeletedRecord(row) || !row.identity.available)
            return;

        ++recordCount;
        for (std::size_t itemOrder = 0; itemOrder < inventory.size(); ++itemOrder)
        {
            const ImportedInventoryItem& item = inventory[itemOrder];
            result.push_back('{');
            appendJsonStringField(result, "schema", schema);
            result.push_back(',');
            appendJsonStringField(result, "recordKey", row.identity.recordKey);
            result.push_back(',');
            appendJsonStringField(result, "recordId", row.identity.recordId);
            result.push_back(',');
            appendJsonStringField(result, "sourceFile", row.contentFile);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "recordIndex", row.recordIndex);
            result.push_back(',');
            appendJsonNumberField(result, "itemOrder", itemOrder);
            result.push_back(',');
            appendJsonStringField(result, "itemRefId", item.refId);
            result.push_back(',');
            appendJsonNumberField(result, "count", item.count);
            result += "}\n";
            ++itemCount;
        }
    }

    void appendActorInventoryRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorInventoryImported)
            return;

        appendInventoryRows(result, row, row.actorInventory, actorInventoryRowSchema,
            stats.actorInventoryRecordCount, stats.actorInventoryItemCount);
    }

    void appendActorProfileRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorProfileImported || isDeletedRecord(row) || !row.identity.available)
            return;

        const ImportedActorProfile& profile = row.actorProfile;
        result.push_back('{');
        appendJsonStringField(result, "schema", actorProfileRowSchema);
        result.push_back(',');
        appendJsonStringField(result, "recordKey", row.identity.recordKey);
        result.push_back(',');
        appendJsonStringField(result, "recordId", row.identity.recordId);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", row.contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", row.recordIndex);
        result.push_back(',');
        appendJsonStringField(result, "actorKind", profile.npc ? "npc" : "creature");
        result.push_back(',');
        appendJsonBoolField(result, "npc", profile.npc);
        result.push_back(',');
        appendJsonBoolField(result, "autocalc", profile.autocalc);
        result.push_back(',');
        appendJsonNumberField(result, "level", profile.level);
        result.push_back(',');
        appendJsonNumberField(result, "flags", profile.flags);
        result.push_back(',');
        appendJsonNumberField(result, "bloodType", profile.bloodType);
        result.push_back(',');
        appendJsonNumberField(result, "services", profile.services);
        result.push_back(',');
        appendJsonStringField(result, "displayName", profile.displayName);
        result.push_back(',');
        appendJsonStringField(result, "model", profile.model);
        result.push_back(',');
        appendJsonStringField(result, "script", profile.script);
        result.push_back(',');
        appendJsonStringField(result, "race", profile.race);
        result.push_back(',');
        appendJsonStringField(result, "class", profile.classId);
        result.push_back(',');
        appendJsonStringField(result, "faction", profile.faction);
        result.push_back(',');
        appendJsonStringField(result, "head", profile.head);
        result.push_back(',');
        appendJsonStringField(result, "hair", profile.hair);
        result.push_back(',');
        appendJsonStringField(result, "original", profile.original);
        result.push_back(',');
        appendJsonNumberField(result, "factionRank", profile.factionRank);
        result.push_back(',');
        appendJsonNumberField(result, "disposition", profile.disposition);
        result.push_back(',');
        appendJsonNumberField(result, "reputation", profile.reputation);
        result.push_back(',');
        appendJsonNumberField(result, "gold", profile.gold);
        result.push_back(',');
        appendJsonNumberField(result, "creatureType", profile.creatureType);
        result.push_back(',');
        appendJsonNumberField(result, "soul", profile.soul);
        result.push_back(',');
        appendJsonNumberField(result, "combat", profile.combat);
        result.push_back(',');
        appendJsonNumberField(result, "magic", profile.magic);
        result.push_back(',');
        appendJsonNumberField(result, "stealth", profile.stealth);
        result.push_back(',');
        appendJsonNumberField(result, "scale", profile.scale);
        for (std::size_t i = 0; i < profile.attributes.size(); ++i)
        {
            const std::string fieldName = "attribute" + std::to_string(i);
            result.push_back(',');
            appendJsonNumberField(result, fieldName, profile.attributes[i]);
        }
        for (std::size_t i = 0; i < profile.skills.size(); ++i)
        {
            const std::string fieldName = "skill" + std::to_string(i);
            result.push_back(',');
            appendJsonNumberField(result, fieldName, profile.skills[i]);
        }
        for (std::size_t i = 0; i < profile.attacks.size(); ++i)
        {
            const std::string fieldName = "attack" + std::to_string(i);
            result.push_back(',');
            appendJsonNumberField(result, fieldName, profile.attacks[i]);
        }
        result += "}\n";

        ++stats.actorProfileRecordCount;
        if (profile.npc)
            ++stats.actorProfileNpcCount;
        else
            ++stats.actorProfileCreatureCount;
        if (profile.npc && profile.autocalc)
            ++stats.actorProfileAutocalcNpcCount;
    }

    void appendActorAiPackageRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorAiPackagesImported || isDeletedRecord(row) || !row.identity.available)
            return;

        ++stats.actorAiPackageRecordCount;
        for (std::size_t packageOrder = 0; packageOrder < row.actorAiPackages.size(); ++packageOrder)
        {
            const ImportedActorAiPackage& package = row.actorAiPackages[packageOrder];
            result.push_back('{');
            appendJsonStringField(result, "schema", actorAiPackageRowSchema);
            result.push_back(',');
            appendJsonStringField(result, "recordKey", row.identity.recordKey);
            result.push_back(',');
            appendJsonStringField(result, "recordId", row.identity.recordId);
            result.push_back(',');
            appendJsonStringField(result, "sourceFile", row.contentFile);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "recordIndex", row.recordIndex);
            result.push_back(',');
            appendJsonNumberField(result, "packageOrder", packageOrder);
            result.push_back(',');
            appendJsonStringField(result, "packageType", package.packageType);
            result.push_back(',');
            appendJsonNumberField(result, "packageTypeInt", package.packageTypeInt);
            result.push_back(',');
            appendJsonNumberField(result, "action", package.action);
            result.push_back(',');
            appendJsonNumberField(result, "distance", package.distance);
            result.push_back(',');
            appendJsonNumberField(result, "duration", package.duration);
            result.push_back(',');
            appendJsonNumberField(result, "timeOfDay", package.timeOfDay);
            for (std::size_t i = 0; i < package.idle.size(); ++i)
            {
                const std::string fieldName = "idle" + std::to_string(i);
                result.push_back(',');
                appendJsonNumberField(result, fieldName, package.idle[i]);
            }
            result.push_back(',');
            appendJsonBoolField(result, "shouldRepeat", package.shouldRepeat);
            result.push_back(',');
            appendJsonNumberField(result, "x", package.x);
            result.push_back(',');
            appendJsonNumberField(result, "y", package.y);
            result.push_back(',');
            appendJsonNumberField(result, "z", package.z);
            result.push_back(',');
            appendJsonStringField(result, "targetId", package.targetId);
            result.push_back(',');
            appendJsonStringField(result, "cellName", package.cellName);
            result += "}\n";
            ++stats.actorAiPackageItemCount;
        }
    }

    void appendActorSpellbookRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorSpellbookImported || isDeletedRecord(row) || !row.identity.available)
            return;

        ++stats.actorSpellbookRecordCount;
        for (std::size_t spellOrder = 0; spellOrder < row.actorSpellbook.size(); ++spellOrder)
        {
            const ImportedActorSpell& spell = row.actorSpellbook[spellOrder];
            if (spell.spellId.empty())
                continue;

            result.push_back('{');
            appendJsonStringField(result, "schema", actorSpellbookRowSchema);
            result.push_back(',');
            appendJsonStringField(result, "recordKey", row.identity.recordKey);
            result.push_back(',');
            appendJsonStringField(result, "recordId", row.identity.recordId);
            result.push_back(',');
            appendJsonStringField(result, "sourceFile", row.contentFile);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "recordIndex", row.recordIndex);
            result.push_back(',');
            appendJsonNumberField(result, "spellOrder", spellOrder);
            result.push_back(',');
            appendJsonStringField(result, "spellId", spell.spellId);
            result += "}\n";
            ++stats.actorSpellbookSpellCount;
        }
    }

    void appendActorStatsDynamicRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorStatsDynamicImported || isDeletedRecord(row) || !row.identity.available)
            return;

        ++stats.actorStatsDynamicRecordCount;
        for (int statIndex = 0; statIndex < 3; ++statIndex)
        {
            const ImportedDynamicStat& stat = row.actorStatsDynamic[statIndex];
            result.push_back('{');
            appendJsonStringField(result, "schema", actorStatsDynamicRowSchema);
            result.push_back(',');
            appendJsonStringField(result, "recordKey", row.identity.recordKey);
            result.push_back(',');
            appendJsonStringField(result, "recordId", row.identity.recordId);
            result.push_back(',');
            appendJsonStringField(result, "sourceFile", row.contentFile);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "recordIndex", row.recordIndex);
            result.push_back(',');
            appendJsonNumberField(result, "statIndex", statIndex);
            result.push_back(',');
            appendJsonNumberField(result, "base", stat.base);
            result.push_back(',');
            appendJsonNumberField(result, "mod", stat.mod);
            result.push_back(',');
            appendJsonNumberField(result, "current", stat.current);
            result.push_back(',');
            appendJsonNumberField(result, "damage", stat.damage);
            result.push_back(',');
            appendJsonNumberField(result, "progress", stat.progress);
            result += "}\n";
            ++stats.actorStatsDynamicItemCount;
        }
    }

    void appendContainerInventoryRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.containerInventoryImported)
            return;

        appendInventoryRows(result, row, row.containerInventory, containerInventoryRowSchema,
            stats.containerInventoryRecordCount, stats.containerInventoryItemCount);
    }

    void appendPathgridRows(RecordIndexTables& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.pathgridImported || isDeletedRecord(row) || !row.identity.available || row.pathgrid.cellKey.empty())
            return;

        ++stats.pathgridRecordCount;
        for (const ImportedPathgridPoint& point : row.pathgrid.points)
        {
            result.pathgridPointsJsonl.push_back('{');
            appendJsonStringField(result.pathgridPointsJsonl, "schema", pathgridPointRowSchema);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonStringField(result.pathgridPointsJsonl, "cellKey", row.pathgrid.cellKey);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonStringField(result.pathgridPointsJsonl, "cellName", row.pathgrid.cellName);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonStringField(result.pathgridPointsJsonl, "sourceFile", row.contentFile);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "loadOrderIndex", row.engineContentIndex);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "engineContentIndex", row.engineContentIndex);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "recordIndex", row.recordIndex);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "gridX", row.pathgrid.gridX);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "gridY", row.pathgrid.gridY);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "granularity", row.pathgrid.granularity);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "pointIndex", point.pointIndex);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "x", point.x);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "y", point.y);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "z", point.z);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "autogenerated", point.autogenerated);
            result.pathgridPointsJsonl.push_back(',');
            appendJsonNumberField(result.pathgridPointsJsonl, "connectionCount", point.connectionCount);
            result.pathgridPointsJsonl += "}\n";
            ++stats.pathgridPointCount;
        }

        for (const ImportedPathgridEdge& edge : row.pathgrid.edges)
        {
            result.pathgridEdgesJsonl.push_back('{');
            appendJsonStringField(result.pathgridEdgesJsonl, "schema", pathgridEdgeRowSchema);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonStringField(result.pathgridEdgesJsonl, "cellKey", row.pathgrid.cellKey);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonStringField(result.pathgridEdgesJsonl, "cellName", row.pathgrid.cellName);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonStringField(result.pathgridEdgesJsonl, "sourceFile", row.contentFile);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "loadOrderIndex", row.engineContentIndex);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "engineContentIndex", row.engineContentIndex);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "recordIndex", row.recordIndex);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "edgeOrder", edge.edgeOrder);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "fromPoint", edge.fromPoint);
            result.pathgridEdgesJsonl.push_back(',');
            appendJsonNumberField(result.pathgridEdgesJsonl, "toPoint", edge.toPoint);
            result.pathgridEdgesJsonl += "}\n";
            ++stats.pathgridEdgeCount;
        }
    }

    struct EquipmentSlotCandidate
    {
        bool assigned = false;
        ImportedEquipmentItem item;
        int score = 0;
    };

    struct WeaponEquipmentCandidate
    {
        ImportedEquipmentItem item;
        int weaponType = ESM::Weapon::None;
        int requiredAmmoType = ESM::Weapon::None;
        int score = 0;
    };

    ImportedEquipmentItem makeEquippedItem(const ImportedInventoryItem& inventoryItem,
        const ImportedEquipmentMetadata& metadata)
    {
        ImportedEquipmentItem item;
        item.refId = inventoryItem.refId;
        item.count = metadata.stacks ? inventoryItem.count : 1;
        item.charge = metadata.health;
        item.enchantmentCharge = -1.f;
        return item;
    }

    int equipmentScore(const ImportedEquipmentMetadata& metadata)
    {
        switch (metadata.kind)
        {
            case ImportedEquipmentKind::Weapon:
                return metadata.weaponMaxDamage * 1000 + metadata.value;
            case ImportedEquipmentKind::Armor:
                return 1000000 + metadata.armor * 1000 + metadata.value;
            case ImportedEquipmentKind::Clothing:
                return metadata.value;
            case ImportedEquipmentKind::None:
                return 0;
        }

        return 0;
    }

    void assignEquipmentSlot(std::array<EquipmentSlotCandidate, mwmp::equipmentSlotCount>& slots,
        const std::vector<int>& possibleSlots, const ImportedEquipmentItem& item, const int score)
    {
        if (item.refId.empty() || item.count <= 0 || possibleSlots.empty())
            return;

        int selectedSlot = -1;
        for (const int slot : possibleSlots)
        {
            if (!isValidEquipmentSlot(slot))
                continue;

            if (!slots[slot].assigned)
            {
                selectedSlot = slot;
                break;
            }
        }

        if (selectedSlot == -1)
        {
            for (const int slot : possibleSlots)
            {
                if (!isValidEquipmentSlot(slot))
                    continue;

                if (selectedSlot == -1 || slots[slot].score < slots[selectedSlot].score)
                    selectedSlot = slot;
            }

            if (selectedSlot == -1 || slots[selectedSlot].score >= score)
                return;
        }

        slots[selectedSlot].assigned = true;
        slots[selectedSlot].item = item;
        slots[selectedSlot].score = score;
    }

    void assignAmmoCandidate(WeaponEquipmentCandidate& target, const ImportedEquipmentItem& item, const int score)
    {
        if (item.refId.empty() || item.count <= 0)
            return;

        if (target.item.refId.empty() || score > target.score)
        {
            target.item = item;
            target.score = score;
        }
    }

    bool canActorUsePassiveEquipment(const IndexedRecordRow& actor, const ImportedEquipmentMetadata& metadata)
    {
        if (actor.recordName.toInt() != ESM::REC_CREA)
            return true;

        // OpenMW creatures do not use humanoid clothing/body armor. Keep their initial
        // derived equipment limited to shield/weapon-style slots until server-side
        // creature-specific auto-equip can mirror MWMechanics exactly.
        if (metadata.kind == ImportedEquipmentKind::Armor)
            return metadata.slots.size() == 1 && metadata.slots.front() == EquipmentSlot::CarriedLeft;

        return metadata.kind == ImportedEquipmentKind::Weapon;
    }

    void deriveActorEquipmentForWinningRows(std::map<std::string, IndexedRecordRow>& winningRows)
    {
        std::map<std::string, const IndexedRecordRow*> equippableItems;
        for (const auto& [_, row] : winningRows)
        {
            if (!isDeletedRecord(row) && row.identity.available && row.itemEquipmentImported
                && !row.itemEquipment.slots.empty())
                equippableItems[row.identity.recordKey] = &row;
        }

        for (auto& [_, actor] : winningRows)
        {
            if (!actor.actorInventoryImported || isDeletedRecord(actor) || !actor.identity.available)
                continue;

            std::array<EquipmentSlotCandidate, mwmp::equipmentSlotCount> selectedSlots;
            std::vector<WeaponEquipmentCandidate> weaponCandidates;
            WeaponEquipmentCandidate bestArrow;
            WeaponEquipmentCandidate bestBolt;

            for (const ImportedInventoryItem& inventoryItem : actor.actorInventory)
            {
                if (inventoryItem.refId.empty() || inventoryItem.count <= 0)
                    continue;

                const auto itemIt = equippableItems.find(normalizedLookupKey(inventoryItem.refId));
                if (itemIt == equippableItems.end())
                    continue;

                const IndexedRecordRow& itemRow = *itemIt->second;
                const ImportedEquipmentMetadata& metadata = itemRow.itemEquipment;
                if (!canActorUsePassiveEquipment(actor, metadata))
                    continue;

                ImportedEquipmentItem equipmentItem = makeEquippedItem(inventoryItem, metadata);
                if (equipmentItem.count <= 0)
                    continue;

                const int score = equipmentScore(metadata);
                if (metadata.kind == ImportedEquipmentKind::Weapon)
                {
                    if (metadata.weaponType == ESM::Weapon::Arrow)
                        assignAmmoCandidate(bestArrow, equipmentItem, score);
                    else if (metadata.weaponType == ESM::Weapon::Bolt)
                        assignAmmoCandidate(bestBolt, equipmentItem, score);
                    else
                    {
                        WeaponEquipmentCandidate candidate;
                        candidate.item = equipmentItem;
                        candidate.weaponType = metadata.weaponType;
                        candidate.requiredAmmoType = requiredAmmoTypeForWeapon(metadata.weaponType);
                        candidate.score = score;
                        weaponCandidates.push_back(std::move(candidate));
                    }
                    continue;
                }

                assignEquipmentSlot(selectedSlots, metadata.slots, equipmentItem, score);
            }

            std::sort(weaponCandidates.begin(), weaponCandidates.end(),
                [](const WeaponEquipmentCandidate& left, const WeaponEquipmentCandidate& right) {
                    if (left.score != right.score)
                        return left.score > right.score;
                    return left.item.refId < right.item.refId;
                });

            for (const WeaponEquipmentCandidate& candidate : weaponCandidates)
            {
                if (candidate.requiredAmmoType == ESM::Weapon::Arrow && bestArrow.item.refId.empty())
                    continue;
                if (candidate.requiredAmmoType == ESM::Weapon::Bolt && bestBolt.item.refId.empty())
                    continue;

                selectedSlots[EquipmentSlot::CarriedRight].assigned = true;
                selectedSlots[EquipmentSlot::CarriedRight].item = candidate.item;
                selectedSlots[EquipmentSlot::CarriedRight].score = candidate.score;

                if (candidate.requiredAmmoType == ESM::Weapon::Arrow)
                {
                    selectedSlots[EquipmentSlot::Ammunition].assigned = true;
                    selectedSlots[EquipmentSlot::Ammunition].item = bestArrow.item;
                    selectedSlots[EquipmentSlot::Ammunition].score = bestArrow.score;
                }
                else if (candidate.requiredAmmoType == ESM::Weapon::Bolt)
                {
                    selectedSlots[EquipmentSlot::Ammunition].assigned = true;
                    selectedSlots[EquipmentSlot::Ammunition].item = bestBolt.item;
                    selectedSlots[EquipmentSlot::Ammunition].score = bestBolt.score;
                }
                break;
            }

            actor.actorEquipmentItemCount = 0;
            for (int slot = 0; slot < mwmp::equipmentSlotCount; ++slot)
            {
                if (!selectedSlots[slot].assigned)
                    continue;

                actor.actorEquipment[slot] = selectedSlots[slot].item;
                ++actor.actorEquipmentItemCount;
            }
            actor.actorEquipmentImported = actor.actorEquipmentItemCount != 0;
        }
    }

    void appendActorEquipmentRows(std::string& result, const IndexedRecordRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        if (!row.actorEquipmentImported || isDeletedRecord(row) || !row.identity.available)
            return;

        ++stats.actorEquipmentRecordCount;
        for (int slot = 0; slot < mwmp::equipmentSlotCount; ++slot)
        {
            const ImportedEquipmentItem& item = row.actorEquipment[slot];
            if (item.refId.empty() || item.count <= 0)
                continue;

            result.push_back('{');
            appendJsonStringField(result, "schema", actorEquipmentRowSchema);
            result.push_back(',');
            appendJsonStringField(result, "recordKey", row.identity.recordKey);
            result.push_back(',');
            appendJsonStringField(result, "recordId", row.identity.recordId);
            result.push_back(',');
            appendJsonStringField(result, "sourceFile", row.contentFile);
            result.push_back(',');
            appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
            result.push_back(',');
            appendJsonNumberField(result, "recordIndex", row.recordIndex);
            result.push_back(',');
            appendJsonNumberField(result, "slot", slot);
            result.push_back(',');
            appendJsonStringField(result, "itemRefId", item.refId);
            result.push_back(',');
            appendJsonNumberField(result, "count", item.count);
            result.push_back(',');
            appendJsonNumberField(result, "charge", item.charge);
            result.push_back(',');
            appendJsonNumberField(result, "enchantmentCharge", item.enchantmentCharge);
            result += "}\n";
            ++stats.actorEquipmentItemCount;
        }
    }

    RecordIndexTables buildRecordIndexTablesJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        RecordIndexTables result;
        result.recordIndexJsonl.reserve(contentFiles.size() * 2048);
        result.recordWinnersJsonl.reserve(contentFiles.size() * 1024);

        std::map<std::string, IndexedRecordRow> winningRows;

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
                esm.setEncoder(&encoder);
                esm.setIndex(static_cast<int>(engineContentIndex));
                esm.open(collections.getPath(contentFile));

                std::size_t recordIndex = 1;
                std::string currentDialogueId;
                while (esm.hasMoreRecs())
                {
                    const std::size_t recordOffset = esm.getFileOffset();
                    const ESM::NAME recordName = esm.getRecName();
                    std::uint32_t flags = 0;
                    esm.getRecHeader(flags);
                    const std::size_t dataOffset = esm.getFileOffset();

                    IndexedRecordRow row;
                    row.engineContentIndex = engineContentIndex;
                    row.contentFile = contentFile;
                    row.recordIndex = recordIndex++;
                    row.recordName = recordName;
                    row.flags = flags;
                    row.recordOffset = recordOffset;
                    row.dataOffset = dataOffset;
                    if ((flags & ESM::FLAG_Ignored) != 0
                        || (!loadActorRecordRowData(esm, recordName, row)
                            && !loadEquippableItemRecordRowData(esm, recordName, row)
                            && !loadContainerRecordRowData(esm, recordName, row)
                            && !loadPathgridRecordRowData(esm, recordName, row)))
                    {
                        row.identity = extractRecordIdentity(esm, recordName, flags, currentDialogueId);
                        esm.skipRecord();
                    }
                    else
                        currentDialogueId.clear();
                    const std::size_t nextRecordOffset = esm.getFileOffset();
                    row.dataSize = nextRecordOffset >= dataOffset ? nextRecordOffset - dataOffset : 0;

                    appendRecordIndexRow(result.recordIndexJsonl, row);
                    ++stats.recordIndexCount;

                    if (row.identity.available)
                    {
                        ++stats.recordKeyCount;
                        if ((flags & ESM::FLAG_Ignored) == 0)
                        {
                            std::string winnerKey(row.recordName.toStringView());
                            winnerKey.push_back('\x1f');
                            winnerKey += row.identity.recordKey;
                            winningRows[std::move(winnerKey)] = row;
                        }
                    }
                    else
                        ++stats.recordUnkeyedCount;
                }
            }
            catch (const std::exception& e)
            {
                ++stats.recordImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
        }

        deriveActorEquipmentForWinningRows(winningRows);
        for (const auto& [_, row] : winningRows)
        {
            appendRecordWinnerRow(result.recordWinnersJsonl, row);
            appendActorProfileRows(result.actorProfileJsonl, row, stats);
            appendActorAiPackageRows(result.actorAiPackagesJsonl, row, stats);
            appendActorInventoryRows(result.actorInventoryJsonl, row, stats);
            appendActorSpellbookRows(result.actorSpellbookJsonl, row, stats);
            appendActorStatsDynamicRows(result.actorStatsDynamicJsonl, row, stats);
            appendActorEquipmentRows(result.actorEquipmentJsonl, row, stats);
            appendContainerInventoryRows(result.containerInventoryJsonl, row, stats);
            appendPathgridRows(result, row, stats);
            if (!isDeletedRecord(row) && row.identity.available && row.itemEquipmentImported
                && !row.itemEquipment.slots.empty())
                ++stats.itemEquipmentRecordCount;
            ++stats.recordWinnerCount;
            if (isDeletedRecord(row))
                ++stats.recordWinnerDeletedCount;
        }

        return result;
    }

    struct CellReferenceRow
    {
        std::size_t engineContentIndex = 0;
        std::string contentFile;
        std::size_t cellRecordIndex = 0;
        std::size_t referenceOrder = 0;
        std::string sourceCellKey;
        std::string effectiveCellKey;
        bool moved = false;
        bool deleted = false;
        int movedTargetX = 0;
        int movedTargetY = 0;
        ESM::CellRef ref;
    };

    struct CellWorldTables
    {
        std::string cellsJsonl;
        std::string cellReferencesJsonl;
        std::string cellReferenceWinnersJsonl;
    };

    std::string cellKeyForGrid(const int x, const int y)
    {
        return "exterior:" + std::to_string(x) + "," + std::to_string(y);
    }

    std::string cellKeyForCell(const ESM::Cell& cell)
    {
        if ((cell.mData.mFlags & ESM::Cell::Interior) != 0)
            return "interior:" + normalizedLookupKey(cell.mName);

        return cellKeyForGrid(cell.mData.mX, cell.mData.mY);
    }

    std::string refNumKey(const ESM::RefNum& refNum)
    {
        return "refnum:" + std::to_string(refNum.mContentFile) + ":" + std::to_string(refNum.mIndex);
    }

    std::string finiteJsonNumber(const float value)
    {
        if (!std::isfinite(value))
            return "null";

        std::ostringstream stream;
        stream << std::setprecision(9) << value;
        return stream.str();
    }

    void appendJsonFloatField(std::string& result, std::string_view name, const float value)
    {
        appendJsonRawField(result, name, finiteJsonNumber(value));
    }

    void appendPositionFields(std::string& result, const std::string_view prefix, const ESM::Position& position)
    {
        appendJsonFloatField(result, std::string(prefix) + "PosX", position.pos[0]);
        result.push_back(',');
        appendJsonFloatField(result, std::string(prefix) + "PosY", position.pos[1]);
        result.push_back(',');
        appendJsonFloatField(result, std::string(prefix) + "PosZ", position.pos[2]);
        result.push_back(',');
        appendJsonFloatField(result, std::string(prefix) + "RotX", position.rot[0]);
        result.push_back(',');
        appendJsonFloatField(result, std::string(prefix) + "RotY", position.rot[1]);
        result.push_back(',');
        appendJsonFloatField(result, std::string(prefix) + "RotZ", position.rot[2]);
    }

    bool isCellRecordDeleted(const std::uint32_t recordFlags, const bool cellDeletedSubrecord)
    {
        return (recordFlags & ESM::FLAG_Deleted) != 0 || cellDeletedSubrecord;
    }

    void appendCellRecordRow(std::string& result, const std::size_t engineContentIndex,
        const std::string& contentFile, const std::size_t recordIndex, const std::uint32_t recordFlags,
        const bool cellDeletedSubrecord, const ESM::Cell& cell, const std::string& cellKey,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", cellRecordRowSchema);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "recordIndex", recordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "recordFlags", recordFlags);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", isCellRecordDeleted(recordFlags, cellDeletedSubrecord));
        result.push_back(',');
        appendJsonBoolField(result, "recordFlagDeleted", (recordFlags & ESM::FLAG_Deleted) != 0);
        result.push_back(',');
        appendJsonBoolField(result, "deleteSubrecord", cellDeletedSubrecord);
        result.push_back(',');
        appendJsonStringField(result, "cellKey", cellKey);
        result.push_back(',');
        appendJsonStringField(result, "cellName", cell.mName);
        result.push_back(',');
        appendJsonBoolField(result, "interior", (cell.mData.mFlags & ESM::Cell::Interior) != 0);
        result.push_back(',');
        appendJsonNumberField(result, "gridX", cell.mData.mX);
        result.push_back(',');
        appendJsonNumberField(result, "gridY", cell.mData.mY);
        result.push_back(',');
        appendJsonNumberField(result, "cellFlags", cell.mData.mFlags);
        result.push_back(',');
        appendJsonStringField(result, "region", refIdToString(cell.mRegion));
        result.push_back(',');
        appendJsonBoolField(result, "hasWaterHeight", cell.mHasWaterHeightSub);
        result.push_back(',');
        appendJsonFloatField(result, "water", cell.mWater);
        result.push_back(',');
        appendJsonNumberField(result, "mapColor", cell.mMapColor);
        result.push_back(',');
        appendJsonNumberField(result, "refNumCounter", cell.mRefNumCounter);
        result.push_back(',');
        appendJsonBoolField(result, "hasAmbient", cell.mHasAmbi);
        result.push_back(',');
        appendJsonNumberField(result, "ambientColor", cell.mAmbi.mAmbient);
        result.push_back(',');
        appendJsonNumberField(result, "sunlightColor", cell.mAmbi.mSunlight);
        result.push_back(',');
        appendJsonNumberField(result, "fogColor", cell.mAmbi.mFog);
        result.push_back(',');
        appendJsonFloatField(result, "fogDensity", cell.mAmbi.mFogDensity);
        result += "}\n";
        ++stats.cellRecordCount;
    }

    void appendCellReferenceCommonFields(std::string& result, const CellReferenceRow& row)
    {
        appendJsonStringField(result, "refKey", refNumKey(row.ref.mRefNum));
        result.push_back(',');
        appendJsonNumberField(result, "refNumContentFile", row.ref.mRefNum.mContentFile);
        result.push_back(',');
        appendJsonNumberField(result, "refNumIndex", row.ref.mRefNum.mIndex);
        result.push_back(',');
        appendJsonStringField(result, "refId", refIdToString(row.ref.mRefID));
        result.push_back(',');
        appendJsonStringField(result, "sourceCellKey", row.sourceCellKey);
        result.push_back(',');
        appendJsonStringField(result, "effectiveCellKey", row.effectiveCellKey);
        result.push_back(',');
        appendJsonBoolField(result, "moved", row.moved);
        result.push_back(',');
        appendJsonBoolField(result, "deleted", row.deleted);
        result.push_back(',');
        appendJsonNumberField(result, "movedTargetX", row.movedTargetX);
        result.push_back(',');
        appendJsonNumberField(result, "movedTargetY", row.movedTargetY);
        result.push_back(',');
        appendJsonNumberField(result, "count", row.ref.mCount);
        result.push_back(',');
        appendJsonFloatField(result, "scale", row.ref.mScale);
        result.push_back(',');
        appendPositionFields(result, "", row.ref.mPos);
        result.push_back(',');
        appendJsonStringField(result, "owner", refIdToString(row.ref.mOwner));
        result.push_back(',');
        appendJsonStringField(result, "globalVariable", row.ref.mGlobalVariable);
        result.push_back(',');
        appendJsonStringField(result, "soul", refIdToString(row.ref.mSoul));
        result.push_back(',');
        appendJsonStringField(result, "faction", refIdToString(row.ref.mFaction));
        result.push_back(',');
        appendJsonNumberField(result, "factionRank", row.ref.mFactionRank);
        result.push_back(',');
        appendJsonNumberField(result, "chargeInt", row.ref.mChargeInt);
        result.push_back(',');
        appendJsonFloatField(result, "chargeIntRemainder", row.ref.mChargeIntRemainder);
        result.push_back(',');
        appendJsonFloatField(result, "enchantmentCharge", row.ref.mEnchantmentCharge);
        result.push_back(',');
        appendJsonBoolField(result, "teleport", row.ref.mTeleport);
        result.push_back(',');
        appendJsonStringField(result, "destCell", row.ref.mDestCell);
        result.push_back(',');
        appendPositionFields(result, "doorDest", row.ref.mDoorDest);
        result.push_back(',');
        appendJsonNumberField(result, "lockLevel", row.ref.mLockLevel);
        result.push_back(',');
        appendJsonBoolField(result, "locked", row.ref.mIsLocked);
        result.push_back(',');
        appendJsonStringField(result, "key", refIdToString(row.ref.mKey));
        result.push_back(',');
        appendJsonStringField(result, "trap", refIdToString(row.ref.mTrap));
        result.push_back(',');
        appendJsonNumberField(result, "referenceBlocked", static_cast<int>(row.ref.mReferenceBlocked));
    }

    void appendCellReferenceRow(std::string& result, const CellReferenceRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", cellReferenceRowSchema);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", row.contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "cellRecordIndex", row.cellRecordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "referenceOrder", row.referenceOrder);
        result.push_back(',');
        appendCellReferenceCommonFields(result, row);
        result += "}\n";

        ++stats.cellReferenceCount;
        if (row.moved)
            ++stats.cellReferenceMovedCount;
        if (row.deleted)
            ++stats.cellReferenceDeletedCount;
    }

    void appendCellReferenceWinnerRow(std::string& result, const CellReferenceRow& row,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        result.push_back('{');
        appendJsonStringField(result, "schema", cellReferenceWinnerRowSchema);
        result.push_back(',');
        appendJsonNumberField(result, "loadOrderIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonNumberField(result, "engineContentIndex", row.engineContentIndex);
        result.push_back(',');
        appendJsonStringField(result, "sourceFile", row.contentFile);
        result.push_back(',');
        appendJsonNumberField(result, "cellRecordIndex", row.cellRecordIndex);
        result.push_back(',');
        appendJsonNumberField(result, "referenceOrder", row.referenceOrder);
        result.push_back(',');
        appendCellReferenceCommonFields(result, row);
        result.push_back(',');
        appendJsonBoolField(result, "tombstone", row.deleted);
        result.push_back(',');
        appendJsonStringField(result, "loadOrderRule", laterContentEntryDominatesLoadOrderRule);
        result += "}\n";

        ++stats.cellReferenceWinnerCount;
        if (row.deleted)
            ++stats.cellReferenceWinnerDeletedCount;
    }

    CellWorldTables buildCellWorldTablesJsonl(const std::vector<std::filesystem::path>& dataDirs,
        const std::vector<std::string>& contentFiles, const std::string& encoding,
        mwmp::ServerContentDatabaseStatistics& stats)
    {
        Files::Collections collections(dataDirs);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(encoding));
        ESM::ReadersCache readers(contentFiles.size() + 1);

        CellWorldTables result;
        result.cellsJsonl.reserve(contentFiles.size() * 4096);
        result.cellReferencesJsonl.reserve(contentFiles.size() * 16384);
        result.cellReferenceWinnersJsonl.reserve(contentFiles.size() * 8192);

        std::map<std::string, CellReferenceRow> winningReferences;

        for (std::size_t engineContentIndex = 0; engineContentIndex < contentFiles.size(); ++engineContentIndex)
        {
            const std::string& contentFile = contentFiles[engineContentIndex];
            if (contentFile.empty() || isBuiltinContentFile(contentFile) || !isEsmLikeContentFile(contentFile))
                continue;

            try
            {
                const ESM::ReadersCache::BusyItem reader = readers.get(engineContentIndex);
                reader->setEncoder(&encoder);
                reader->setIndex(static_cast<int>(engineContentIndex));
                if (!reader->isOpen())
                    reader->open(collections.getPath(contentFile));
                reader->resolveParentFileIndices(readers);

                std::size_t recordIndex = 1;
                while (reader->hasMoreRecs())
                {
                    const ESM::NAME recordName = reader->getRecName();
                    std::uint32_t recordFlags = 0;
                    reader->getRecHeader(recordFlags);

                    if ((recordFlags & ESM::FLAG_Ignored) != 0 || recordName.toInt() != ESM::REC_CELL)
                    {
                        reader->skipRecord();
                        ++recordIndex;
                        continue;
                    }

                    ESM::Cell cell;
                    bool cellDeletedSubrecord = false;
                    cell.loadNameAndData(*reader, cellDeletedSubrecord);
                    const std::string sourceCellKey = cellKeyForCell(cell);
                    cell.loadCell(*reader, false);

                    appendCellRecordRow(result.cellsJsonl, engineContentIndex, contentFile, recordIndex, recordFlags,
                        cellDeletedSubrecord, cell, sourceCellKey, stats);

                    std::size_t referenceOrder = 0;
                    while (reader->hasMoreSubs())
                    {
                        ESM::CellRef ref;
                        bool deleted = false;
                        ESM::MovedCellRef movedRef{};
                        bool moved = false;
                        if (!ESM::Cell::getNextRef(
                                *reader, ref, deleted, movedRef, moved, ESM::Cell::GetNextRefMode::LoadAll))
                            break;

                        CellReferenceRow row;
                        row.engineContentIndex = engineContentIndex;
                        row.contentFile = contentFile;
                        row.cellRecordIndex = recordIndex;
                        row.referenceOrder = referenceOrder++;
                        row.sourceCellKey = sourceCellKey;
                        row.moved = moved;
                        row.deleted = deleted;
                        row.ref = std::move(ref);

                        if (moved)
                        {
                            row.movedTargetX = movedRef.mTarget[0];
                            row.movedTargetY = movedRef.mTarget[1];
                            row.effectiveCellKey = cellKeyForGrid(movedRef.mTarget[0], movedRef.mTarget[1]);
                        }
                        else
                            row.effectiveCellKey = sourceCellKey;

                        appendCellReferenceRow(result.cellReferencesJsonl, row, stats);
                        winningReferences[refNumKey(row.ref.mRefNum)] = std::move(row);
                    }

                    reader->skipRecord();
                    ++recordIndex;
                }
            }
            catch (const std::exception& e)
            {
                ++stats.cellImportErrorCount;
                if (stats.lastError.empty())
                    stats.lastError = e.what();
            }
        }

        for (const auto& [_, row] : winningReferences)
            appendCellReferenceWinnerRow(result.cellReferenceWinnersJsonl, row, stats);

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
        const std::vector<std::string>& contentFiles, const std::vector<std::string>& archives,
        const std::string& encoding,
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
        mStats.tableCount = 21;

        try
        {
            ServerContentDatabaseStatistics newStats = mStats;
            const std::string dataDirsJsonl = buildDataDirsJsonl(dataDirs, newStats);
            const std::string loadOrderJsonl = buildLoadOrderJsonl(contentFiles, newStats);
            const std::string contentFilesJsonl
                = buildContentFilesJsonl(dataDirs, contentFiles, dataFileRequirements, newStats);
            const std::string assetProvidersJsonl = buildAssetProvidersJsonl(dataDirs, archives, newStats);
            const std::string archiveFilesJsonl = buildArchiveFilesJsonl(dataDirs, archives, encoding, newStats);
            const std::string resolvedAssetsJsonl = buildResolvedAssetsJsonl(dataDirs, archives, encoding, newStats);
            const RecordIndexTables recordIndexTables
                = buildRecordIndexTablesJsonl(dataDirs, contentFiles, encoding, newStats);
            const CellWorldTables cellWorldTables = buildCellWorldTablesJsonl(dataDirs, contentFiles, encoding, newStats);
            const std::string questSourcesJsonl = buildQuestSourcesJsonl(dataDirs, contentFiles, encoding, newStats);
            const GeneratedQuestDbTables generatedQuestDb
                = buildGeneratedQuestDatabasePackage(dataDirs, contentFiles, encoding, newStats);
            const std::string manifestJson = buildManifestJson(newStats);

            bool changed = false;
            changed = writeIfChanged(newStats.rootPath / "data_dirs.jsonl", dataDirsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "load_order.jsonl", loadOrderJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "content_files.jsonl", contentFilesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "asset_providers.jsonl", assetProvidersJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "archive_files.jsonl", archiveFilesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "resolved_assets.jsonl", resolvedAssetsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "record_index.jsonl", recordIndexTables.recordIndexJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "record_winners.jsonl", recordIndexTables.recordWinnersJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_profile.jsonl",
                recordIndexTables.actorProfileJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_ai_packages.jsonl",
                recordIndexTables.actorAiPackagesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_inventory.jsonl",
                recordIndexTables.actorInventoryJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_spellbook.jsonl",
                recordIndexTables.actorSpellbookJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_stats_dynamic.jsonl",
                recordIndexTables.actorStatsDynamicJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "actor_equipment.jsonl",
                recordIndexTables.actorEquipmentJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "container_inventory.jsonl",
                recordIndexTables.containerInventoryJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "pathgrid_points.jsonl",
                recordIndexTables.pathgridPointsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "pathgrid_edges.jsonl",
                recordIndexTables.pathgridEdgesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "cells.jsonl", cellWorldTables.cellsJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "cell_references.jsonl", cellWorldTables.cellReferencesJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "cell_reference_winners.jsonl",
                cellWorldTables.cellReferenceWinnersJsonl) || changed;
            changed = writeIfChanged(newStats.rootPath / "quest_sources.jsonl", questSourcesJsonl) || changed;
            changed = writeIfChanged(newStats.manifestPath, manifestJson) || changed;
            changed = writeGeneratedQuestDatabasePackage(newStats.generatedQuestDatabasePath, generatedQuestDb) || changed;

            newStats.changed = changed;
            newStats.available = true;
            mStats = std::move(newStats);
            WorldDatabaseStore::get().loadFromDirectory(mStats.rootPath);
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
