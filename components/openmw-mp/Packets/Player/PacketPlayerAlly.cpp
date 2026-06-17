#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAlly.hpp"

namespace
{
    constexpr uint32_t maxAlliedPlayers = 1000;
}

mwmp::PacketPlayerAlly::PacketPlayerAlly() : PlayerPacket()
{
    packetID = ID_PLAYER_ALLY;
}

void mwmp::PacketPlayerAlly::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->alliedPlayers.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxAlliedPlayers)
        {
            packetValid = false;
            player->alliedPlayers.clear();
            return;
        }

        player->alliedPlayers.clear();
        player->alliedPlayers.resize(count);
    }

    for (auto &&teamPlayerGuid : player->alliedPlayers)
    {
        RW(teamPlayerGuid, send, true);
    }
}
