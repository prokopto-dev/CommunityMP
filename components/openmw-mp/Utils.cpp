#include "Utils.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <memory>
#include <iostream>
#include <sstream>
#include <boost/crc.hpp>
#include <boost/filesystem/fstream.hpp>
#include <iomanip>
#include <array>
#include <variant>

#include <components/esm/attr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#ifdef _WIN32
int setenv(const char *name, const char *value, int overwrite)
{
    printf("%s: %s\n", name, value);
    return _putenv_s(name, value);
}
#endif


std::string Utils::convertPath(std::string str)
{
#if defined(_WIN32)
#define _SEP_ '\\'
#elif defined(__APPLE__)
#define _SEP_ ':'
#endif

#if defined(_WIN32) || defined(__APPLE__)
    replace(str.begin(), str.end(), '/', _SEP_);
#endif //defined(_WIN32) || defined(__APPLE__)
    return str;

#undef _SEP_
}

bool Utils::doesFileHaveChecksum(std::string filePath, unsigned int requiredChecksum)
{
    unsigned int fileChecksum = crc32Checksum(filePath);

    if (fileChecksum == requiredChecksum)
        return true;

    return false;
}

void Utils::timestamp()
{
    time_t ltime;
    ltime = time(0);
    char t[32];
    snprintf(t, sizeof(t), "[%s", asctime(localtime(&ltime)));
    char* newline = strchr(t, '\n');
    *newline = ']';
    strcat(t, " ");
    printf("%s", t);
}

// Based on http://stackoverflow.com/questions/1637587/c-libcurl-console-progress-bar
int Utils::progressFunc(double TotalToDownload, double NowDownloaded)
{
    // how wide you want the progress meter to be
    int totaldotz=40;
    double fractiondownloaded = NowDownloaded / TotalToDownload;
    // part of the progressmeter that's already "full"
    int dotz = round(fractiondownloaded * totaldotz);

    // create the "meter"
    int ii=0;
    printf("%3.0f%% [",fractiondownloaded*100);
    // part  that's full already
    for ( ; ii < dotz;ii++) {
        printf("=");
    }
    // remaining part (spaces)
    for ( ; ii < totaldotz;ii++) {
        printf(" ");
    }
    // and back to line begin - do not forget the fflush to avoid output buffering problems!
    printf("]\r");
    fflush(stdout);
    return 1;
}

bool Utils::compareDoubles(double a, double b, double epsilon)
{
    return fabs(a - b) < epsilon;
}

bool Utils::compareFloats(float a, float b, float epsilon)
{
    return fabs(a - b) < epsilon;
}

// Based on https://stackoverflow.com/a/1489873
unsigned int Utils::getNumberOfDigits(int integer)
{
    int digits = 0;
    if (integer < 0) digits = 1;
    while (integer) {
        integer /= 10;
        digits++;
    }
    return digits;
}



std::string Utils::toString(int num)
{
    std::ostringstream stream;
    stream << num;
    return stream.str();
}

std::string Utils::replaceString(const std::string& source, const char* find, const char* replace)
{
    unsigned int find_len = strlen(find);
    unsigned int replace_len = strlen(replace);
    size_t pos = 0;

    std::string dest = source;

    while ((pos = dest.find(find, pos)) != std::string::npos)
    {
        dest.replace(pos, find_len, replace);
        pos += replace_len;
    }

    return dest;
}

std::string& Utils::removeExtension(std::string& file)
{
    size_t pos = file.find_last_of('.');

    if (pos)
        file = file.substr(0, pos);

    return file;
}

long int Utils::getFileLength(const char* file)
{
    FILE* _file = fopen(file, "rb");

    if (!_file)
        return 0;

    fseek(_file, 0, SEEK_END);
    long int size = ftell(_file);
    fclose(_file);

    return size;
}

unsigned int ::Utils::crc32Checksum(const std::string &file)
{
    boost::crc_32_type  crc32;
    boost::filesystem::ifstream  ifs(file, std::ios_base::binary);
    if (ifs)
    {
        do
        {
            char buffer[1024];

            ifs.read(buffer, 1024);
            crc32.process_bytes( buffer, ifs.gcount() );
        } while (ifs);
    }
    return crc32.checksum();
}

std::string Utils::getOperatingSystemType()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux)
    return "Linux";
#elif defined(__APPLE__)
    return "OS X";
#else
    return "Unknown OS";
#endif
}

std::string Utils::getArchitectureType()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "64-bit";
#elif defined(__i386__) || defined(_M_I86) || defined(_M_IX86)
    return "32-bit";
#elif defined(__ARM_ARCH)
    std::string architectureType = "ARMv" + __ARM_ARCH;
#ifdef __aarch64__
    architectureType = architectureType + " 64-bit";
#else
    architectureType = architectureType + " 32-bit";
#endif
    return architectureType;
#else
    return "Unknown architecture";
#endif
}

