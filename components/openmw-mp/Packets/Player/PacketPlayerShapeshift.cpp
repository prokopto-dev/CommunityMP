#include "PacketPlayerShapeshift.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerShapeshift::PacketPlayerShapeshift() : PlayerPacket()
{
    packetID = ID_PLAYER_SHAPESHIFT;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerShapeshift::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->scale, send);
    RW(player->isWerewolf, send);

    RW(player->displayCreatureName, send);
    RW(player->creatureRefId, send, true);
}
