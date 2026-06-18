#include "OpenMwServerSimulationHost.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/osgpluginchecker.hpp>
#include <components/platform/platform.hpp>

#include "../openmw/OpenMWApplication.hpp"
#include "../openmw/engine.hpp"
#include "../openmw-mp/ServerContentDatabase.hpp"
#include "../openmw-mp/ServerContentRegistry.hpp"

namespace
{
    using ConfigureOpenMwApplication = bool (*)(int, char**, OMW::Engine&, Files::ConfigurationManager&, std::string_view);
    using LoadOpenMwApplicationSettings = bool (*)(
        int, char**, Files::ConfigurationManager&, OpenMwApplicationSettings&, bool);

    ConfigureOpenMwApplication getOpenMwApplicationConfigurator()
    {
        return &configureOpenMwApplication;
    }

    LoadOpenMwApplicationSettings getOpenMwApplicationSettingsLoader()
    {
        return &loadOpenMwApplicationSettings;
    }

    std::vector<std::string> buildOpenMwConfigArguments(bool includeServerContent)
    {
        std::vector<std::string> arguments;
        arguments.emplace_back("communitymp-openmw-server");
        arguments.emplace_back("--no-sound");
        arguments.emplace_back("--skip-menu");
        arguments.emplace_back("--new-game");
        arguments.emplace_back("--script-all");
        arguments.emplace_back("--script-all-dialogue");
        if (!includeServerContent)
            return arguments;

        for (const mwmp::ServerDataFileRequirement& requirement : mwmp::ServerContentRegistry::get().dataFiles())
        {
            if (!requirement.name.empty())
                arguments.emplace_back("--content=" + requirement.name);
        }

        return arguments;
    }

