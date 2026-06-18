#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/osgpluginchecker.hpp>
#include <components/misc/rng.hpp>
#include <components/platform/platform.hpp>
#include <components/version/version.hpp>

#include "mwgui/debugwindow.hpp"

#include "OpenMWApplication.hpp"
#include "engine.hpp"
#include "options.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "mwmp/Main.hpp"
#endif

#include <boost/program_options/variables_map.hpp>

#if defined(_WIN32)
#include <components/misc/windows.hpp>
// makes __argc and __argv available on windows
#include <cstdlib>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

#include <filesystem>
#include <set>

#if (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))
#include <unistd.h>
#endif

namespace
{
    namespace bpo = boost::program_options;
    using StringsVector = std::vector<std::string>;

    struct LoadedOpenMwApplicationSettings
    {
        bpo::variables_map variables;
        OpenMwApplicationSettings settings;
    };

    bool loadOpenMwApplicationSettingsImpl(
        int argc, char** argv, Files::ConfigurationManager& cfgMgr, LoadedOpenMwApplicationSettings& loaded, bool quiet)
    {
        bpo::options_description desc = OpenMW::makeOptionsDescription();
        bpo::variables_map& variables = loaded.variables;
        OpenMwApplicationSettings& settings = loaded.settings;

        Files::parseArgs(argc, argv, variables, desc);
        bpo::notify(variables);

        if (variables.count("help"))
        {
            Debug::getRawStdout() << desc << std::endl;
            return false;
        }

        if (variables.count("version"))
        {
            Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
            return false;
        }

        cfgMgr.processPaths(variables, std::filesystem::current_path());

        cfgMgr.readConfiguration(variables, desc);

        settings = {};
        settings.grabMouse = !variables["no-grab"].as<bool>();

        // Font encoding settings
        settings.encoding = variables["encoding"].as<std::string>();

        Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

        Files::PathContainer::value_type local(variables["data-local"]
                                                   .as<Files::MaybeQuotedPathContainer::value_type>()
                                                   .u8string()); // This call to u8string is redundant, but required to
                                                                 // build on MSVC 14.26 due to implementation bugs.
        if (!local.empty())
            dataDirs.push_back(std::move(local));

        cfgMgr.filterOutNonExistingPaths(dataDirs);
        settings.dataDirs = std::move(dataDirs);

        settings.resourceDir = variables["resources"]
                                   .as<Files::MaybeQuotedPath>()
                                   .u8string(); // This call to u8string is redundant, but required to build on MSVC
                                                 // 14.26 due to implementation bugs.

        // fallback archives
        settings.archives = variables["fallback-archive"].as<StringsVector>();

        StringsVector content = variables["content"].as<StringsVector>();
        if (content.empty())
        {
            if (!quiet)
            {
                Debug::getRawStderr() << "No content file given (esm/esp, nor omwgame/omwaddon). Aborting..."
                                      << std::endl;
            }
            return false;
        }
        settings.contentFiles.push_back("builtin.omwscripts");
        std::set<std::string> contentDedupe{ "builtin.omwscripts" };
        for (const auto& contentFile : content)
        {
            if (!contentDedupe.insert(contentFile).second)
            {
                if (!quiet)
                {
                    Debug::getRawStderr() << "Content file specified more than once: " << contentFile << ". Aborting..."
                                          << std::endl;
                }
                return false;
            }
        }

        for (auto& file : content)
        {
            settings.contentFiles.push_back(file);
        }

        settings.groundcoverFiles = variables["groundcover"].as<StringsVector>();

        settings.usedLegacyLuaScriptsOption = variables.count("lua-scripts") != 0;

        // startup-settings
        settings.startCell = variables["start"].as<std::string>();
        settings.skipMenu = variables["skip-menu"].as<bool>();
        settings.newGame = variables["new-game"].as<bool>();
        settings.newGameWithoutSkipMenu = !settings.skipMenu && settings.newGame;

        // scripts
        settings.compileAllScripts = variables["script-all"].as<bool>();
        settings.compileAllDialogue = variables["script-all-dialogue"].as<bool>();
        settings.scriptConsoleMode = variables["script-console"].as<bool>();
        settings.startupScript = variables["script-run"].as<std::string>();
        settings.warningsMode = variables["script-warn"].as<int>();
        settings.saveGameFile = variables["load-savegame"].as<Files::MaybeQuotedPath>().u8string();

        // other settings
        Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
        settings.soundUsage = !variables["no-sound"].as<bool>();
        settings.activationDistanceOverride = variables["activate-dist"].as<int>();
        settings.exportFonts = variables["export-fonts"].as<bool>();
        settings.randomSeed = variables["random-seed"].as<unsigned int>();

        return true;
    }
}

