#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAnimFlags.hpp"

using namespace mwmp;

PacketActorAnimFlags::PacketActorAnimFlags() : ActorPacket()
{
    packetID = ID_ACTOR_ANIM_FLAGS;
    reliability = PacketReliability::UnreliableSequenced;
}

void PacketActorAnimFlags::Actor(BaseActor &actor, bool send)
{
    bool readOk = RW(actor.hasPositionData, send);
    if (!readOk)
        return;

    if (actor.hasPositionData)
    {
        readOk = RW(actor.positionSequence, send);
        if (!readOk)
            return;

        readOk = RW(actor.position, send, true);
        if (!readOk)
            return;

        readOk = RW(actor.direction, send, true);
        if (!readOk)
            return;

        float sampleInterval = sanitizeMovementSampleIntervalSeconds(actor.movementSampleIntervalSeconds);
        readOk = RW(sampleInterval, send);
        if (!readOk)
            return;

        actor.movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(sampleInterval);
        float latencySeconds = sanitizeMovementLatencySeconds(actor.movementLatencySeconds);
        readOk = RW(latencySeconds, send);
        if (!readOk)
            return;

        actor.movementLatencySeconds = sanitizeMovementLatencySeconds(latencySeconds);
    }

    readOk = RW(actor.animFlagsSequence, send);
    if (!readOk)
        return;

    readOk = RW(actor.movementFlags, send);
    if (!readOk)
        return;

    readOk = RW(actor.drawState, send);
    if (!readOk)
        return;

    readOk = RW(actor.isJumping, send);
    if (!readOk)
        return;

    readOk = RW(actor.isFlying, send);
    if (!readOk)
        return;

    actor.hasAnimFlagsData = true;
}
