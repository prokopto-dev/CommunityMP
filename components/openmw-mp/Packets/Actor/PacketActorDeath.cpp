#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorDeath.hpp"

using namespace mwmp;

PacketActorDeath::PacketActorDeath() : ActorPacket()
{
    packetID = ID_ACTOR_DEATH;
}

void PacketActorDeath::Actor(BaseActor &actor, bool send)
{
    if (!RW(actor.refId, send))
        return;

    if (!RW(actor.statsDynamicSequence, send))
        return;

    if (!RW(actor.hasPositionData, send))
        return;

    if (actor.hasPositionData)
    {
        if (!RW(actor.positionSequence, send) || !RW(actor.position, send, true)
            || !RW(actor.direction, send, true))
            return;
    }

    if (!RW(actor.deathState, send) || !RW(actor.isInstantDeath, send) || !RW(actor.killer.isPlayer, send))
        return;

    if (actor.killer.isPlayer)
    {
        if (!RW(actor.killer.guid, send))
            return;
    }
    else
    {
        if (!RW(actor.killer.refId, send, true) || !RW(actor.killer.refNum, send)
            || !RW(actor.killer.mpNum, send))
            return;

        if (!RW(actor.killer.name, send, true))
            return;
    }
}
