#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <boost/program_options.hpp>

#include <osg/Math>

#include <components/esm/format.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadinfo.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/openfile.hpp>
#include <components/misc/strings/algorithm.hpp>

#include "arguments.hpp"
#include "labels.hpp"
#include "record.hpp"
#include "tes4.hpp"

namespace
{

    using namespace EsmTool;

    constexpr unsigned majorVersion = 1;
    constexpr unsigned minorVersion = 3;

    // Create a local alias for brevity
    namespace bpo = boost::program_options;

    struct ESMData
    {
        ESM::Header mHeader;
        std::deque<std::unique_ptr<EsmTool::RecordBase>> mRecords;
        // Value: (Reference, Deleted flag)
        std::map<ESM::Cell*, std::deque<std::pair<ESM::CellRef, bool>>> mCellRefs;
        std::map<int, int> mRecordStats;
    };

    bool parseOptions(int argc, char** argv, Arguments& info)
    {
        bpo::options_description desc(R"(Inspect and extract from Morrowind ES files (ESM, ESP, ESS)
Syntax: esmtool [options] mode infile [outfile]
Allowed modes:
  dump   Dumps all readable data from the input file.
  clone  Clones the input file to the output file.
  comp   Compares the given files.
  quest-export
         Exports dialogue and journal source records as CommunityMP JSONL.

Allowed options)");
        auto addOption = desc.add_options();
        addOption("help,h", "print help message.");
        addOption("version,v", "print version information and quit.");
        addOption("raw,r", bpo::value<std::string>(),
            "Show an unformatted list of all records and subrecords of given format:\n"
            "\n\tTES3"
            "\n\tTES4");
        // The intention is that this option would interact better
        // with other modes including clone, dump, and raw.
        addOption("type,t", bpo::value<std::vector<std::string>>(),
            "Show only records of this type (four character record code).  May "
            "be specified multiple times.  Only affects dump mode.");
        addOption("name,n", bpo::value<std::string>(), "Show only the record with this name.  Only affects dump mode.");
        addOption("plain,p",
            "Print contents of dialogs, books and scripts. "
            "(skipped by default) "
            "Only affects dump mode.");
        addOption("quiet,q", "Suppress all record information. Useful for speed tests.");
        addOption("loadcells,C", "Browse through contents of all cells.");

        addOption("encoding,e", bpo::value<std::string>(&(info.encoding))->default_value("win1252"),
            "Character encoding used in ESMTool:\n"
            "\n\twin1250 - Central and Eastern European such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, "
            "Croatian, Serbian (Latin script), Romanian and Albanian languages\n"
            "\n\twin1251 - Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages\n"
            "\n\twin1252 - Western European (Latin) alphabet, used by default");

        std::string finalText
            = "\nIf no option is given, the default action is to parse all records in the archive\nand display "
              "diagnostic information.";

        // input-file is hidden and used as a positional argument
        bpo::options_description hidden("Hidden Options");
        auto addHiddenOption = hidden.add_options();
        addHiddenOption("mode,m", bpo::value<std::string>(), "esmtool mode");
        addHiddenOption("input-file,i", bpo::value<Files::MaybeQuotedPathContainer>(), "input file");

        bpo::positional_options_description p;
        p.add("mode", 1).add("input-file", 2);

        // there might be a better way to do this
        bpo::options_description all;
        all.add(desc).add(hidden);
        bpo::variables_map variables;

        try
        {
            bpo::parsed_options validOpts = bpo::command_line_parser(argc, argv).options(all).positional(p).run();

            bpo::store(validOpts, variables);
        }
        catch (std::exception& e)
        {
            std::cout << "ERROR parsing arguments: " << e.what() << std::endl;
            return false;
        }

        bpo::notify(variables);

        if (variables.count("help"))
        {
            std::cout << desc << finalText << std::endl;
            return false;
        }
        if (variables.count("version"))
        {
            std::cout << "ESMTool version " << majorVersion << '.' << minorVersion << std::endl;
            return false;
        }
        if (!variables.count("mode"))
        {
            std::cout << "No mode specified!\n\n" << desc << finalText << std::endl;
            return false;
        }

        if (variables.count("type") > 0)
            info.types = variables["type"].as<std::vector<std::string>>();
        if (variables.count("name") > 0)
            info.name = variables["name"].as<std::string>();

        info.mode = variables["mode"].as<std::string>();
        if (!(info.mode == "dump" || info.mode == "clone" || info.mode == "comp" || info.mode == "quest-export"))
        {
            std::cout << "\nERROR: invalid mode \"" << info.mode << "\"\n\n" << desc << finalText << std::endl;
            return false;
        }

        if (!variables.count("input-file"))
        {
            std::cout << "\nERROR: missing ES file\n\n";
            std::cout << desc << finalText << std::endl;
            return false;
        }

        // handling gracefully the user adding multiple files
        /*    if (variables["input-file"].as< std::vector<std::string> >().size() > 1)
              {
              std::cout << "\nERROR: more than one ES file specified\n\n";
              std::cout << desc << finalText << std::endl;
              return false;
              }*/

        const auto& inputFiles = variables["input-file"].as<Files::MaybeQuotedPathContainer>();
        info.filename = inputFiles[0].u8string(); // This call to u8string is redundant, but required to build on
                                                  // MSVC 14.26 due to implementation bugs.
        if (inputFiles.size() > 1)
            info.outname = inputFiles[1].u8string(); // This call to u8string is redundant, but required to build on
                                                     // MSVC 14.26 due to implementation bugs.

        if (const auto it = variables.find("raw"); it != variables.end())
            info.mRawFormat = ESM::parseFormat(it->second.as<std::string>());

        info.quiet_given = variables.count("quiet") != 0;
        info.loadcells_given = variables.count("loadcells") != 0;
        info.plain_given = variables.count("plain") != 0;

        // Font encoding settings
        info.encoding = variables["encoding"].as<std::string>();
        if (info.encoding != "win1250" && info.encoding != "win1251" && info.encoding != "win1252")
        {
            std::cout << info.encoding << " is not a valid encoding option.\n";
            info.encoding = "win1252";
        }
        if (info.mode == "quest-export")
            std::cerr << ToUTF8::encodingUsingMessage(info.encoding) << std::endl;
        else
            std::cout << ToUTF8::encodingUsingMessage(info.encoding) << std::endl;

        return true;
    }

