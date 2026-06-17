#include "communitympbindings.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include <components/lua/util.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include "context.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/CommunityMpLuaEventQueue.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#endif

namespace MWLua
{
    namespace
    {
        bool isValidLuaEventName(const std::string& value, std::size_t maxLength)
        {
            if (value.empty() || value.size() > maxLength)
                return false;

            for (const char c : value)
            {
                const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                if (!valid)
                    return false;
            }

            return true;
        }

        bool isConnectedToCommunityMpServer()
        {
#ifdef BUILD_TES3MP_CLIENT
            if (!mwmp::Main::isInitialized())
                return false;

            const mwmp::Main& main = mwmp::Main::get();
            mwmp::Networking* networking = main.getNetworking();
            mwmp::LocalPlayer* localPlayer = main.getLocalPlayer();
            return networking != nullptr && networking->isConnected() && localPlayer != nullptr
                && localPlayer->isLoggedIn();
#else
            return false;
#endif
        }

        bool sendPlayerEvent(std::string namespaceName, std::string eventName, std::string payloadJson)
        {
            if (!isValidLuaEventName(namespaceName, mwmp::clientLuaEventMaxNamespaceLength))
                throw std::runtime_error("Invalid CommunityMP Lua event namespace");

            if (!isValidLuaEventName(eventName, mwmp::clientLuaEventMaxNameLength))
                throw std::runtime_error("Invalid CommunityMP Lua event name");

            if (payloadJson.size() > mwmp::clientLuaEventMaxPayloadLength)
                throw std::runtime_error("CommunityMP Lua event payload is too large");

#ifdef BUILD_TES3MP_CLIENT
            if (!isConnectedToCommunityMpServer())
                return false;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            localPlayer->luaEvent.schemaVersion = mwmp::clientLuaEventSchemaVersion;
            localPlayer->luaEvent.sequence += 1;
            localPlayer->luaEvent.namespaceName = std::move(namespaceName);
            localPlayer->luaEvent.eventName = std::move(eventName);
            localPlayer->luaEvent.payload = std::move(payloadJson);

            mwmp::PlayerPacket* packet = mwmp::Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_LUA_EVENT);
            packet->setPlayer(localPlayer);
            packet->Send();
            return true;
#else
            static_cast<void>(namespaceName);
            static_cast<void>(eventName);
            static_cast<void>(payloadJson);
            return false;
#endif
        }

        sol::table receiveServerEvents(sol::this_state state)
        {
            sol::state_view lua(state);
            sol::table events(lua, sol::create);

#ifdef BUILD_TES3MP_CLIENT
            std::vector<mwmp::CommunityMpLuaEvent> queuedEvents
                = mwmp::CommunityMpLuaEventQueue::takeServerEvents();

            int index = 1;
            for (const mwmp::CommunityMpLuaEvent& event : queuedEvents)
            {
                sol::table luaEvent(lua, sol::create);
                luaEvent["schemaVersion"] = event.schemaVersion;
                luaEvent["sequence"] = event.sequence;
                luaEvent["namespaceName"] = event.namespaceName;
                luaEvent["eventName"] = event.eventName;
                luaEvent["payload"] = event.payload;
                luaEvent["subjectGuid"] = mwmp::packetGuidToString(event.subjectGuid);
                luaEvent["subjectIsLocal"] = event.subjectIsLocal;
                events[index++] = luaEvent;
            }
#endif

            return events;
        }
    }

    sol::table initCommunityMpPackage(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);

        api["BRIDGE_SCHEMA_VERSION"] = mwmp::clientLuaEventSchemaVersion;
        api["MAX_PAYLOAD_BYTES"] = mwmp::clientLuaEventMaxPayloadLength;
        api["isConnected"] = isConnectedToCommunityMpServer;
        api["sendPlayerEvent"] = sendPlayerEvent;
        api["receiveServerEvents"] = receiveServerEvents;

        return LuaUtil::makeReadOnly(api);
    }
}
