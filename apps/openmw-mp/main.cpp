#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>

#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/ErrorMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include "Player.hpp"
#include "ServerNetworking.hpp"
#include "MasterClient.hpp"
#include "ServerApplication.hpp"
#include "ServerContentRegistry.hpp"
#include "ServerEventDispatcher.hpp"
#include "ServerSimulation.hpp"
#include "Utils.hpp"

#include <apps/openmw-mp/Script/Script.hpp>

#ifdef ENABLE_BREAKPAD
#include <handler/exception_handler.h>
#endif

using namespace mwmp;

#ifdef ENABLE_BREAKPAD
google_breakpad::ExceptionHandler *pHandler = 0;
#if defined(_WIN32)
bool DumpCallback(const wchar_t* _dump_dir,const wchar_t* _minidump_id,void* context,EXCEPTION_POINTERS* exinfo,MDRawAssertionInfo* assertion,bool success)
#elif defined(__linux)
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool success)
#endif
{
    // NO STACK USE, NO HEAP USE THERE !!!
    return success;
}

void breakpad(std::string pathToDump)
{
#ifdef _WIN32
    pHandler = new google_breakpad::ExceptionHandler(
            L"crashdumps\\",
            /*FilterCallback*/ 0,
            DumpCallback,
            0,
            google_breakpad::ExceptionHandler::HANDLER_ALL);
#else
    google_breakpad::MinidumpDescriptor md(pathToDump);
    pHandler = new google_breakpad::ExceptionHandler(
            md,
            /*FilterCallback*/ 0,
            DumpCallback,
            /*context*/ 0,
            true,
            -1
    );
#endif
}

void breakpad_close()
{
    delete pHandler;
}
#else
void breakpad(std::string pathToDump){}
void breakpad_close(){}
#endif

namespace
{
    constexpr char settingsFileName[] = "communitymp-server.cfg";
    constexpr char legacySettingsFileName[] = "tes3mp-server.cfg";
    constexpr std::array<const char*, 2> defaultSettingsFileNames = {
        "communitymp-server-default.cfg",
        "tes3mp-server-default.cfg",
    };

    std::filesystem::path getDefaultSettingsPath(const Files::ConfigurationManager& cfgMgr)
    {
        for (const char* fileName : defaultSettingsFileNames)
        {
            const std::filesystem::path localDefault = cfgMgr.getLocalPath() / fileName;
            if (std::filesystem::exists(localDefault))
                return localDefault;

            const std::filesystem::path globalDefault = cfgMgr.getGlobalPath() / fileName;
            if (std::filesystem::exists(globalDefault))
                return globalDefault;
        }

        throw std::runtime_error(
            "No default settings file found! Make sure \"communitymp-server-default.cfg\" was properly installed.");
    }

    std::filesystem::path getReadableSettingsPath(const Files::ConfigurationManager& cfgMgr)
    {
        const std::filesystem::path preferred = cfgMgr.getUserConfigPath() / settingsFileName;
        if (std::filesystem::exists(preferred))
            return preferred;

        const std::filesystem::path legacy = cfgMgr.getUserConfigPath() / legacySettingsFileName;
        if (std::filesystem::exists(legacy))
            return legacy;

        return preferred;
    }

    std::string getRequiredSimulationRuntimeBlocker(const mwmp::SimulationRuntime& runtime)
    {
        if (runtime.canOwnActorAuthority())
            return {};

        if (!runtime.bootstrap().blockedBy.empty())
            return runtime.bootstrap().blockedBy;

        const mwmp::SimulationRuntimeTopology& topology = runtime.topology();
        const mwmp::SimulationRuntimeCapabilities& capabilities = runtime.capabilities();

        if (!topology.unifiedExecutable)
            return "unified-executable-required";
        if (!topology.linksOpenMwCore)
            return "openmw-core-not-linked";
        if (!topology.hasHeadlessOpenMwEngine)
            return "headless-openmw-engine-missing";
        if (!runtime.worldState().prepared)
            return "openmw-world-not-prepared";
        if (!runtime.worldState().persistent)
            return "openmw-world-save-not-bound";
        if (!capabilities.ownsWorldState || !capabilities.resolvesCells)
            return "openmw-world-state-not-owned";
        if (!capabilities.runsScripts)
            return "openmw-scripts-not-running";
        if (!capabilities.runsActorAi || !capabilities.ownsActorMovement)
            return "openmw-actor-ai-not-owned";
        if (!capabilities.ownsActorCombat)
            return "openmw-actor-combat-not-owned";

        return "unknown";
    }

    struct MasterServerSettings
    {
        bool enabled = false;
        std::string address;
        int port = 0;
        int updateRate = 0;
        std::string hostname;
    };

