#include "SimulationRuntime.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <components/openmw-mp/Base/BasePlayer.hpp>

#include "CommunityMpLuaEventSender.hpp"
#include "Player.hpp"

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

    bool startsWith(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    bool isPlayerScopedServerEvent(std::string_view eventName)
    {
        return startsWith(eventName, "OnPlayer")
            || startsWith(eventName, "OnActor")
            || startsWith(eventName, "OnObject")
            || startsWith(eventName, "OnDoor")
            || eventName == "OnGUIAction"
            || eventName == "OnCellLoad"
            || eventName == "OnCellUnload"
            || eventName == "OnClientScriptGlobal"
            || eventName == "OnClientScriptLocal"
            || eventName == "OnConsoleCommand"
            || eventName == "OnContainer"
            || eventName == "OnRecordDynamic"
            || eventName == "OnVideoPlay"
            || eventName == "OnWorldKillCount"
            || eventName == "OnWorldMap"
            || eventName == "OnWorldWeather";
    }

    void appendJsonString(std::string& result, std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";

        result.push_back('"');
        for (const unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        result += "\\u00";
                        result.push_back(hex[(c >> 4) & 0x0f]);
                        result.push_back(hex[c & 0x0f]);
                    }
                    else
                        result.push_back(static_cast<char>(c));
            }
        }
        result.push_back('"');
    }

    void appendJsonArgument(std::string& result, const mwmp::SimulationRuntimeEventArgument& argument)
    {
        switch (argument.type)
        {
            case mwmp::SimulationRuntimeEventArgument::Type::Boolean:
                result += argument.booleanValue ? "true" : "false";
                break;
            case mwmp::SimulationRuntimeEventArgument::Type::Integer:
                result += std::to_string(argument.integerValue);
                break;
            case mwmp::SimulationRuntimeEventArgument::Type::String:
                appendJsonString(result, argument.stringValue);
                break;
        }
    }

    std::string makeServerEventPayload(
        std::string_view eventName, const mwmp::SimulationRuntimeEventArguments& arguments)
    {
        std::string payload;
        payload.reserve(64 + eventName.size());
        payload += "{\"schema\":1,\"eventName\":";
        appendJsonString(payload, eventName);
        payload += ",\"arguments\":[";

        bool first = true;
        for (const mwmp::SimulationRuntimeEventArgument& argument : arguments)
        {
            if (!first)
                payload.push_back(',');
            first = false;
            appendJsonArgument(payload, argument);
        }

        payload += "]}";
        return payload;
    }

    void mirrorPlayerScopedServerEvent(
        std::string_view eventName, const mwmp::SimulationRuntimeEventArguments& arguments)
    {
        if (!isPlayerScopedServerEvent(eventName)
            || arguments.empty()
            || arguments.front().type != mwmp::SimulationRuntimeEventArgument::Type::Integer)
            return;

        const int playerId = arguments.front().integerValue;
        if (playerId < 0 || playerId > std::numeric_limits<unsigned short>::max())
            return;

        Player* player = Players::getPlayer(static_cast<unsigned short>(playerId));
        if (player == nullptr || !player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
            return;

        std::string payload = makeServerEventPayload(eventName, arguments);
        if (payload.size() > mwmp::clientLuaEventMaxPayloadLength)
            return;

        static_cast<void>(mwmp::CommunityMpLuaEventSender::sendToPlayer(
            *player, "communitymp.server", std::string(eventName), std::move(payload)));
    }

    mwmp::SimulationRuntimeCapabilities packetMirrorCapabilities()
    {
        return {};
    }

    mwmp::SimulationRuntimeTopology packetMirrorTopology()
    {
        return {};
    }

    mwmp::SimulationRuntimeBootstrap packetMirrorBootstrap()
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
        , mTopology(packetMirrorTopology())
        , mBootstrap(packetMirrorBootstrap())
    {
        // The unified executable links the OpenMW client core, but dedicated
        // server mode does not yet construct a headless OMW::Engine. Until it
        // does, keep the active runtime honest and refuse actor authority.
    }

    SimulationRuntime::SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
        SimulationRuntimeCapabilities capabilities)
        : SimulationRuntime(requestedKind, activeKind, capabilities, packetMirrorTopology())
    {
    }

    SimulationRuntime::SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
        SimulationRuntimeCapabilities capabilities, SimulationRuntimeTopology topology)
        : SimulationRuntime(requestedKind, activeKind, capabilities, topology, packetMirrorBootstrap())
    {
    }

    SimulationRuntime::SimulationRuntime(SimulationRuntimeKind requestedKind, SimulationRuntimeKind activeKind,
        SimulationRuntimeCapabilities capabilities, SimulationRuntimeTopology topology, SimulationRuntimeBootstrap bootstrap)
        : mRequestedKind(requestedKind)
        , mActiveKind(activeKind)
        , mCapabilities(capabilities)
        , mTopology(topology)
        , mBootstrap(std::move(bootstrap))
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

    const SimulationRuntimeTopology& SimulationRuntime::topology() const
    {
        return mTopology;
    }

    const SimulationRuntimeBootstrap& SimulationRuntime::bootstrap() const
    {
        return mBootstrap;
    }

    bool SimulationRuntime::hasOpenMwWorld() const
    {
        return mCapabilities.ownsWorldState && mCapabilities.resolvesCells;
    }

    bool SimulationRuntime::hasHeadlessOpenMwEngine() const
    {
        return mTopology.hasHeadlessOpenMwEngine;
    }

    bool SimulationRuntime::canSimulateActors() const
    {
        return hasHeadlessOpenMwEngine() && mCapabilities.runsActorAi && mCapabilities.ownsActorMovement;
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
        mirrorPlayerScopedServerEvent(eventName, arguments);

        // This mirror is a migration bridge for the OpenMW LuaJIT side, not a
        // claim that the runtime fully handled the old server Lua callback.
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
