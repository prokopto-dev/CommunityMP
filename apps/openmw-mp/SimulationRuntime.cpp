#include "SimulationRuntime.hpp"

#include <utility>

namespace
{
    const char* runtimeKindName(mwmp::SimulationRuntimeKind kind)
    {
        switch (kind)
        {
            case mwmp::SimulationRuntimeKind::PacketMirror:
                return "packet-mirror";

            case mwmp::SimulationRuntimeKind::OpenMwHeadless:
                return "openmw-headless";
        }

        return "unknown";
    }

    mwmp::SimulationRuntimeCapabilities packetMirrorCapabilities()
    {
        return {};
    }

    mwmp::SimulationRuntimeFactory& simulationRuntimeFactory()
    {
        static mwmp::SimulationRuntimeFactory factory = nullptr;
        return factory;
    }
}

namespace mwmp
{
    SimulationRuntimeEventArgument SimulationRuntimeEventArgument::boolean(bool value)
    {
        SimulationRuntimeEventArgument argument;
        argument.type = Type::Boolean;
        argument.booleanValue = value;
        return argument;
    }

    SimulationRuntimeEventArgument SimulationRuntimeEventArgument::integer(int value)
    {
        SimulationRuntimeEventArgument argument;
        argument.type = Type::Integer;
        argument.integerValue = value;
        return argument;
    }

    SimulationRuntimeEventArgument SimulationRuntimeEventArgument::string(std::string value)
    {
        SimulationRuntimeEventArgument argument;
        argument.type = Type::String;
        argument.stringValue = std::move(value);
        return argument;
    }

    SimulationRuntime::SimulationRuntime()
        : SimulationRuntime(SimulationRuntimeKind::PacketMirror)
    {
    }

    SimulationRuntime::SimulationRuntime(SimulationRuntimeKind requestedKind)
        : mRequestedKind(requestedKind)
        , mActiveKind(SimulationRuntimeKind::PacketMirror)
        , mCapabilities(packetMirrorCapabilities())
    {
        // The unified executable links the OpenMW client core, but dedicated
        // server mode does not yet construct a headless OMW::Engine. Until it
        // does, keep the active runtime honest and refuse actor authority.
    }

    SimulationRuntime::SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
        SimulationRuntimeCapabilities capabilities)
        : mRequestedKind(requestedKind)
        , mActiveKind(activeKind)
        , mCapabilities(capabilities)
    {
    }

    SimulationRuntimeKind SimulationRuntime::requestedKind() const
    {
        return mRequestedKind;
    }

    SimulationRuntimeKind SimulationRuntime::activeKind() const
    {
        return mActiveKind;
    }

    const SimulationRuntimeCapabilities& SimulationRuntime::capabilities() const
    {
        return mCapabilities;
    }

    bool SimulationRuntime::hasOpenMwWorld() const
    {
        return mCapabilities.ownsWorldState && mCapabilities.resolvesCells;
    }

    bool SimulationRuntime::canSimulateActors() const
    {
        return mCapabilities.runsActorAi && mCapabilities.ownsActorMovement;
    }

    bool SimulationRuntime::canOwnActorAuthority() const
    {
        return canSimulateActors() && mCapabilities.ownsActorCombat;
    }

    void SimulationRuntime::tick(float deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

    bool SimulationRuntime::dispatchServerEvent(
        std::string_view eventName, const SimulationRuntimeEventArguments& arguments)
    {
        static_cast<void>(eventName);
        static_cast<void>(arguments);
        return false;
    }

    const char* SimulationRuntime::requestedName() const
    {
        return runtimeKindName(mRequestedKind);
    }

    const char* SimulationRuntime::activeName() const
    {
        return runtimeKindName(mActiveKind);
    }

    std::unique_ptr<SimulationRuntime> makePacketMirrorSimulationRuntime()
    {
        return std::make_unique<SimulationRuntime>(SimulationRuntimeKind::PacketMirror);
    }

    std::unique_ptr<SimulationRuntime> createSimulationRuntime()
    {
        if (SimulationRuntimeFactory factory = simulationRuntimeFactory())
        {
            if (std::unique_ptr<SimulationRuntime> runtime = factory())
                return runtime;
        }

        return makePacketMirrorSimulationRuntime();
    }

    void setSimulationRuntimeFactory(SimulationRuntimeFactory factory)
    {
        simulationRuntimeFactory() = factory;
    }
}