    std::vector<char*> makeMutableArgv(std::vector<std::string>& arguments)
    {
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (std::string& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        return argv;
    }

    bool loadOpenMwSettingsFromArguments(
        std::vector<std::string> arguments, OpenMwApplicationSettings& settings, Files::ConfigurationManager& cfgMgr)
    {
        std::vector<char*> argv = makeMutableArgv(arguments);
        return loadOpenMwApplicationSettings(static_cast<int>(arguments.size()), argv.data(), cfgMgr, settings, true);
    }

    std::string normalizeContentName(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::set<std::string> getServerContentNames()
    {
        std::set<std::string> names;
        for (const mwmp::ServerDataFileRequirement& requirement : mwmp::ServerContentRegistry::get().dataFiles())
        {
            if (!requirement.name.empty())
                names.insert(normalizeContentName(requirement.name));
        }
        return names;
    }

    std::set<std::string> getOpenMwContentNames(const OpenMwApplicationSettings& settings)
    {
        std::set<std::string> names;
        for (const std::string& contentFile : settings.contentFiles)
        {
            const std::string normalized = normalizeContentName(contentFile);
            if (!normalized.empty() && normalized != "builtin.omwscripts")
                names.insert(normalized);
        }
        return names;
    }

    std::size_t countMissingNames(const std::set<std::string>& expected, const std::set<std::string>& actual)
    {
        std::size_t missing = 0;
        for (const std::string& name : expected)
        {
            if (actual.find(name) == actual.end())
                ++missing;
        }
        return missing;
    }

    mwmp::SimulationRuntimeBootstrap buildOpenMwBootstrap(std::vector<std::string>& selectedEngineArguments)
    {
        std::vector<std::string> engineArguments = buildOpenMwConfigArguments(false);
        OpenMwApplicationSettings settings;
        Files::ConfigurationManager cfgMgr(true);

        mwmp::SimulationRuntimeBootstrap bootstrap;
        bootstrap.canConfigureOpenMwApplication = getOpenMwApplicationConfigurator() != nullptr;
        bootstrap.canLoadOpenMwApplicationSettings = getOpenMwApplicationSettingsLoader() != nullptr
            && loadOpenMwSettingsFromArguments(engineArguments, settings, cfgMgr);
        selectedEngineArguments = engineArguments;

        if (bootstrap.canLoadOpenMwApplicationSettings)
        {
            mwmp::ServerContentRegistry::get().enrichFromOpenMwContentPlan(settings.dataDirs, settings.contentFiles);
            mwmp::ServerContentDatabase::get().updateFromOpenMwContentPlan(
                settings.dataDirs, settings.contentFiles, mwmp::ServerContentRegistry::get().dataFiles());
        }

        mwmp::ServerContentRegistryStatistics content = mwmp::ServerContentRegistry::get().statistics();
        if (bootstrap.canLoadOpenMwApplicationSettings && content.loaded && content.dataFileCount != 0)
        {
            const std::set<std::string> serverContentNames = getServerContentNames();
            const std::set<std::string> openMwContentNames = getOpenMwContentNames(settings);
            if (countMissingNames(serverContentNames, openMwContentNames) != 0
                || countMissingNames(openMwContentNames, serverContentNames) != 0)
                bootstrap.canLoadOpenMwApplicationSettings = false;
        }

        if (!bootstrap.canLoadOpenMwApplicationSettings && content.loaded && content.dataFileCount != 0)
        {
            engineArguments = buildOpenMwConfigArguments(true);
            Files::ConfigurationManager fallbackCfgMgr(true);
            OpenMwApplicationSettings fallbackSettings;
            bootstrap.canLoadOpenMwApplicationSettings
                = loadOpenMwSettingsFromArguments(engineArguments, fallbackSettings, fallbackCfgMgr);
            bootstrap.usedServerContentFallback = bootstrap.canLoadOpenMwApplicationSettings;
            if (bootstrap.canLoadOpenMwApplicationSettings)
            {
                settings = std::move(fallbackSettings);
                selectedEngineArguments = engineArguments;
                mwmp::ServerContentRegistry::get().enrichFromOpenMwContentPlan(settings.dataDirs, settings.contentFiles);
                mwmp::ServerContentDatabase::get().updateFromOpenMwContentPlan(
                    settings.dataDirs, settings.contentFiles, mwmp::ServerContentRegistry::get().dataFiles());
                content = mwmp::ServerContentRegistry::get().statistics();
            }
        }
        bootstrap.contentRegistryLoaded = content.loaded;
        bootstrap.contentFileCount = content.dataFileCount;
        bootstrap.resolvedOpenMwDataDirCount = settings.dataDirs.size();
        bootstrap.resolvedOpenMwContentFileCount = settings.contentFiles.size();
        bootstrap.engineArgumentCount = engineArguments.size();
        const std::set<std::string> serverContentNames = getServerContentNames();
        const std::set<std::string> openMwContentNames = getOpenMwContentNames(settings);
        bootstrap.missingServerContentFileCount = countMissingNames(serverContentNames, openMwContentNames);
        bootstrap.extraOpenMwContentFileCount = countMissingNames(openMwContentNames, serverContentNames);
        bootstrap.contentPlanMatchesServerRegistry = content.loaded
            && !serverContentNames.empty()
            && bootstrap.missingServerContentFileCount == 0
            && bootstrap.extraOpenMwContentFileCount == 0;
        bootstrap.hasOpenMwContentPlan = bootstrap.canLoadOpenMwApplicationSettings
            && bootstrap.resolvedOpenMwDataDirCount != 0
            && bootstrap.resolvedOpenMwContentFileCount != 0
            && content.loaded
            && content.dataFileCount != 0;

        if (!bootstrap.canConfigureOpenMwApplication)
            bootstrap.blockedBy = "openmw-application-configurator-missing";
        else if (!bootstrap.canLoadOpenMwApplicationSettings)
            bootstrap.blockedBy = "openmw-application-settings-unavailable";
        else if (bootstrap.resolvedOpenMwDataDirCount == 0)
            bootstrap.blockedBy = "openmw-data-dirs-unresolved";
        else if (bootstrap.resolvedOpenMwContentFileCount == 0)
            bootstrap.blockedBy = "openmw-content-plan-empty";
        else if (!content.loaded)
            bootstrap.blockedBy = "server-content-registry-unavailable";
        else if (content.dataFileCount == 0)
            bootstrap.blockedBy = "server-content-registry-empty";
        else if (!bootstrap.contentPlanMatchesServerRegistry)
            bootstrap.blockedBy = "openmw-content-plan-registry-mismatch";
        else
            bootstrap.blockedBy = "headless-openmw-engine-not-created";

        return bootstrap;
    }

    mwmp::SimulationRuntimeTopology buildOpenMwTopology(bool hasPreparedEngine)
    {
        mwmp::SimulationRuntimeTopology topology;
        topology.unifiedExecutable = true;
        topology.linksOpenMwCore = true;
        topology.hasHeadlessOpenMwEngine = hasPreparedEngine;
        topology.runsOpenMwLua = hasPreparedEngine;
        topology.rendererClientProtocol = false;
        return topology;
    }

    mwmp::SimulationRuntimeCapabilities buildOpenMwCapabilities(bool hasPreparedEngine)
    {
        mwmp::SimulationRuntimeCapabilities capabilities;
        capabilities.ownsWorldState = hasPreparedEngine;
        capabilities.resolvesCells = hasPreparedEngine;
        capabilities.runsScripts = hasPreparedEngine;
        capabilities.runsActorAi = hasPreparedEngine;
        capabilities.ownsActorMovement = hasPreparedEngine;
        capabilities.ownsActorCombat = hasPreparedEngine;
        return capabilities;
    }

    class OpenMwServerSimulationRuntime final : public mwmp::SimulationRuntime
    {
    public:
        OpenMwServerSimulationRuntime(mwmp::SimulationRuntimeKind activeKind,
            mwmp::SimulationRuntimeCapabilities capabilities, mwmp::SimulationRuntimeTopology topology,
            mwmp::SimulationRuntimeBootstrap bootstrap, std::unique_ptr<Files::ConfigurationManager> cfgMgr,
            std::unique_ptr<OMW::Engine> engine)
            : mwmp::SimulationRuntime(
                  mwmp::SimulationRuntimeKind::OpenMwHeadless, activeKind, capabilities, topology, std::move(bootstrap))
            , mCfgMgr(std::move(cfgMgr))
            , mEngine(std::move(engine))
        {
        }

        void setSimulationCellFocuses(const std::vector<mwmp::SimulationCellFocus>& focuses) override
        {
            mSimulationFocuses = focuses;
            if (mNextSimulationFocus >= mSimulationFocuses.size())
                mNextSimulationFocus = 0;
        }

        void tick(float deltaSeconds) override
        {
            if (mEngine != nullptr)
            {
                focusNextSimulationCell();
                static_cast<void>(mEngine->tickServerSimulation(deltaSeconds));
            }
        }

        bool collectActorSnapshots(std::vector<mwmp::BaseActorList>& actorLists) override
        {
            if (mEngine == nullptr || !mEngine->isServerSimulationPrepared())
                return false;

            mEngine->exportServerSimulationActorSnapshots(actorLists);
            return !actorLists.empty();
        }

    private:
        void focusNextSimulationCell()
        {
            if (mEngine == nullptr || mSimulationFocuses.empty())
                return;

            if (mNextSimulationFocus >= mSimulationFocuses.size())
                mNextSimulationFocus = 0;

            const mwmp::SimulationCellFocus& focus = mSimulationFocuses[mNextSimulationFocus];
            mNextSimulationFocus = (mNextSimulationFocus + 1) % mSimulationFocuses.size();
            static_cast<void>(mEngine->focusServerSimulationCell(
                focus.cell, focus.hasPosition ? &focus.position : nullptr));
        }

        std::unique_ptr<Files::ConfigurationManager> mCfgMgr;
        std::unique_ptr<OMW::Engine> mEngine;
        std::vector<mwmp::SimulationCellFocus> mSimulationFocuses;
        std::size_t mNextSimulationFocus = 0;
    };

    std::unique_ptr<OMW::Engine> prepareOpenMwServerEngine(
        const std::vector<std::string>& engineArguments, Files::ConfigurationManager& cfgMgr)
    {
        Platform::init();

        std::vector<std::string> mutableArguments = engineArguments;
        std::vector<char*> argv = makeMutableArgv(mutableArguments);

        auto engine = std::make_unique<OMW::Engine>(cfgMgr);
        engine->setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

        if (!configureOpenMwApplication(static_cast<int>(mutableArguments.size()), argv.data(), *engine, cfgMgr,
                "CommunityMP-server-openmw"))
            return nullptr;

        if (!Misc::checkRequiredOSGPluginsArePresent())
            return nullptr;

        engine->prepareServerSimulation();
        if (!engine->isServerSimulationPrepared())
            return nullptr;

        return engine;
    }
}

namespace communitymp
{
    std::unique_ptr<mwmp::SimulationRuntime> createOpenMwServerSimulationRuntime()
    {
        std::vector<std::string> engineArguments;
        mwmp::SimulationRuntimeBootstrap bootstrap = buildOpenMwBootstrap(engineArguments);
        std::unique_ptr<Files::ConfigurationManager> cfgMgr;
        std::unique_ptr<OMW::Engine> engine;

        if (bootstrap.blockedBy.empty() || bootstrap.blockedBy == "headless-openmw-engine-not-created")
        {
            try
            {
                cfgMgr = std::make_unique<Files::ConfigurationManager>(true);
                engine = prepareOpenMwServerEngine(engineArguments, *cfgMgr);
                if (engine == nullptr)
                    bootstrap.blockedBy = "openmw-server-engine-prepare-failed";
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to prepare server OpenMW simulation runtime: " << e.what();
                bootstrap.blockedBy = "openmw-server-engine-prepare-failed";
                engine = nullptr;
                cfgMgr = nullptr;
            }
        }

        const bool hasPreparedEngine = engine != nullptr && engine->isServerSimulationPrepared();
        if (hasPreparedEngine)
            bootstrap.blockedBy.clear();

        const mwmp::SimulationRuntimeKind activeKind = hasPreparedEngine ? mwmp::SimulationRuntimeKind::OpenMwHeadless
                                                                         : mwmp::SimulationRuntimeKind::PacketMirror;
        return std::make_unique<OpenMwServerSimulationRuntime>(activeKind, buildOpenMwCapabilities(hasPreparedEngine),
            buildOpenMwTopology(hasPreparedEngine), std::move(bootstrap), std::move(cfgMgr), std::move(engine));
    }
}
