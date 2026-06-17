#include "PacketPlayerPosition.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerPosition::PacketPlayerPosition() : PlayerPacket()
{
    packetID = ID_PLAYER_POSITION;
    priority = PacketPriority::High;
    reliability = PacketReliability::UnreliableSequenced;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerPosition::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    bool readOk = RW(player->positionSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->position, send, 1);
    if (!readOk)
        return;

    readOk = RW(player->direction, send, 1);
    if (!readOk)
        return;
}
