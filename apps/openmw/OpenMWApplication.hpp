#ifndef OPENMW_APPLICATION_HPP
#define OPENMW_APPLICATION_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Files
{
    struct ConfigurationManager;
}

namespace OMW
{
    class Engine;
}

struct OpenMwApplicationSettings
{
    std::vector<std::filesystem::path> dataDirs;
    std::filesystem::path resourceDir;
    std::vector<std::string> archives;
    std::vector<std::string> contentFiles;
    std::vector<std::string> groundcoverFiles;
    std::string startCell;
    std::string encoding;
    std::filesystem::path startupScript;
    std::filesystem::path saveGameFile;
    bool grabMouse = true;
    bool skipMenu = false;
    bool newGame = false;
    bool compileAllScripts = false;
    bool compileAllDialogue = false;
    bool scriptConsoleMode = false;
    bool soundUsage = true;
    bool exportFonts = false;
    bool usedLegacyLuaScriptsOption = false;
    bool newGameWithoutSkipMenu = false;
    int activationDistanceOverride = -1;
    int warningsMode = 1;
    unsigned int randomSeed = 0;
};

bool loadOpenMwApplicationSettings(
    int argc, char* argv[], Files::ConfigurationManager& cfgMgr, OpenMwApplicationSettings& settings,
    bool quiet = false);

bool configureOpenMwApplication(int argc, char* argv[], OMW::Engine& engine, Files::ConfigurationManager& cfgMgr,
    std::string_view logName = "OpenMW");

int runApplication(int argc, char* argv[]);

#endif
