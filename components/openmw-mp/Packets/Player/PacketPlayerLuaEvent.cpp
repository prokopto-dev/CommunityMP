#include "PacketPlayerLuaEvent.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

using namespace mwmp;

PacketPlayerLuaEvent::PacketPlayerLuaEvent()
    : PlayerPacket()
{
    packetID = ID_PLAYER_LUA_EVENT;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_PLAYER;
}

void PacketPlayerLuaEvent::Packet(PacketStream* newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    if (!RW(player->luaEvent.schemaVersion, send))
        return;
    if (!RW(player->luaEvent.sequence, send))
        return;
    if (!ExchangeBoundedString(player->luaEvent.namespaceName, send, clientLuaEventMaxNamespaceLength))
        return;
    if (!ExchangeBoundedString(player->luaEvent.eventName, send, clientLuaEventMaxNameLength))
        return;
    if (!ExchangeBoundedString(player->luaEvent.payload, send, clientLuaEventMaxPayloadLength))
        return;

    if (!send && !player->hasValidClientLuaEvent())
        packetValid = false;
}

bool PacketPlayerLuaEvent::ExchangeBoundedString(std::string& value, bool send, std::string::size_type maxSize)
{
    if (send)
    {
        if (value.size() > maxSize || value.size() > static_cast<std::string::size_type>(std::numeric_limits<std::uint32_t>::max()))
        {
            packetValid = false;
            return false;
        }

        const std::uint32_t serializedSize = static_cast<std::uint32_t>(value.size());
        bs->Write(serializedSize);
        if (serializedSize > 0)
            bs->Write(value.data(), serializedSize);
        return true;
    }

    std::uint32_t serializedSize = 0;
    if (!bs->Read(serializedSize))
    {
        packetValid = false;
        return false;
    }

    if (serializedSize > maxSize)
    {
        packetValid = false;
        value.clear();
        return false;
    }

    std::string readValue(serializedSize, '\0');
    if (serializedSize > 0 && !bs->Read(readValue.data(), serializedSize))
    {
        packetValid = false;
        value.clear();
        return false;
    }

    value = std::move(readValue);
    return true;
}
