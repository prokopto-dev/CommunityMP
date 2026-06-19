#include "PacketPlayerResurrect.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

PacketPlayerResurrect::PacketPlayerResurrect() : PlayerPacket()
{
    packetID = ID_PLAYER_RESURRECT;
}

void PacketPlayerResurrect::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    if (!RW(player->resurrectType, send))
        return;

    if (!RW(player->statsDynamicSequence, send))
        return;

    if (!RW(player->creatureStats.mDead, send))
        return;

    if (!RW(player->creatureStats.mDynamic, send))
        return;

    if (!send && !player->hasFiniteDynamicStats())
    {
        player->restoreAcceptedStatsDynamicPacket();
        packetValid = false;
    }
}
