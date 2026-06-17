#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellChange.hpp"


mwmp::PacketPlayerCellChange::PacketPlayerCellChange() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_CHANGE;
    priority = PacketPriority::Immediate;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_PLAYER;
}

void mwmp::PacketPlayerCellChange::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    bool readOk = RW(player->cell.mData, send, true);
    if (!readOk)
        return;

    readOk = RW(player->cell.mName, send, true);
    if (!readOk)
        return;

    readOk = RW(player->positionSequence, send);
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

    readOk = RW(player->cellChangeReason, send);
    if (!readOk)
        return;

    if (!send && !mwmp::isValidCellChangeReason(player->cellChangeReason))
    {
        packetValid = false;
        return;
    }

    if (!send && !player->hasFinitePositionPacket())
    {
        packetValid = false;
        return;
    }

    readOk = RW(player->previousCellPosition.pos, send, true);
    if (!readOk)
        return;

    readOk = RW(player->isChangingRegion, send);
    if (!readOk)
        return;

    if (player->isChangingRegion)
    {
        readOk = RW(player->cell.mRegion, send, true);
        if (!readOk)
            return;
    }
}
