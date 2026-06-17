#include "PacketPlayerDeath.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerDeath::PacketPlayerDeath() : PlayerPacket()
{
    packetID = ID_PLAYER_DEATH;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerDeath::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    if (!RW(player->combatSequence, send))
        return;

    if (!RW(player->positionSequence, send) || !RW(player->position, send, true)
        || !RW(player->direction, send, true))
        return;

    if (!RW(player->deathState, send) || !RW(player->killer.isPlayer, send))
        return;

    if (player->killer.isPlayer)
    {
        if (!RW(player->killer.guid, send))
            return;
    }
    else
    {
        if (!RW(player->killer.refId, send, true) || !RW(player->killer.refNum, send)
            || !RW(player->killer.mpNum, send))
            return;

        if (!RW(player->killer.name, send, true))
            return;
    }
}
