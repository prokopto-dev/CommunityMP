#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimPlay.hpp"

mwmp::PacketPlayerAnimPlay::PacketPlayerAnimPlay() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_PLAY;
    orderChannel = CHANNEL_COMBAT;
}

void mwmp::PacketPlayerAnimPlay::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    bool readOk = RW(player->combatSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->positionSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->position, send, true);
    if (!readOk)
        return;

    readOk = RW(player->direction, send, true);
    if (!readOk)
        return;

    float sampleInterval = sanitizeMovementSampleIntervalSeconds(player->movementSampleIntervalSeconds);
    readOk = RW(sampleInterval, send);
    if (!readOk)
        return;

    player->movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(sampleInterval);
    float latencySeconds = sanitizeMovementLatencySeconds(player->movementLatencySeconds);
    readOk = RW(latencySeconds, send);
    if (!readOk)
        return;

    player->movementLatencySeconds = sanitizeMovementLatencySeconds(latencySeconds);

    readOk = RW(player->animation.groupname, send);
    if (!readOk)
        return;

    readOk = RW(player->animation.mode, send);
    if (!readOk)
        return;

    readOk = RW(player->animation.count, send);
    if (!readOk)
        return;

    readOk = RW(player->animation.persist, send);
    if (!readOk)
        return;
}
