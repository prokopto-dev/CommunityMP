#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAnimPlay.hpp"

using namespace mwmp;

PacketActorAnimPlay::PacketActorAnimPlay() : ActorPacket()
{
    packetID = ID_ACTOR_ANIM_PLAY;
}

void PacketActorAnimPlay::Actor(BaseActor &actor, bool send)
{
    bool readOk = RW(actor.combatSequence, send);
    if (!readOk)
        return;

    if (!send)
        actor.hasCombatData = true;

    readOk = RW(actor.hasPositionData, send);
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
    }

    readOk = RW(actor.animation.groupname, send);
    if (!readOk)
        return;

    readOk = RW(actor.animation.mode, send);
    if (!readOk)
        return;

    readOk = RW(actor.animation.count, send);
    if (!readOk)
        return;

    readOk = RW(actor.animation.persist, send);
    if (!readOk)
        return;
}
