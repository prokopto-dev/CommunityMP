#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimFlags.hpp"

mwmp::PacketPlayerAnimFlags::PacketPlayerAnimFlags() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_FLAGS;
    reliability = PacketReliability::UnreliableSequenced;
    orderChannel = CHANNEL_MOVEMENT;
}

void mwmp::PacketPlayerAnimFlags::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    bool readOk = RW(player->positionSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->position, send, true);
    if (!readOk)
        return;

    readOk = RW(player->direction, send, true);
    if (!readOk)
        return;

    readOk = RW(player->animFlagsSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->movementFlags, send);
    if (!readOk)
        return;

    readOk = RW(player->drawState, send);
    if (!readOk)
        return;

    readOk = RW(player->isJumping, send);
    if (!readOk)
        return;

    readOk = RW(player->isFlying, send);
    if (!readOk)
        return;

    readOk = RW(player->hasTcl, send);
    if (!readOk)
        return;
}