    MasterServerSettings readMasterServerSettings(Settings::Manager& mgr)
    {
        MasterServerSettings settings;
        settings.enabled = mgr.getBool("enabled", "MasterServer");
        settings.address = mgr.getString("address", "MasterServer");
        settings.port = mgr.getInt("port", "MasterServer");
        settings.updateRate = mgr.getInt("rate", "MasterServer");
        settings.hostname = mgr.getString("hostname", "General");
        return settings;
    }
}

std::string loadSettings(const Files::ConfigurationManager& cfgMgr)
{
    Settings::SettingsFileParser parser;

    parser.loadSettingsFile(getDefaultSettingsPath(cfgMgr), Settings::Manager::mDefaultSettings, false, false);

    const std::filesystem::path settingspath = getReadableSettingsPath(cfgMgr);
    if (std::filesystem::exists(settingspath))
        parser.loadSettingsFile(settingspath, Settings::Manager::mUserSettings, false, false);

    return settingspath.string();
}

class Tee : public boost::iostreams::sink
{
public:
    Tee(std::ostream &stream, std::ostream &stream2)
            : out(stream), out2(stream2)
    {
    }

    std::streamsize write(const char *str, std::streamsize size)
    {
        out.write (str, size);
        out.flush();
        out2.write (str, size);
        out2.flush();
        return size;
    }

private:
    std::ostream &out;
    std::ostream &out2;
};

boost::program_options::variables_map launchOptions(int argc, char *argv[], Files::ConfigurationManager& cfgMgr)
{
    namespace bpo = boost::program_options;
    bpo::variables_map variables;
    bpo::options_description desc;

    Files::ConfigurationManager::addCommonOptions(desc);
    desc.add_options()
            ("no-logs", bpo::value<bool>()->implicit_value(true)->default_value(false),
             "Do not write logs. Useful for daemonizing.");

    bpo::parsed_options valid_opts = bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
    bpo::store(valid_opts, variables);
    cfgMgr.processPaths(variables, std::filesystem::current_path());
    cfgMgr.readConfiguration(variables, desc, true);
    bpo::notify(variables);

    return variables;
}

