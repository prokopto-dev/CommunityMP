#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/ActorStatsAuthority.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/esm3/creaturestats.hpp>
#include "PacketActorStatsDynamic.hpp"

using namespace mwmp;

PacketActorStatsDynamic::PacketActorStatsDynamic() : ActorPacket()
{
    packetID = ID_ACTOR_STATS_DYNAMIC;
}

void PacketActorStatsDynamic::Actor(BaseActor &actor, bool send)
{
    bool readOk = RW(actor.statsDynamicSequence, send);
    if (!readOk)
        return;

    readOk = RW(actor.creatureStats.mDead, send);
    if (!readOk)
        return;

    readOk = RW(actor.creatureStats.mDeathAnimationFinished, send);
    if (!readOk)
        return;

    readOk = RW(actor.creatureStats.mDynamic, send);
    if (!readOk)
        return;

    if (!send && !hasFiniteActorDynamicStats(actor))
    {
        packetValid = false;
        return;
    }

    actor.hasStatsDynamicData = true;
}
