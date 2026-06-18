#include "OpenMwServerSimulationHost.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <components/files/configurationmanager.hpp>

#include "../openmw/OpenMWApplication.hpp"
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

    mwmp::SimulationRuntimeBootstrap buildOpenMwBootstrap()
    {
        const mwmp::ServerContentRegistryStatistics content = mwmp::ServerContentRegistry::get().statistics();
        std::vector<std::string> engineArguments = buildOpenMwConfigArguments(false);
        OpenMwApplicationSettings settings;
        Files::ConfigurationManager cfgMgr(true);

        mwmp::SimulationRuntimeBootstrap bootstrap;
        bootstrap.canConfigureOpenMwApplication = getOpenMwApplicationConfigurator() != nullptr;
        bootstrap.canLoadOpenMwApplicationSettings = getOpenMwApplicationSettingsLoader() != nullptr
            && loadOpenMwSettingsFromArguments(engineArguments, settings, cfgMgr);
        if (!bootstrap.canLoadOpenMwApplicationSettings && content.loaded && content.dataFileCount != 0)
        {
            engineArguments = buildOpenMwConfigArguments(true);
            Files::ConfigurationManager fallbackCfgMgr(true);
            bootstrap.canLoadOpenMwApplicationSettings
                = loadOpenMwSettingsFromArguments(engineArguments, settings, fallbackCfgMgr);
        }
        bootstrap.contentRegistryLoaded = content.loaded;
        bootstrap.contentFileCount = content.dataFileCount;
        bootstrap.resolvedOpenMwDataDirCount = settings.dataDirs.size();
        bootstrap.resolvedOpenMwContentFileCount = settings.contentFiles.size();
        bootstrap.engineArgumentCount = engineArguments.size();
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
        else
            bootstrap.blockedBy = "headless-openmw-engine-not-created";

        return bootstrap;
    }

    mwmp::SimulationRuntimeTopology buildOpenMwTopology()
    {
        mwmp::SimulationRuntimeTopology topology;
        topology.unifiedExecutable = true;
        topology.linksOpenMwCore = true;
        topology.hasHeadlessOpenMwEngine = false;
        topology.runsOpenMwLua = false;
        topology.rendererClientProtocol = false;
        return topology;
    }
}

namespace communitymp
{
    std::unique_ptr<mwmp::SimulationRuntime> createOpenMwServerSimulationRuntime()
    {
        return std::make_unique<mwmp::SimulationRuntime>(mwmp::SimulationRuntimeKind::OpenMwHeadless,
            mwmp::SimulationRuntimeKind::PacketMirror, mwmp::SimulationRuntimeCapabilities{}, buildOpenMwTopology(),
            buildOpenMwBootstrap());
    }
}