int runCommunityMpDedicatedServer(int argc, char* argv[])
{
    Settings::Manager mgr;
    Files::ConfigurationManager cfgMgr;

    auto variables = launchOptions(argc, argv, cfgMgr);
    breakpad(boost::filesystem::path(cfgMgr.getLogPath()).string());

    loadSettings(cfgMgr);

    std::string commitHash = std::string(Version::getCommitHash());

    int logLevel = mgr.getInt("logLevel", "General");
    if (logLevel < TimedLog::LOG_VERBOSE || logLevel > TimedLog::LOG_FATAL)
        logLevel = TimedLog::LOG_VERBOSE;

    // Some objects used to redirect cout and cerr
    // Scope must be here, so this still works inside the catch block for logging exceptions
    std::streambuf* cout_rdbuf = std::cout.rdbuf ();
    std::streambuf* cerr_rdbuf = std::cerr.rdbuf ();

    boost::iostreams::stream_buffer<Tee> coutsb;
    boost::iostreams::stream_buffer<Tee> cerrsb;

    std::ostream oldcout(cout_rdbuf);
    std::ostream oldcerr(cerr_rdbuf);

    boost::filesystem::ofstream logfile;

    if (!variables["no-logs"].as<bool>())
    {
        // Redirect cout and cerr to CommunityMP server log

        const boost::filesystem::path logPath = boost::filesystem::path(cfgMgr.getLogPath())
            / ("communitymp-server-" + TimedLog::getFilenameTimestamp() + ".log");
        logfile.open(logPath);

        coutsb.open(Tee(logfile, oldcout));
        cerrsb.open(Tee(logfile, oldcerr));

        std::cout.rdbuf(&coutsb);
        std::cerr.rdbuf(&cerrsb);
    }

    LOG_INIT(logLevel);

    int players = mgr.getInt("maximumPlayers", "General");
    std::string address = mgr.getString("localAddress", "General");
    int port = mgr.getInt("port", "General");

    std::string password = mgr.getString("password", "General");
    const bool enforceDataFiles = Settings::Manager::getOrDefault<bool>("enforceDataFiles", "General", true);
    const bool ignoreScriptErrors = Settings::Manager::getOrDefault<bool>("ignoreScriptErrors", "General", false);
    const bool requireOpenMwServerSimulation
        = Settings::Manager::getOrDefault<bool>("requireOpenMwServerSimulation", "General", false);
    MasterServerSettings masterServerSettings = readMasterServerSettings(mgr);

    std::string pluginHome = mgr.getString("home", "Plugins");
    std::filesystem::path pluginHomePath = Files::pathFromUnicodeString(pluginHome);
    cfgMgr.processPath(pluginHomePath, cfgMgr.getLocalPath());
    pluginHome = Files::pathToUnicodeString(pluginHomePath);
    std::string dataDirectory = Utils::convertPath(pluginHome + "/data");

    std::vector<std::string> plugins(Utils::split(mgr.getString("plugins", "Plugins"), ','));

    std::string versionInfo = Utils::getVersionInfo(
        std::string(Branding::productName) + " dedicated server", TES3MP_VERSION, commitHash, TES3MP_PROTO_VERSION);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", versionInfo.c_str());
    
    Script::SetModDir(dataDirectory);
    ServerContentRegistry::get().loadFromDataDirectory(Files::pathFromUnicodeString(dataDirectory));

#ifdef ENABLE_LUA
    LangLua::AddPackagePath(Utils::convertPath(pluginHome + "/scripts/?.lua" + ";"
        + pluginHome + "/lib/lua/?.lua" + ";"));
#ifdef _WIN32
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.dll"));
#else
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.so"));
#endif

#endif

    int code = 1;

    // Remove carriage returns added to version file on Windows
    commitHash.erase(std::remove(commitHash.begin(), commitHash.end(), '\r'), commitHash.end());

    if (!isPacketAddressNumericHost(address))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "You cannot use non-numeric addresses for the server.");
        return 1;
    }

    try
    {
        for (auto plugin : plugins)
            Script::LoadScript(plugin.c_str(), pluginHome.c_str());

        std::unique_ptr<GnsTransport> gnsTransport = std::make_unique<GnsTransport>(GnsMode::Server);
        gnsTransport->startupServer(address, static_cast<unsigned short>(port), static_cast<unsigned int>(players));

        ServerNetworking serverNetworking(gnsTransport.get());
        serverNetworking.setServerPassword(password);
        serverNetworking.setDataFileEnforcementState(enforceDataFiles);
        serverNetworking.setScriptErrorIgnoringState(ignoreScriptErrors);
        serverNetworking.setNativeServerPoliciesEnabled(true);

        if (requireOpenMwServerSimulation)
        {
            const mwmp::SimulationRuntime& runtime = serverNetworking.getServerSimulation().runtime();
            const std::string blocker = getRequiredSimulationRuntimeBlocker(runtime);
            if (!blocker.empty())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "OpenMW server simulation is required but not ready: requested=%s active=%s blockedBy=%s",
                    runtime.requestedName(), runtime.activeName(), blocker.c_str());
                throw std::runtime_error("OpenMW server simulation is required but not ready: " + blocker);
            }
        }

        if (masterServerSettings.enabled)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sharing server query info to master enabled.");

            if (masterServerSettings.updateRate < 8000)
            {
                masterServerSettings.updateRate = 8000;
                LOG_APPEND(TimedLog::LOG_INFO,
                    "- switching to updateRate %i because the one in the server config was too low",
                    masterServerSettings.updateRate);
            }

            serverNetworking.InitQuery(masterServerSettings.address, (unsigned short) masterServerSettings.port);
            serverNetworking.getMasterClient()->SetMaxPlayers((unsigned) players);
            serverNetworking.getMasterClient()->SetUpdateRate((unsigned) masterServerSettings.updateRate);
            serverNetworking.getMasterClient()->SetHostname(masterServerSettings.hostname);
            serverNetworking.getMasterClient()->SetRuleString("CommitHash", commitHash.substr(0, 10));

            serverNetworking.getMasterClient()->Start();
        }

        serverNetworking.postInit();
        if (serverNetworking.getMasterClient() != nullptr)
        {
            serverNetworking.getMasterClient()->SetRuleString(
                "enforceDataFiles", serverNetworking.getDataFileEnforcementState() ? "true" : "false");
            serverNetworking.getMasterClient()->SetRuleString(
                "ignoreScriptErrors", serverNetworking.getScriptErrorIgnoringState() ? "true" : "false");
        }

        code = serverNetworking.mainLoop();

        if (serverNetworking.getMasterClient())
            serverNetworking.getMasterClient()->Stop();
    }
    catch (std::exception &e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, e.what());
        try
        {
            ServerEvents::serverScriptCrash(e.what());
        }
        catch (std::exception& callbackError)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, callbackError.what());
        }
        code = 1;
    }

    if (code == 0)
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Quitting peacefully.");

    LOG_QUIT();

    if (!variables["no-logs"].as<bool>())
    {
        // Restore cout and cerr
        std::cout.rdbuf(cout_rdbuf);
        std::cerr.rdbuf(cerr_rdbuf);
    }


    breakpad_close();
    return code;
}
