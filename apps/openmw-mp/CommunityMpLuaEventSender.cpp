#include "CommunityMpLuaEventSender.hpp"

#include <map>
#include <utility>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

#include "Player.hpp"
#include "ServerNetworking.hpp"

namespace
{
    std::map<mwmp::PacketGuid, std::uint32_t> sServerLuaEventSequences;
}

namespace mwmp
{
    bool CommunityMpLuaEventSender::isValidEventName(std::string_view value, std::size_t maxLength)
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

    bool CommunityMpLuaEventSender::sendToPlayer(
        Player& player, std::string namespaceName, std::string eventName, std::string payloadJson)
    {
        if (!isValidEventName(namespaceName, clientLuaEventMaxNamespaceLength)
            || !isValidEventName(eventName, clientLuaEventMaxNameLength)
            || payloadJson.size() > clientLuaEventMaxPayloadLength)
            return false;

        ServerNetworking* networking = ServerNetworking::getPtr();
        if (networking == nullptr || networking->getPlayerPacketController() == nullptr)
            return false;

        PlayerPacket* packet = networking->getPlayerPacketController()->GetPacket(ID_PLAYER_LUA_EVENT);
        if (packet == nullptr)
            return false;

        const ClientLuaEvent previousEvent = player.luaEvent;

        player.luaEvent.schemaVersion = clientLuaEventSchemaVersion;
        player.luaEvent.sequence = ++sServerLuaEventSequences[player.guid];
        player.luaEvent.namespaceName = std::move(namespaceName);
        player.luaEvent.eventName = std::move(eventName);
        player.luaEvent.payload = std::move(payloadJson);

        const bool canSend = player.hasValidClientLuaEvent();
        if (canSend)
        {
            packet->setPlayer(&player);
            packet->Send(player.guid);
        }

        player.luaEvent = previousEvent;
        return canSend;
    }

    void CommunityMpLuaEventSender::clearPlayer(PacketGuid guid)
    {
        sServerLuaEventSequences.erase(guid);
    }
}