bool loadOpenMwApplicationSettings(
    int argc, char** argv, Files::ConfigurationManager& cfgMgr, OpenMwApplicationSettings& settings, bool quiet)
{
    LoadedOpenMwApplicationSettings loaded;
    if (!loadOpenMwApplicationSettingsImpl(argc, argv, cfgMgr, loaded, quiet))
        return false;

    settings = std::move(loaded.settings);
    return true;
}

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
bool configureOpenMwApplication(int argc, char** argv, OMW::Engine& engine, Files::ConfigurationManager& cfgMgr,
    std::string_view logName)
{
    LoadedOpenMwApplicationSettings loaded;
    if (!loadOpenMwApplicationSettingsImpl(argc, argv, cfgMgr, loaded, false))
        return false;

    const OpenMwApplicationSettings& settings = loaded.settings;

    Debug::setupLogging(cfgMgr.getLogPath(), logName);
    Log(Debug::Info) << Version::getOpenmwVersionDescription();

    Settings::Manager::load(cfgMgr);

#ifdef BUILD_TES3MP_CLIENT
    mwmp::Main::configure(loaded.variables, cfgMgr);
#endif

    if (settings.usedLegacyLuaScriptsOption)
    {
        Log(Debug::Warning) << "Lua scripts have been specified via the old lua-scripts option and will not be loaded. "
                               "Please update them to a version which uses the new omwscripts format.";
    }
    if (settings.newGameWithoutSkipMenu)
        Log(Debug::Warning) << "Warning: new-game used without skip-menu -> ignoring it";

    MWGui::DebugWindow::startLogRecording();

    engine.setGrabMouse(settings.grabMouse);

    Log(Debug::Info) << ToUTF8::encodingUsingMessage(settings.encoding);
    engine.setEncoding(ToUTF8::calculateEncoding(settings.encoding));
    engine.setResourceDir(settings.resourceDir);
    engine.setDataDirs(settings.dataDirs);

    for (const std::string& archive : settings.archives)
        engine.addArchive(archive);

    for (const std::string& contentFile : settings.contentFiles)
        engine.addContentFile(contentFile);

    for (const std::string& file : settings.groundcoverFiles)
        engine.addGroundcoverFile(file);

    engine.setCell(settings.startCell);
    engine.setSkipMenu(settings.skipMenu, settings.newGame);
    engine.setCompileAll(settings.compileAllScripts);
    engine.setCompileAllDialogue(settings.compileAllDialogue);
    engine.setScriptConsoleMode(settings.scriptConsoleMode);
    engine.setStartupScript(settings.startupScript);
    engine.setWarningsMode(settings.warningsMode);
    engine.setSaveGameFile(settings.saveGameFile);
    engine.setSoundUsage(settings.soundUsage);
    engine.setActivationDistanceOverride(settings.activationDistanceOverride);
    engine.enableFontExport(settings.exportFonts);
    engine.setRandomSeed(settings.randomSeed);

    return true;
}

namespace
{
    class OSGLogHandler : public osg::NotifyHandler
    {
        void notify(osg::NotifySeverity severity, const char* msg) override
        {
            // Copy, because osg logging is not thread safe.
            std::string msgCopy(msg);
            if (msgCopy.empty())
                return;

            Debug::Level level;
            switch (severity)
            {
                case osg::ALWAYS:
                case osg::FATAL:
                    level = Debug::Error;
                    break;
                case osg::WARN:
                case osg::NOTICE:
                    level = Debug::Warning;
                    break;
                case osg::INFO:
                    level = Debug::Info;
                    break;
                case osg::DEBUG_INFO:
                case osg::DEBUG_FP:
                default:
                    level = Debug::Debug;
            }
            std::string_view s(msgCopy);
            if (s.size() < 1024)
                Log(level) << (s.back() == '\n' ? s.substr(0, s.size() - 1) : s);
            else
            {
                while (!s.empty())
                {
                    size_t lineSize = 1;
                    while (lineSize < s.size() && s[lineSize - 1] != '\n')
                        lineSize++;
                    Log(level) << s.substr(0, s[lineSize - 1] == '\n' ? lineSize - 1 : lineSize);
                    s = s.substr(lineSize);
                }
            }
        }
    };
}

int runApplication(int argc, char* argv[])
{
    Platform::init();

#ifdef __APPLE__
    setenv("OSG_GL_TEXTURE_STORAGE", "OFF", 0);
#endif

    osg::setNotifyHandler(new OSGLogHandler());
    Files::ConfigurationManager cfgMgr;
    std::unique_ptr<OMW::Engine> engine = std::make_unique<OMW::Engine>(cfgMgr);

    engine->setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

    if (configureOpenMwApplication(argc, argv, *engine, cfgMgr))
    {
        if (!Misc::checkRequiredOSGPluginsArePresent())
            return 1;

        engine->go();
    }

    return 0;
}