std::string Utils::getVersionInfo(std::string appName, std::string version, std::string commitHash, int protocol)
{
    std::stringstream stream;

    stream << appName << " " << version << " (" << getOperatingSystemType() << " " << getArchitectureType() << ")" << std::endl;
    stream << "Protocol version: " << protocol << std::endl;
    stream << "Build commit hash: " << commitHash.substr(0, 10) << std::endl;
    stream << "------------------------------------------------------------" << std::endl;

    return stream.str();
}

void Utils::printWithWidth(std::ostringstream &sstr, std::string str, size_t width)
{
    sstr << std::left << std::setw(width) << std::setfill(' ') << str;
}

std::string Utils::intToHexStr(unsigned val)
{
    std::ostringstream sstr;
    sstr << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return sstr.str();
}

unsigned int Utils::hexStrToInt(std::string hexString)
{
    unsigned int intValue;
    sscanf(hexString.c_str(), "%x", &intValue);
    return intValue;
}

namespace
{
    bool isSummonEffect(ESM::RefId effectId)
    {
        static const std::array summonEffects{
            ESM::MagicEffect::SummonScamp,
            ESM::MagicEffect::SummonClannfear,
            ESM::MagicEffect::SummonDaedroth,
            ESM::MagicEffect::SummonDremora,
            ESM::MagicEffect::SummonAncestralGhost,
            ESM::MagicEffect::SummonSkeletalMinion,
            ESM::MagicEffect::SummonBonewalker,
            ESM::MagicEffect::SummonGreaterBonewalker,
            ESM::MagicEffect::SummonBonelord,
            ESM::MagicEffect::SummonWingedTwilight,
            ESM::MagicEffect::SummonHunger,
            ESM::MagicEffect::SummonGoldenSaint,
            ESM::MagicEffect::SummonFlameAtronach,
            ESM::MagicEffect::SummonFrostAtronach,
            ESM::MagicEffect::SummonStormAtronach,
            ESM::MagicEffect::SummonCenturionSphere,
            ESM::MagicEffect::SummonFabricant,
            ESM::MagicEffect::SummonWolf,
            ESM::MagicEffect::SummonBear,
            ESM::MagicEffect::SummonBonewolf,
            ESM::MagicEffect::SummonCreature04,
            ESM::MagicEffect::SummonCreature05,
        };

        return std::find(summonEffects.begin(), summonEffects.end(), effectId) != summonEffects.end();
    }

    bool affectsAttribute(ESM::RefId effectId)
    {
        static const std::array effects{
            ESM::MagicEffect::DrainAttribute,
            ESM::MagicEffect::DamageAttribute,
            ESM::MagicEffect::RestoreAttribute,
            ESM::MagicEffect::FortifyAttribute,
            ESM::MagicEffect::AbsorbAttribute,
        };

        return std::find(effects.begin(), effects.end(), effectId) != effects.end();
    }

    bool affectsSkill(ESM::RefId effectId)
    {
        static const std::array effects{
            ESM::MagicEffect::DrainSkill,
            ESM::MagicEffect::DamageSkill,
            ESM::MagicEffect::RestoreSkill,
            ESM::MagicEffect::FortifySkill,
            ESM::MagicEffect::AbsorbSkill,
        };

        return std::find(effects.begin(), effects.end(), effectId) != effects.end();
    }
}

ESM::RefId Utils::getActiveEffectIdFromLegacyIndex(int effectId)
{
    return ESM::MagicEffect::indexToRefId(effectId);
}

int Utils::getLegacyIndexFromActiveEffectId(ESM::RefId effectId)
{
    return ESM::MagicEffect::refIdToIndex(effectId);
}

std::variant<ESM::RefId, ESM::RefNum> Utils::getActiveEffectArgFromLegacyIndex(ESM::RefId effectId, int arg)
{
    if (arg < 0)
        return ESM::RefId();

    if (isSummonEffect(effectId))
        return ESM::RefNum{ .mIndex = static_cast<uint32_t>(arg), .mContentFile = -1 };

    if (affectsAttribute(effectId))
        return ESM::Attribute::indexToRefId(arg);

    if (affectsSkill(effectId))
        return ESM::Skill::indexToRefId(arg);

    return ESM::RefId();
}

int Utils::getLegacyIndexFromActiveEffectArg(const ESM::ActiveEffect& effect)
{
    if (const ESM::RefNum* actor = std::get_if<ESM::RefNum>(&effect.mArg))
        return actor->isSet() ? static_cast<int>(actor->mIndex) : -1;

    const ESM::RefId* id = std::get_if<ESM::RefId>(&effect.mArg);
    if (id == nullptr || id->empty())
        return -1;

    if (affectsAttribute(effect.mEffectId))
        return ESM::Attribute::refIdToIndex(*id);

    if (affectsSkill(effect.mEffectId))
        return ESM::Skill::refIdToIndex(*id);

    int attributeIndex = ESM::Attribute::refIdToIndex(*id);
    if (attributeIndex != -1)
        return attributeIndex;

    return ESM::Skill::refIdToIndex(*id);
}