    void loadCell(const Arguments& info, ESM::Cell& cell, ESM::ESMReader& esm, ESMData* data);

    int load(const Arguments& info, ESMData* data);
    int questExport(const Arguments& info);
    int clone(const Arguments& info);
    int comp(const Arguments& info);

}

int main(int argc, char** argv)
{
    try
    {
        Arguments info;
        if (!parseOptions(argc, argv, info))
            return 1;

        if (info.mode == "dump")
            return load(info, nullptr);
        else if (info.mode == "quest-export")
            return questExport(info);
        else if (info.mode == "clone")
            return clone(info);
        else if (info.mode == "comp")
            return comp(info);
        else
        {
            std::cout << "Invalid or no mode specified, dying horribly. Have a nice day." << std::endl;
            return 1;
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}

namespace
{

    void loadCell(const Arguments& info, ESM::Cell& cell, ESM::ESMReader& esm, ESMData* data)
    {
        bool quiet = (info.quiet_given || info.mode == "clone");
        bool save = (info.mode == "clone");

        // Skip back to the beginning of the reference list
        // FIXME: Changes to the references backend required to support multiple plugins have
        //  almost certainly broken this following line. I'll leave it as is for now, so that
        //  the compiler does not complain.
        cell.restore(esm, 0);

        // Loop through all the references
        ESM::CellRef ref;
        if (!quiet)
            std::cout << "  References:\n";

        bool deleted = false;
        ESM::MovedCellRef movedCellRef;
        bool moved = false;
        while (cell.getNextRef(esm, ref, deleted, movedCellRef, moved))
        {
            if (data != nullptr && save)
                data->mCellRefs[&cell].push_back(std::make_pair(ref, deleted));

            if (quiet)
                continue;

            std::cout << "  - Refnum: " << ref.mRefNum.mIndex << '\n';
            std::cout << "    ID: " << ref.mRefID << '\n';
            std::cout << "    Position: (" << ref.mPos.pos[0] << ", " << ref.mPos.pos[1] << ", " << ref.mPos.pos[2]
                      << ")\n";
            std::cout << "    Rotation: (" << osg::RadiansToDegrees(ref.mPos.rot[0]) << ", "
                      << osg::RadiansToDegrees(ref.mPos.rot[1]) << ", " << osg::RadiansToDegrees(ref.mPos.rot[2])
                      << ")\n";
            if (ref.mScale != 1.f)
                std::cout << "    Scale: " << ref.mScale << '\n';
            if (!ref.mOwner.empty())
                std::cout << "    Owner: " << ref.mOwner << '\n';
            if (!ref.mGlobalVariable.empty())
                std::cout << "    Global: " << ref.mGlobalVariable << '\n';
            if (!ref.mFaction.empty())
                std::cout << "    Faction: " << ref.mFaction << '\n';
            if (!ref.mFaction.empty() || ref.mFactionRank != -2)
                std::cout << "    Faction rank: " << ref.mFactionRank << '\n';
            std::cout << "    Enchantment charge: " << ref.mEnchantmentCharge << '\n';
            std::cout << "    Uses/health: " << ref.mChargeInt << '\n';
            std::cout << "    Count: " << ref.mCount << '\n';
            std::cout << "    Blocked: " << static_cast<int>(ref.mReferenceBlocked) << '\n';
            std::cout << "    Deleted: " << deleted << '\n';
            if (!ref.mKey.empty())
                std::cout << "    Key: " << ref.mKey << '\n';
            std::cout << "    Lock level: " << ref.mLockLevel << '\n';
            if (!ref.mTrap.empty())
                std::cout << "    Trap: " << ref.mTrap << '\n';
            if (!ref.mSoul.empty())
                std::cout << "    Soul: " << ref.mSoul << '\n';
            if (ref.mTeleport)
            {
                std::cout << "    Destination position: (" << ref.mDoorDest.pos[0] << ", " << ref.mDoorDest.pos[1]
                          << ", " << ref.mDoorDest.pos[2] << ")\n";
                std::cout << "    Destination rotation: (" << osg::RadiansToDegrees(ref.mDoorDest.rot[0]) << ", "
                          << osg::RadiansToDegrees(ref.mDoorDest.rot[1]) << ", "
                          << osg::RadiansToDegrees(ref.mDoorDest.rot[2]) << ")\n";
                if (!ref.mDestCell.empty())
                    std::cout << "    Destination cell: " << ref.mDestCell << '\n';
            }
            std::cout << "    Moved: " << std::boolalpha << moved << std::noboolalpha << '\n';
            if (moved)
            {
                std::cout << "    Moved refnum: " << movedCellRef.mRefNum.mIndex << '\n';
                std::cout << "    Moved content file: " << movedCellRef.mRefNum.mContentFile << '\n';
                std::cout << "    Target: " << movedCellRef.mTarget[0] << ", " << movedCellRef.mTarget[1] << '\n';
            }
        }
    }

    void printRawTes3(const std::filesystem::path& path)
    {
        std::cout << "TES3 RAW file listing: " << Files::pathToUnicodeString(path) << '\n';
        ESM::ESMReader esm;
        esm.openRaw(path);
        while (esm.hasMoreRecs())
        {
            ESM::NAME n = esm.getRecName();
            std::cout << "Record: " << n.toStringView() << '\n';
            esm.getRecHeader();
            while (esm.hasMoreSubs())
            {
                size_t offs = esm.getFileOffset();
                esm.getSubName();
                esm.skipHSub();
                n = esm.retSubName();
                std::ios::fmtflags f(std::cout.flags());
                std::cout << "    " << n.toStringView() << " - " << esm.getSubSize() << " bytes @ 0x" << std::hex
                          << offs << '\n';
                std::cout.flags(f);
            }
        }
    }

    std::string makeQuestPackageId(const std::filesystem::path& path)
    {
        std::string result = Files::pathToUnicodeString(path.stem());
        for (char& ch : result)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch))
                ch = static_cast<char>(std::tolower(uch));
            else if (ch != '-' && ch != '_')
                ch = '_';
        }

        if (result.empty())
            return "content";

        return result;
    }

    std::string refIdToString(const ESM::RefId& refId)
    {
        if (refId.empty())
            return {};

        return refId.toString();
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

    void writeJsonString(std::ostream& stream, std::string_view value)
    {
        static constexpr char hex[] = "0123456789abcdef";

        stream << '"';
        for (const unsigned char ch : value)
        {
            switch (ch)
            {
                case '"':
                    stream << "\\\"";
                    break;
                case '\\':
                    stream << "\\\\";
                    break;
                case '\b':
                    stream << "\\b";
                    break;
                case '\f':
                    stream << "\\f";
                    break;
                case '\n':
                    stream << "\\n";
                    break;
                case '\r':
                    stream << "\\r";
                    break;
                case '\t':
                    stream << "\\t";
                    break;
                default:
                    if (ch < 0x20)
                        stream << "\\u00" << hex[ch >> 4] << hex[ch & 0xf];
                    else
                        stream << static_cast<char>(ch);
                    break;
            }
        }
        stream << '"';
    }

    void writeJsonField(std::ostream& stream, std::string_view name, std::string_view value)
    {
        writeJsonString(stream, name);
        stream << ':';
        writeJsonString(stream, value);
    }

    template <class T>
    void writeJsonNumberField(std::ostream& stream, std::string_view name, const T value)
    {
        writeJsonString(stream, name);
        stream << ':' << value;
    }

    void writeJsonBoolField(std::ostream& stream, std::string_view name, const bool value)
    {
        writeJsonString(stream, name);
        stream << ':' << (value ? "true" : "false");
    }

    void writeQuestExportHeader(
        std::ostream& stream, const Arguments& info, ESM::ESMReader& esm, std::string_view packageId)
    {
        stream << '{';
        writeJsonField(stream, "schema", "communitymp.quest.source.v1");
        stream << ',';
        writeJsonField(stream, "kind", "package");
        stream << ',';
        writeJsonField(stream, "packageId", packageId);
        stream << ',';
        writeJsonField(stream, "sourceFile", Files::pathToUnicodeString(info.filename));
        stream << ',';
        writeJsonField(stream, "sourceFileName", Files::pathToUnicodeString(info.filename.filename()));
        stream << ',';
        writeJsonField(stream, "author", esm.getAuthor());
        stream << ',';
        writeJsonField(stream, "description", esm.getDesc());
        stream << ',';
        writeJsonNumberField(stream, "esmVersion", esm.esmVersionF());
        stream << ',';
        writeJsonNumberField(stream, "recordCount", esm.getRecordCount());
        stream << ',';
        writeJsonString(stream, "masters");
        stream << ":[";
        bool firstMaster = true;
        for (const ESM::Header::MasterData& master : esm.getGameFiles())
        {
            if (!firstMaster)
                stream << ',';
            firstMaster = false;

            stream << '{';
            writeJsonField(stream, "name", master.name);
            stream << ',';
            writeJsonNumberField(stream, "size", master.size);
            stream << '}';
        }
        stream << "]}\n";
    }

    void writeQuestExportDialogue(std::ostream& stream, const ESM::Dialogue& dialogue, std::string_view packageId,
        const std::size_t recordIndex, const std::uint32_t flags, const bool isDeleted)
    {
        stream << '{';
        writeJsonField(stream, "schema", "communitymp.quest.source.v1");
        stream << ',';
        writeJsonField(stream, "kind", "dialogue");
        stream << ',';
        writeJsonField(stream, "packageId", packageId);
        stream << ',';
        writeJsonNumberField(stream, "recordIndex", recordIndex);
        stream << ',';
        writeJsonNumberField(stream, "recordFlags", flags);
        stream << ',';
        writeJsonBoolField(stream, "deleted", isDeleted);
        stream << ',';
        writeJsonField(stream, "dialogueId", refIdToString(dialogue.mId));
        stream << ',';
        writeJsonField(stream, "displayName", dialogue.mStringId);
        stream << ',';
        writeJsonNumberField(stream, "dialogueTypeCode", static_cast<int>(dialogue.mType));
        stream << ',';
        writeJsonField(stream, "dialogueType", dialogTypeLabel(static_cast<int>(dialogue.mType)));
        stream << "}\n";
    }

    void writeQuestExportCondition(std::ostream& stream, const ESM::DialogueCondition& condition)
    {
        stream << '{';
        writeJsonNumberField(stream, "index", static_cast<int>(condition.mIndex));
        stream << ',';
        writeJsonNumberField(stream, "functionCode", static_cast<int>(condition.mFunction));
        stream << ',';
        writeJsonField(stream, "function", ruleFunction(static_cast<int>(condition.mFunction)));
        stream << ',';
        writeJsonNumberField(stream, "comparisonCode", static_cast<int>(condition.mComparison));
        stream << ',';
        writeJsonField(stream, "comparison", comparisonLabel(condition.mComparison));
        stream << ',';
        writeJsonField(stream, "variable", condition.mVariable);
        stream << ',';
        writeJsonString(stream, "valueType");
        stream << ':';
        std::visit(
            [&](auto value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, float>)
                    writeJsonString(stream, "float");
                else
                    writeJsonString(stream, "int");
            },
            condition.mValue);
        stream << ',';
        writeJsonString(stream, "value");
        stream << ':';
        std::visit(
            [&](auto value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, float>)
                    stream << std::setprecision(9) << value;
                else
                    stream << value;
            },
            condition.mValue);
        stream << '}';
    }

    void writeQuestExportInfo(std::ostream& stream, const ESM::DialInfo& info, std::string_view packageId,
        const std::size_t recordIndex, const std::uint32_t flags, const bool isDeleted, const bool hasDialogue,
        const ESM::Dialogue& dialogue, const std::size_t infoOrder)
    {
        const bool isJournal = hasDialogue && dialogue.mType == ESM::Dialogue::Journal;

        stream << '{';
        writeJsonField(stream, "schema", "communitymp.quest.source.v1");
        stream << ',';
        writeJsonField(stream, "kind", "info");
        stream << ',';
        writeJsonField(stream, "packageId", packageId);
        stream << ',';
        writeJsonNumberField(stream, "recordIndex", recordIndex);
        stream << ',';
        writeJsonNumberField(stream, "recordFlags", flags);
        stream << ',';
        writeJsonNumberField(stream, "infoOrder", infoOrder);
        stream << ',';
        writeJsonBoolField(stream, "deleted", isDeleted);
        stream << ',';
        writeJsonBoolField(stream, "orphaned", !hasDialogue);
        stream << ',';
        writeJsonField(stream, "dialogueId", hasDialogue ? refIdToString(dialogue.mId) : std::string_view{});
        stream << ',';
        writeJsonField(stream, "dialogueType", hasDialogue ? dialogTypeLabel(static_cast<int>(dialogue.mType)) : "");
        stream << ',';
        writeJsonNumberField(stream, "dialogueTypeCode", hasDialogue ? static_cast<int>(dialogue.mType) : -1);
        stream << ',';
        writeJsonField(stream, "infoId", refIdToString(info.mId));
        stream << ',';
        writeJsonField(stream, "previousInfoId", refIdToString(info.mPrev));
        stream << ',';
        writeJsonField(stream, "nextInfoId", refIdToString(info.mNext));
        stream << ',';
        writeJsonNumberField(stream, "dataTypeCode", info.mData.mType);
        stream << ',';
        writeJsonNumberField(stream, "rank", static_cast<int>(info.mData.mRank));
        stream << ',';
        writeJsonNumberField(stream, "gender", static_cast<int>(info.mData.mGender));
        stream << ',';
        writeJsonNumberField(stream, "pcRank", static_cast<int>(info.mData.mPCrank));
        stream << ',';
        writeJsonField(stream, "dataValueKind", isJournal ? "journalIndex" : "disposition");
        stream << ',';
        writeJsonNumberField(stream, "dataValue", isJournal ? info.mData.mJournalIndex : info.mData.mDisposition);
        stream << ',';
        writeJsonField(stream, "actor", refIdToString(info.mActor));
        stream << ',';
        writeJsonField(stream, "race", refIdToString(info.mRace));
        stream << ',';
        writeJsonField(stream, "class", refIdToString(info.mClass));
        stream << ',';
        writeJsonField(stream, "faction", refIdToString(info.mFaction));
        stream << ',';
        writeJsonField(stream, "pcFaction", refIdToString(info.mPcFaction));
        stream << ',';
        writeJsonField(stream, "cell", refIdToString(info.mCell));
        stream << ',';
        writeJsonBoolField(stream, "factionLess", info.mFactionLess);
        stream << ',';
        writeJsonField(stream, "sound", info.mSound);
        stream << ',';
        writeJsonField(stream, "response", info.mResponse);
        stream << ',';
        writeJsonField(stream, "resultScript", info.mResultScript);
        stream << ',';
        writeJsonNumberField(stream, "questStatusCode", static_cast<int>(info.mQuestStatus));
        stream << ',';
        writeJsonField(stream, "questStatus", questStatusLabel(static_cast<int>(info.mQuestStatus)));
        stream << ',';
        writeJsonString(stream, "conditions");
        stream << ":[";
        bool firstCondition = true;
        for (const ESM::DialogueCondition& condition : info.mSelects)
        {
            if (!firstCondition)
                stream << ',';
            firstCondition = false;
            writeQuestExportCondition(stream, condition);
        }
        stream << "]}\n";
    }

    int questExportTes3(const Arguments& info, std::unique_ptr<std::ifstream>&& stream)
    {
        std::unique_ptr<std::ofstream> outputFile;
        std::ostream* output = &std::cout;
        if (!info.outname.empty())
        {
            outputFile = std::make_unique<std::ofstream>(info.outname, std::ios::binary);
            if (!outputFile->is_open())
            {
                std::cerr << "Failed to open output file " << Files::pathToUnicodeString(info.outname) << ": "
                          << std::generic_category().message(errno) << '\n';
                return 1;
            }
            output = outputFile.get();
        }

        std::cerr << "Exporting CommunityMP quest source records from TES3 file: "
                  << Files::pathToUnicodeString(info.filename) << '\n';

        ESM::ESMReader esm;
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(info.encoding));
        esm.setEncoder(&encoder);

        const std::string packageId = makeQuestPackageId(info.filename);
        ESM::Dialogue currentDialogue;
        bool hasCurrentDialogue = false;
        std::size_t recordIndex = 0;
        std::size_t dialogueCount = 0;
        std::size_t infoCount = 0;

        try
        {
            esm.open(std::move(stream), info.filename);
            writeQuestExportHeader(*output, info, esm, packageId);

            while (esm.hasMoreRecs())
            {
                const ESM::NAME name = esm.getRecName();
                std::uint32_t flags = 0;
                esm.getRecHeader(flags);
                ++recordIndex;

                if (name.toInt() == ESM::REC_DIAL)
                {
                    bool isDeleted = false;
                    currentDialogue.blank();
                    currentDialogue.load(esm, isDeleted);
                    hasCurrentDialogue = true;
                    ++dialogueCount;

                    writeQuestExportDialogue(*output, currentDialogue, packageId, recordIndex, flags, isDeleted);
                }
                else if (name.toInt() == ESM::REC_INFO)
                {
                    ESM::DialInfo infoRecord;
                    bool isDeleted = false;
                    infoRecord.load(esm, isDeleted);
                    ++infoCount;

                    writeQuestExportInfo(
                        *output, infoRecord, packageId, recordIndex, flags, isDeleted, hasCurrentDialogue,
                        currentDialogue, infoCount);
                }
                else
                {
                    hasCurrentDialogue = false;
                    esm.skipRecord();
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "\nERROR while exporting quest source records:\n\n  " << e.what() << std::endl;
            return 1;
        }

        std::cerr << "Exported " << dialogueCount << " dialogue records and " << infoCount
                  << " info records for package " << packageId << ".\n";

        return 0;
    }

    int questExport(const Arguments& info)
    {
        auto stream = Files::openBinaryInputFileStream(info.filename);
        if (!stream->is_open())
        {
            std::cerr << "Failed to open file " << Files::pathToUnicodeString(info.filename) << ": "
                      << std::generic_category().message(errno) << '\n';
            return 1;
        }

        const ESM::Format format = ESM::readFormat(*stream);
        stream->seekg(0);

        switch (format)
        {
            case ESM::Format::Tes3:
                return questExportTes3(info, std::move(stream));
            case ESM::Format::Tes4:
                std::cerr << "CommunityMP quest export does not yet support TES4 files: "
                          << Files::pathToUnicodeString(info.filename) << '\n';
                return 1;
        }

        std::cerr << "Unsupported ESM format: " << ESM::NAME(format).toStringView() << '\n';
        return 1;
    }

    int loadTes3(const Arguments& info, std::unique_ptr<std::ifstream>&& stream, ESMData* data)
    {
        std::cout << "Loading TES3 file: " << info.filename << '\n';

        ESM::ESMReader esm;
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(info.encoding));
        esm.setEncoder(&encoder);

        std::unordered_set<uint32_t> skipped;

        try
        {
            bool quiet = (info.quiet_given || info.mode == "clone");
            bool loadCells = (info.loadcells_given || info.mode == "clone");
            bool save = (info.mode == "clone");

            esm.open(std::move(stream), info.filename);

            if (data != nullptr)
                data->mHeader = esm.getHeader();

            if (!quiet)
            {
                std::cout << "Author: " << esm.getAuthor() << '\n'
                          << "Description: " << esm.getDesc() << '\n'
                          << "File format version: " << esm.esmVersionF() << '\n';
                std::vector<ESM::Header::MasterData> masterData = esm.getGameFiles();
                if (!masterData.empty())
                {
                    std::cout << "Masters:" << '\n';
                    for (const auto& master : masterData)
                        std::cout << "  " << master.name << ", " << master.size << " bytes\n";
                }
            }

            // Loop through all records
            while (esm.hasMoreRecs())
            {
                const ESM::NAME n = esm.getRecName();
                uint32_t flags;
                esm.getRecHeader(flags);

                auto record = EsmTool::RecordBase::create(n);
                if (record == nullptr)
                {
                    if (!quiet && skipped.count(n.toInt()) == 0)
                    {
                        std::cout << "Skipping " << n.toStringView() << " records.\n";
                        skipped.emplace(n.toInt());
                    }

                    esm.skipRecord();
                    if (quiet)
                        break;
                    std::cout << "  Skipping\n";

                    continue;
                }

                record->setFlags(static_cast<int>(flags));
                record->setPrintPlain(info.plain_given);
                record->load(esm);

                // Is the user interested in this record type?
                bool interested = true;
                if (!info.types.empty()
                    && std::find(info.types.begin(), info.types.end(), n.toStringView()) == info.types.end())
                    interested = false;

                if (!info.name.empty() && !Misc::StringUtils::ciEqual(info.name, record->getId()))
                    interested = false;

                if (!quiet && interested)
                {
                    std::cout << "\nRecord: " << n.toStringView() << " " << record->getId() << "\n"
                              << "Record flags: " << recordFlags(record->getFlags()) << '\n';
                    record->print();
                }

                if (record->getType().toInt() == ESM::REC_CELL && loadCells && interested)
                {
                    loadCell(info, record->cast<ESM::Cell>()->get(), esm, data);
                }

                if (data != nullptr)
                {
                    if (save)
                        data->mRecords.push_back(std::move(record));
                    ++data->mRecordStats[n.toInt()];
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "\nERROR:\n\n  " << e.what() << std::endl;
            if (data != nullptr)
                data->mRecords.clear();
            return 1;
        }

        return 0;
    }

    int load(const Arguments& info, ESMData* data)
    {
        if (info.mRawFormat.has_value() && info.mode == "dump")
        {
            switch (*info.mRawFormat)
            {
                case ESM::Format::Tes3:
                    printRawTes3(info.filename);
                    break;
                case ESM::Format::Tes4:
                    std::cout << "Printing raw TES4 file is not supported: "
                              << Files::pathToUnicodeString(info.filename) << "\n";
                    break;
            }
            return 0;
        }

        auto stream = Files::openBinaryInputFileStream(info.filename);
        if (!stream->is_open())
        {
            std::cout << "Failed to open file " << info.filename << ": " << std::generic_category().message(errno)
                      << '\n';
            return -1;
        }

        const ESM::Format format = ESM::readFormat(*stream);
        stream->seekg(0);

        switch (format)
        {
            case ESM::Format::Tes3:
                return loadTes3(info, std::move(stream), data);
            case ESM::Format::Tes4:
                if (data != nullptr)
                {
                    std::cout << "Collecting data from esm file is not supported for TES4\n";
                    return -1;
                }
                return loadTes4(info, std::move(stream));
        }

        std::cout << "Unsupported ESM format: " << ESM::NAME(format).toStringView() << '\n';

        return -1;
    }

    int clone(const Arguments& info)
    {
        if (info.outname.empty())
        {
            std::cout << "You need to specify an output name" << std::endl;
            return 1;
        }

        ESMData data;
        if (load(info, &data) != 0)
        {
            std::cout << "Failed to load, aborting." << std::endl;
            return 1;
        }

        size_t recordCount = data.mRecords.size();

        int digitCount = 1; // For a nicer output
        if (recordCount > 0)
            digitCount = static_cast<int>(std::log10(recordCount)) + 1;

        std::cout << "Loaded " << recordCount << " records:\n\n";

        int i = 0;
        for (std::pair<int, int> stat : data.mRecordStats)
        {
            ESM::NAME name;
            name = stat.first;
            int amount = stat.second;
            std::cout << std::setw(digitCount) << amount << " " << name.toStringView() << "  ";
            if (++i % 3 == 0)
                std::cout << '\n';
        }

        if (i % 3 != 0)
            std::cout << '\n';

        std::cout << "\nSaving records to: " << Files::pathToUnicodeString(info.outname) << "...\n";

        ESM::ESMWriter esm;
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(info.encoding));
        esm.setEncoder(&encoder);
        esm.setHeader(data.mHeader);
        esm.setVersion(ESM::VER_130);
        esm.setRecordCount(static_cast<int>(recordCount));

        std::fstream save(info.outname, std::fstream::out | std::fstream::binary);
        esm.save(save);

        int saved = 0;
        for (auto& record : data.mRecords)
        {
            if (i <= 0)
                break;

            const ESM::NAME typeName = record->getType();

            esm.startRecord(typeName, record->getFlags());

            record->save(esm);
            if (typeName.toInt() == ESM::REC_CELL)
            {
                ESM::Cell* ptr = &record->cast<ESM::Cell>()->get();
                if (!data.mCellRefs[ptr].empty())
                {
                    for (std::pair<ESM::CellRef, bool>& ref : data.mCellRefs[ptr])
                        ref.first.save(esm, ref.second);
                }
            }

            esm.endRecord(typeName);

            saved++;
            int perc = recordCount == 0 ? 100 : (int)((saved / (float)recordCount) * 100);
            if (perc % 10 == 0)
            {
                std::cerr << "\r" << perc << "%";
            }
        }

        std::cout << "\rDone!" << std::endl;

        esm.close();
        save.close();

        return 0;
    }

    int comp(const Arguments& info)
    {
        if (info.filename.empty() || info.outname.empty())
        {
            std::cout << "You need to specify two input files" << std::endl;
            return 1;
        }

        Arguments fileOne;
        Arguments fileTwo;

        fileOne.mode = "clone";
        fileTwo.mode = "clone";

        fileOne.encoding = info.encoding;
        fileTwo.encoding = info.encoding;

        fileOne.filename = info.filename;
        fileTwo.filename = info.outname;

        ESMData dataOne;
        if (load(fileOne, &dataOne) != 0)
        {
            std::cout << "Failed to load " << Files::pathToUnicodeString(info.filename) << ", aborting comparison."
                      << std::endl;
            return 1;
        }

        ESMData dataTwo;
        if (load(fileTwo, &dataTwo) != 0)
        {
            std::cout << "Failed to load " << Files::pathToUnicodeString(info.outname) << ", aborting comparison."
                      << std::endl;
            return 1;
        }

        if (dataOne.mRecords.size() != dataTwo.mRecords.size())
        {
            std::cout << "Not equal, different amount of records." << std::endl;
            return 1;
        }

        return 0;
    }

}
