#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAI.hpp"

using namespace mwmp;

PacketActorAI::PacketActorAI() : ActorPacket()
{
    packetID = ID_ACTOR_AI;
}

void PacketActorAI::Actor(BaseActor &actor, bool send)
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

    readOk = RW(actor.aiAction, send);
    if (!readOk)
        return;

    if (actor.aiAction != mwmp::BaseActorList::CANCEL)
    {
        if (actor.aiAction == mwmp::BaseActorList::WANDER)
        {
            readOk = RW(actor.aiDistance, send);
            if (!readOk)
                return;

            readOk = RW(actor.aiShouldRepeat, send);
            if (!readOk)
                return;
        }

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::WANDER)
        {
            readOk = RW(actor.aiDuration, send);
            if (!readOk)
                return;
        }

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::TRAVEL)
        {
            readOk = RW(actor.aiCoordinates, send);
            if (!readOk)
                return;
        }

        if (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT ||
            actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
        {
            readOk = RW(actor.hasAiTarget, send);
            if (!readOk)
                return;

            if (actor.hasAiTarget)
            {
                readOk = RW(actor.aiTarget.isPlayer, send);
                if (!readOk)
                    return;

                if (actor.aiTarget.isPlayer)
                {
                    readOk = RW(actor.aiTarget.guid, send);
                    if (!readOk)
                        return;
                }
                else
                {
                    readOk = RW(actor.aiTarget.refId, send, true);
                    if (!readOk)
                        return;

                    readOk = RW(actor.aiTarget.refNum, send);
                    if (!readOk)
                        return;

                    readOk = RW(actor.aiTarget.mpNum, send);
                    if (!readOk)
                        return;
                }
            }
        }
    }
}
