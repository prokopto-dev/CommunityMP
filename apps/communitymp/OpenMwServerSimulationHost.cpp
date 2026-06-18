#include "OpenMwServerSimulationHost.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "../openmw/OpenMWApplication.hpp"
#include "../openmw-mp/ServerContentRegistry.hpp"

namespace
{
    using ConfigureOpenMwApplication = bool (*)(
        int, char**, OMW::Engine&, Files::ConfigurationManager&, std::string_view);

    ConfigureOpenMwApplication getOpenMwApplicationConfigurator()
    {
        return &configureOpenMwApplication;
    }

    std::vector<std::string> buildOpenMwEngineArguments()
    {
        std::vector<std::string> arguments;
        arguments.emplace_back("communitymp-openmw-server");
        arguments.emplace_back("--no-sound");
        arguments.emplace_back("--skip-menu");
        arguments.emplace_back("--new-game");
        arguments.emplace_back("--script-all");
        arguments.emplace_back("--script-all-dialogue");

        for (const mwmp::ServerDataFileRequirement& requirement : mwmp::ServerContentRegistry::get().dataFiles())
        {
            if (!requirement.name.empty())
                arguments.emplace_back("--content=" + requirement.name);
        }

        return arguments;
    }

    mwmp::SimulationRuntimeBootstrap buildOpenMwBootstrap()
    {
        const mwmp::ServerContentRegistryStatistics content = mwmp::ServerContentRegistry::get().statistics();
        const std::vector<std::string> engineArguments = buildOpenMwEngineArguments();

        mwmp::SimulationRuntimeBootstrap bootstrap;
        bootstrap.canConfigureOpenMwApplication = getOpenMwApplicationConfigurator() != nullptr;
        bootstrap.contentRegistryLoaded = content.loaded;
        bootstrap.contentFileCount = content.dataFileCount;
        bootstrap.engineArgumentCount = engineArguments.size();
        bootstrap.hasOpenMwContentPlan = content.loaded && content.dataFileCount != 0;

        if (!bootstrap.canConfigureOpenMwApplication)
            bootstrap.blockedBy = "openmw-application-configurator-missing";
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
