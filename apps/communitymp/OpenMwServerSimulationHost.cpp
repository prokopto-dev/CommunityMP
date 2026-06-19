#include "OpenMwServerSimulationHost.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
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
#include <components/files/conversion.hpp>
#include <components/misc/osgpluginchecker.hpp>
#include <components/platform/platform.hpp>
#include <components/settings/settings.hpp>

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
        if (!includeServerContent)
            return arguments;

        arguments.emplace_back("--replace=content");
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

    bool isTruthyRuntimeToggle(std::string value)
    {
        value = normalizeContentName(std::move(value));
        return value == "1" || value == "true" || value == "yes" || value == "on"
            || value == "openmw" || value == "openmw-headless";
    }

    bool isFalseRuntimeToggle(std::string value)
    {
        value = normalizeContentName(std::move(value));
        return value == "0" || value == "false" || value == "no" || value == "off"
            || value == "packet-mirror" || value == "mirror";
    }

    void logHeadlessOpenMwStage(std::string_view stage)
    {
        Log(Debug::Info) << "[CommunityMP headless] " << stage;
    }

    std::filesystem::path getServerOpenMwSavesPath()
    {
        return std::filesystem::current_path() / "server" / "data" / "world" / "openmw" / "saves";
    }

    bool shouldAttemptHeadlessOpenMwRuntime()
    {
        if (const char* runtime = std::getenv("COMMUNITYMP_RUNTIME"))
        {
            if (isTruthyRuntimeToggle(runtime))
                return true;
            if (isFalseRuntimeToggle(runtime))
                return false;
        }

        if (const char* enabled = std::getenv("COMMUNITYMP_ENABLE_HEADLESS_OPENMW"))
            return isTruthyRuntimeToggle(enabled);

        return true;
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
            const mwmp::ServerContentRegistryStatistics content = mwmp::ServerContentRegistry::get().statistics();
            mwmp::ServerContentDatabase::get().updateFromOpenMwContentPlan(
                settings.dataDirs, settings.contentFiles, settings.archives, settings.encoding,
                mwmp::ServerContentRegistry::get().dataFiles(), content.loadOrderSource);
        }

        mwmp::ServerContentRegistryStatistics content = mwmp::ServerContentRegistry::get().statistics();
        if (bootstrap.canLoadOpenMwApplicationSettings && content.loaded && content.dataFileCount != 0
            && content.serverLoadOrderLoaded)
        {
            std::vector<std::string> serverEngineArguments = buildOpenMwConfigArguments(true);
            Files::ConfigurationManager serverCfgMgr(true);
            OpenMwApplicationSettings serverSettings;
            const bool loadedServerOrderedSettings
                = loadOpenMwSettingsFromArguments(serverEngineArguments, serverSettings, serverCfgMgr);

            if (loadedServerOrderedSettings)
            {
                engineArguments = std::move(serverEngineArguments);
                settings = std::move(serverSettings);
                selectedEngineArguments = engineArguments;
                bootstrap.usedServerContentFallback = true;
                mwmp::ServerContentRegistry::get().enrichFromOpenMwContentPlan(settings.dataDirs, settings.contentFiles);
                content = mwmp::ServerContentRegistry::get().statistics();
                mwmp::ServerContentDatabase::get().updateFromOpenMwContentPlan(
                    settings.dataDirs, settings.contentFiles, settings.archives, settings.encoding,
                    mwmp::ServerContentRegistry::get().dataFiles(), content.loadOrderSource);
            }
            else
                bootstrap.canLoadOpenMwApplicationSettings = false;
        }

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
                content = mwmp::ServerContentRegistry::get().statistics();
                mwmp::ServerContentDatabase::get().updateFromOpenMwContentPlan(
                    settings.dataDirs, settings.contentFiles, settings.archives, settings.encoding,
                    mwmp::ServerContentRegistry::get().dataFiles(), content.loadOrderSource);
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
        topology.usesSinglePlayerProxy = hasPreparedEngine;
        topology.hasPersistentPlayerActors = hasPreparedEngine;
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

    mwmp::SimulationRuntimeWorldState buildOpenMwWorldState(const OMW::Engine* engine, bool hasPreparedEngine)
    {
        mwmp::SimulationRuntimeWorldState worldState;
        if (engine == nullptr || !hasPreparedEngine)
            return worldState;

        worldState.prepared = true;
        worldState.loadedFromSave = engine->wasServerSimulationWorldLoadedFromSave();
        worldState.initializedNewWorld = engine->wasServerSimulationWorldInitializedNew();
        worldState.savePath = Files::pathToUnicodeString(engine->getServerSimulationWorldSavePath());
        worldState.manifestPath = Files::pathToUnicodeString(engine->getServerSimulationWorldManifestPath());
        worldState.persistent = !worldState.savePath.empty() && !worldState.manifestPath.empty();

        const mwmp::ServerContentDatabaseStatistics contentDatabase = mwmp::ServerContentDatabase::get().statistics();
        worldState.contentPlanFingerprint = contentDatabase.contentPlanFingerprint;
        worldState.worldDatabaseFingerprint = contentDatabase.worldDatabaseFingerprint;
        worldState.serverWorldCompatibilityFingerprint = contentDatabase.serverWorldCompatibilityFingerprint;

        return worldState;
    }

    class OpenMwServerSimulationRuntime final : public mwmp::SimulationRuntime
    {
    public:
        OpenMwServerSimulationRuntime(mwmp::SimulationRuntimeKind activeKind,
            mwmp::SimulationRuntimeCapabilities capabilities, mwmp::SimulationRuntimeTopology topology,
            mwmp::SimulationRuntimeBootstrap bootstrap, mwmp::SimulationRuntimeWorldState worldState,
            std::unique_ptr<Files::ConfigurationManager> cfgMgr, std::unique_ptr<OMW::Engine> engine)
            : mwmp::SimulationRuntime(
                  mwmp::SimulationRuntimeKind::OpenMwHeadless, activeKind, capabilities, topology, std::move(bootstrap),
                  std::move(worldState))
            , mCfgMgr(std::move(cfgMgr))
            , mEngine(std::move(engine))
        {
        }

        void setSimulationCellFocuses(const std::vector<mwmp::SimulationCellFocus>& focuses) override
        {
            mSimulationFocuses = focuses;
            mFocusState.configuredCellCount = mSimulationFocuses.size();
            mFocusState.configuredPlayerCount = countConfiguredPlayerFocuses();
            if (mNextSimulationFocus >= mSimulationFocuses.size())
                mNextSimulationFocus = 0;
            pruneQueuedFocusDeltas();
        }

        void setPlayerActors(const std::vector<mwmp::SimulationPlayerTarget>& players) override
        {
            mPersistentPlayerActors.clear();
            if (mEngine != nullptr)
                mEngine->setServerSimulationPlayerActors(players);

            for (const mwmp::SimulationPlayerTarget& player : players)
            {
                if (!mwmp::isPacketGuidAssigned(player.guid) || !player.hasPosition)
                    continue;

                mwmp::SimulationPlayerSnapshot snapshot;
                snapshot.cell = player.cell;
                snapshot.position = player.position;
                snapshot.guid = player.guid;
                snapshot.name = player.name;
                snapshot.hasPositionData = true;
                if (player.hasStatsDynamicData)
                {
                    snapshot.creatureStats = player.creatureStats;
                    snapshot.hasStatsDynamicData = true;
                }
                mPersistentPlayerActors[player.guid] = std::move(snapshot);
            }

            mFocusState.persistentPlayerActorCount = mPersistentPlayerActors.size();
        }

        void tick(float deltaSeconds) override
        {
            if (mEngine == nullptr)
                return;

            mFocusState.lastClockDeltaSeconds = deltaSeconds;
            if (mSimulationFocuses.empty())
            {
                static_cast<void>(mEngine->tickServerSimulation(deltaSeconds));
                mFocusState.lastSimulationDeltaSeconds = deltaSeconds;
                mFocusState.lastQueuedDeltaSeconds = deltaSeconds;
                return;
            }

            queueFocusElapsedTime(deltaSeconds);
            const float simulationDeltaSeconds = focusNextSimulationCell(deltaSeconds);
            static_cast<void>(mEngine->tickServerSimulation(simulationDeltaSeconds, deltaSeconds));
        }

        bool collectActorSnapshots(std::vector<mwmp::BaseActorList>& actorLists) override
        {
            if (mEngine == nullptr || !mEngine->isServerSimulationPrepared())
                return false;

            mEngine->exportServerSimulationActorSnapshots(actorLists);
            return !actorLists.empty();
        }

        bool collectPlayerSnapshots(std::vector<mwmp::SimulationPlayerSnapshot>& playerSnapshots) override
        {
            if (mEngine == nullptr || !mEngine->isServerSimulationPrepared())
                return false;

            mFocusState.exportedPlayerSnapshotCount = 0;
            mFocusState.persistentPlayerActorSnapshotCount = 0;
            mFocusState.virtualPlayerSnapshotCount = 0;
            mFocusState.exportedFocusPlayerSnapshot = false;

            const std::size_t initialSnapshotCount = playerSnapshots.size();
            std::vector<mwmp::PacketGuid> exportedGuids;
            auto appendSnapshot = [&](mwmp::SimulationPlayerSnapshot snapshot) {
                if (!mwmp::isPacketGuidAssigned(snapshot.guid))
                    return false;

                if (std::find(exportedGuids.begin(), exportedGuids.end(), snapshot.guid) != exportedGuids.end())
                    return false;

                exportedGuids.push_back(snapshot.guid);
                playerSnapshots.push_back(std::move(snapshot));
                return true;
            };

            mwmp::SimulationPlayerSnapshot focusSnapshot;
            if (mEngine->exportServerSimulationFocusPlayerSnapshot(focusSnapshot)
                && appendSnapshot(std::move(focusSnapshot)))
                mFocusState.exportedFocusPlayerSnapshot = true;

            std::vector<mwmp::SimulationPlayerSnapshot> enginePlayerSnapshots;
            mEngine->exportServerSimulationPlayerActorSnapshots(enginePlayerSnapshots);
            for (mwmp::SimulationPlayerSnapshot& snapshot : enginePlayerSnapshots)
            {
                if (appendSnapshot(std::move(snapshot)))
                    ++mFocusState.persistentPlayerActorSnapshotCount;
            }

            if (mFocusState.persistentPlayerActorSnapshotCount == 0)
            {
                for (const auto& [guid, snapshot] : mPersistentPlayerActors)
                {
                    static_cast<void>(guid);
                    if (appendSnapshot(snapshot))
                        ++mFocusState.persistentPlayerActorSnapshotCount;
                }
            }

            for (const mwmp::SimulationCellFocus& focus : mSimulationFocuses)
            {
                mwmp::SimulationPlayerSnapshot virtualSnapshot = makeVirtualPlayerSnapshot(focus);
                if (appendSnapshot(std::move(virtualSnapshot)))
                    ++mFocusState.virtualPlayerSnapshotCount;
            }

            mFocusState.exportedPlayerSnapshotCount = playerSnapshots.size() - initialSnapshotCount;
            return mFocusState.exportedPlayerSnapshotCount != 0;
        }

        bool startActorCombatWithPlayer(
            const mwmp::SimulationActorTarget& actor, const mwmp::SimulationPlayerTarget& player) override
        {
            if (mEngine == nullptr || !mEngine->isServerSimulationPrepared() || !player.hasPosition)
                return false;

            return mEngine->startServerSimulationActorCombatWithPlayer(
                actor.cell, actor.refId, actor.refNum, actor.mpNum, player.position, player.guid, player.name,
                player.hasStatsDynamicData ? &player.creatureStats : nullptr,
                player.hasBaseInfo ? &player.npc : nullptr,
                player.hasClass ? &player.classId : nullptr,
                player.hasEquipmentData ? &player.equipmentItems : nullptr);
        }

        const mwmp::SimulationRuntimeFocusState& focusState() const override
        {
            return mFocusState;
        }

    private:
        static constexpr float maxQueuedSimulationDeltaSeconds = 0.2f;

        static std::string focusDescription(const mwmp::SimulationCellFocus& focus)
        {
            return focus.cell.getDescription();
        }

        static std::string focusScheduleKey(const mwmp::SimulationCellFocus& focus)
        {
            return focusDescription(focus);
        }

        static mwmp::SimulationPlayerSnapshot makeVirtualPlayerSnapshot(const mwmp::SimulationCellFocus& focus)
        {
            mwmp::SimulationPlayerSnapshot snapshot;
            if (!focus.hasPlayer || !focus.hasPosition)
                return snapshot;

            snapshot.cell = focus.cell;
            snapshot.position = focus.position;
            snapshot.guid = focus.playerGuid;
            snapshot.name = focus.playerName;
            snapshot.hasPositionData = true;

            if (focus.hasPlayerStats)
            {
                snapshot.creatureStats = focus.playerStats;
                snapshot.hasStatsDynamicData = true;
            }

            return snapshot;
        }

        std::size_t countConfiguredPlayerFocuses() const
        {
            std::vector<mwmp::PacketGuid> countedGuids;
            std::size_t result = 0;
            for (const mwmp::SimulationCellFocus& focus : mSimulationFocuses)
            {
                if (!focus.hasPlayer || !focus.hasPosition || !mwmp::isPacketGuidAssigned(focus.playerGuid))
                    continue;

                if (std::find(countedGuids.begin(), countedGuids.end(), focus.playerGuid) != countedGuids.end())
                    continue;

                countedGuids.push_back(focus.playerGuid);
                ++result;
            }

            return result;
        }

        void pruneQueuedFocusDeltas()
        {
            std::set<std::string> activeFocuses;
            for (const mwmp::SimulationCellFocus& focus : mSimulationFocuses)
            {
                const std::string key = focusScheduleKey(focus);
                if (!key.empty())
                    activeFocuses.insert(key);
            }

            for (auto it = mQueuedFocusDeltaSeconds.begin(); it != mQueuedFocusDeltaSeconds.end();)
            {
                if (activeFocuses.find(it->first) == activeFocuses.end())
                    it = mQueuedFocusDeltaSeconds.erase(it);
                else
                    ++it;
            }
        }

        void queueFocusElapsedTime(float deltaSeconds)
        {
            for (const mwmp::SimulationCellFocus& focus : mSimulationFocuses)
            {
                const std::string key = focusScheduleKey(focus);
                if (!key.empty())
                    mQueuedFocusDeltaSeconds[key] += deltaSeconds;
            }
        }

        float focusNextSimulationCell(float fallbackDeltaSeconds)
        {
            if (mEngine == nullptr || mSimulationFocuses.empty())
                return fallbackDeltaSeconds;

            if (mNextSimulationFocus >= mSimulationFocuses.size())
                mNextSimulationFocus = 0;

            const mwmp::SimulationCellFocus& focus = mSimulationFocuses[mNextSimulationFocus];
            mNextSimulationFocus = (mNextSimulationFocus + 1) % mSimulationFocuses.size();
            const std::string description = focusDescription(focus);
            const std::string scheduleKey = focusScheduleKey(focus);
            const auto queuedIt = mQueuedFocusDeltaSeconds.find(scheduleKey);
            const float queuedDeltaSeconds = queuedIt != mQueuedFocusDeltaSeconds.end()
                ? queuedIt->second
                : fallbackDeltaSeconds;
            const float simulationDeltaSeconds = std::min(queuedDeltaSeconds, maxQueuedSimulationDeltaSeconds);

            ++mFocusState.focusAttemptCount;
            mFocusState.lastCellDescription = description;
            mFocusState.lastFocusHadPosition = focus.hasPosition;
            mFocusState.lastFocusSucceeded = mEngine->focusServerSimulationCell(
                focus.cell, focus.hasPosition ? &focus.position : nullptr,
                focus.hasPlayer ? focus.playerGuid : mwmp::unassignedPacketGuid(),
                focus.hasPlayer ? std::string_view(focus.playerName) : std::string_view(),
                focus.hasPlayerStats ? &focus.playerStats : nullptr,
                focus.hasPlayerBaseInfo ? &focus.playerNpc : nullptr,
                focus.hasPlayerClass ? &focus.playerClassId : nullptr,
                focus.hasPlayerEquipmentData ? &focus.playerEquipmentItems : nullptr);
            mFocusState.lastQueuedDeltaSeconds = queuedDeltaSeconds;
            if (mFocusState.lastFocusSucceeded)
            {
                ++mFocusState.focusSuccessCount;
                mFocusState.lastSimulationDeltaSeconds = simulationDeltaSeconds;
                if (queuedDeltaSeconds > maxQueuedSimulationDeltaSeconds)
                    ++mFocusState.focusCatchupClampCount;
                if (queuedIt != mQueuedFocusDeltaSeconds.end())
                    queuedIt->second = 0.f;
                return simulationDeltaSeconds;
            }
            else
            {
                ++mFocusState.focusFailureCount;
                mFocusState.lastSimulationDeltaSeconds = 0.f;
                return 0.f;
            }
        }

        std::unique_ptr<Files::ConfigurationManager> mCfgMgr;
        std::unique_ptr<OMW::Engine> mEngine;
        std::vector<mwmp::SimulationCellFocus> mSimulationFocuses;
        std::map<mwmp::PacketGuid, mwmp::SimulationPlayerSnapshot> mPersistentPlayerActors;
        std::map<std::string, float> mQueuedFocusDeltaSeconds;
        mwmp::SimulationRuntimeFocusState mFocusState;
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
        engine->setServerSimulationSavesPath(getServerOpenMwSavesPath());
        const mwmp::ServerContentDatabaseStatistics contentDatabase = mwmp::ServerContentDatabase::get().statistics();
        engine->setServerSimulationContentFingerprints(
            contentDatabase.contentPlanFingerprint, contentDatabase.worldDatabaseFingerprint,
            contentDatabase.serverWorldCompatibilityFingerprint);

        logHeadlessOpenMwStage("prepareOpenMwServerEngine: configuring OpenMW application");
        Settings::Manager::clear();
        if (!configureOpenMwApplication(static_cast<int>(mutableArguments.size()), argv.data(), *engine, cfgMgr,
                "CommunityMP-server-openmw"))
            return nullptr;
        logHeadlessOpenMwStage("prepareOpenMwServerEngine: OpenMW application configured");

        logHeadlessOpenMwStage("prepareOpenMwServerEngine: checking OSG plugins");
        if (!Misc::checkRequiredOSGPluginsArePresent())
            return nullptr;
        logHeadlessOpenMwStage("prepareOpenMwServerEngine: OSG plugins ready");

        logHeadlessOpenMwStage("prepareOpenMwServerEngine: preparing server simulation");
        engine->prepareServerSimulation();
        if (!engine->isServerSimulationPrepared())
            return nullptr;
        logHeadlessOpenMwStage("prepareOpenMwServerEngine: server simulation prepared");

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
        const bool attemptHeadlessOpenMwRuntime = shouldAttemptHeadlessOpenMwRuntime();

        if (!attemptHeadlessOpenMwRuntime
            && (bootstrap.blockedBy.empty() || bootstrap.blockedBy == "headless-openmw-engine-not-created"))
            bootstrap.blockedBy = "openmw-headless-runtime-disabled";

        if (attemptHeadlessOpenMwRuntime
            && (bootstrap.blockedBy.empty() || bootstrap.blockedBy == "headless-openmw-engine-not-created"))
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
        mwmp::SimulationRuntimeWorldState worldState = buildOpenMwWorldState(engine.get(), hasPreparedEngine);
        return std::make_unique<OpenMwServerSimulationRuntime>(activeKind, buildOpenMwCapabilities(hasPreparedEngine),
            buildOpenMwTopology(hasPreparedEngine), std::move(bootstrap), std::move(worldState), std::move(cfgMgr),
            std::move(engine));
    }
}
