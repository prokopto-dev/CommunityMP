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
}
