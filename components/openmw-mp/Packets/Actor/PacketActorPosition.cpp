#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorPosition.hpp"

using namespace mwmp;

PacketActorPosition::PacketActorPosition() : ActorPacket()
{
    packetID = ID_ACTOR_POSITION;
    reliability = PacketReliability::UnreliableSequenced;
}

void PacketActorPosition::Actor(BaseActor &actor, bool send)
{
    bool readOk = RW(actor.positionSequence, send);
    if (!readOk)
        return;

    readOk = RW(actor.position, send, true);
    if (!readOk)
        return;

    readOk = RW(actor.direction, send, true);
    if (!readOk)
        return;

    actor.hasPositionData = true;
}
