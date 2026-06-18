#ifndef OPENMW_MP_SIMULATIONRUNTIME_HPP
#define OPENMW_MP_SIMULATIONRUNTIME_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/esm3/loadcell.hpp>

namespace mwmp
{
    class BaseActorList;

    struct SimulationCellFocus
    {
        ESM::Cell cell;
        ESM::Position position;
        bool hasPosition = false;
    };

    struct SimulationRuntimeEventArgument
    {
        enum class Type
        {
            Boolean,
            Integer,
            String
        };

        Type type = Type::String;
        bool booleanValue = false;
        int integerValue = 0;
        std::string stringValue;

        static SimulationRuntimeEventArgument boolean(bool value);
        static SimulationRuntimeEventArgument integer(int value);
        static SimulationRuntimeEventArgument string(std::string value);
    };

    using SimulationRuntimeEventArguments = std::vector<SimulationRuntimeEventArgument>;

    enum class SimulationRuntimeKind
    {
        PacketMirror,
        OpenMwHeadless
    };

    struct SimulationRuntimeCapabilities
    {
        bool ownsWorldState = false;
        bool resolvesCells = false;
        bool runsScripts = false;
        bool runsActorAi = false;
        bool ownsActorMovement = false;
        bool ownsActorCombat = false;
    };

    struct SimulationRuntimeTopology
    {
        bool unifiedExecutable = false;
        bool linksOpenMwCore = false;
        bool hasHeadlessOpenMwEngine = false;
        bool runsOpenMwLua = false;
        bool rendererClientProtocol = false;
    };

    struct SimulationRuntimeBootstrap
    {
        bool canConfigureOpenMwApplication = false;
        bool canLoadOpenMwApplicationSettings = false;
        bool hasOpenMwContentPlan = false;
        bool contentPlanMatchesServerRegistry = false;
        bool usedServerContentFallback = false;
        bool contentRegistryLoaded = false;
        std::size_t contentFileCount = 0;
        std::size_t resolvedOpenMwDataDirCount = 0;
        std::size_t resolvedOpenMwContentFileCount = 0;
        std::size_t missingServerContentFileCount = 0;
        std::size_t extraOpenMwContentFileCount = 0;
        std::size_t engineArgumentCount = 0;
        std::string blockedBy;
    };

    struct SimulationRuntimeWorldState
    {
        bool prepared = false;
        bool persistent = false;
        bool loadedFromSave = false;
        bool initializedNewWorld = false;
        std::string savePath;
        std::string manifestPath;
        std::string contentPlanFingerprint;
        std::string worldDatabaseFingerprint;
    };

    struct SimulationRuntimeFocusState
    {
        std::size_t configuredCellCount = 0;
        std::uint64_t focusAttemptCount = 0;
        std::uint64_t focusSuccessCount = 0;
        std::uint64_t focusFailureCount = 0;
        std::string lastCellDescription;
        bool lastFocusHadPosition = false;
        bool lastFocusSucceeded = false;
    };

    class SimulationRuntime
    {
    public:
        SimulationRuntime();
        explicit SimulationRuntime(SimulationRuntimeKind requestedKind);
        SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
            SimulationRuntimeCapabilities capabilities);
        SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
            SimulationRuntimeCapabilities capabilities, SimulationRuntimeTopology topology);
        SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
            SimulationRuntimeCapabilities capabilities, SimulationRuntimeTopology topology,
            SimulationRuntimeBootstrap bootstrap);
        SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
            SimulationRuntimeCapabilities capabilities, SimulationRuntimeTopology topology,
            SimulationRuntimeBootstrap bootstrap, SimulationRuntimeWorldState worldState);
        virtual ~SimulationRuntime() = default;

        virtual SimulationRuntimeKind requestedKind() const;
        virtual SimulationRuntimeKind activeKind() const;
        virtual const SimulationRuntimeCapabilities& capabilities() const;
        virtual const SimulationRuntimeTopology& topology() const;
        virtual const SimulationRuntimeBootstrap& bootstrap() const;
        virtual const SimulationRuntimeWorldState& worldState() const;
        virtual const SimulationRuntimeFocusState& focusState() const;

        virtual bool hasOpenMwWorld() const;
        virtual bool hasPersistentWorld() const;
        virtual bool hasHeadlessOpenMwEngine() const;
        virtual bool canSimulateActors() const;
        virtual bool canOwnActorAuthority() const;

        virtual void tick(float deltaSeconds);
        virtual void setSimulationCellFocuses(const std::vector<SimulationCellFocus>& focuses);
        virtual bool collectActorSnapshots(std::vector<BaseActorList>& actorLists);
        virtual bool dispatchServerEvent(
            std::string_view eventName, const SimulationRuntimeEventArguments& arguments);

        virtual const char* requestedName() const;
        virtual const char* activeName() const;

    private:
        SimulationRuntimeKind mRequestedKind;
        SimulationRuntimeKind mActiveKind;
        SimulationRuntimeCapabilities mCapabilities;
        SimulationRuntimeTopology mTopology;
        SimulationRuntimeBootstrap mBootstrap;
        SimulationRuntimeWorldState mWorldState;
        SimulationRuntimeFocusState mFocusState;
    };

    using SimulationRuntimeFactory = std::unique_ptr<SimulationRuntime> (*)();

    std::unique_ptr<SimulationRuntime> makePacketMirrorSimulationRuntime();
    std::unique_ptr<SimulationRuntime> createSimulationRuntime();
    void setSimulationRuntimeFactory(SimulationRuntimeFactory factory);
}

#endif // OPENMW_MP_SIMULATIONRUNTIME_HPP
