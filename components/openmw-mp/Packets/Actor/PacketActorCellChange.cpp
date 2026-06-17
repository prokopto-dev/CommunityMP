#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorCellChange.hpp"

using namespace mwmp;

PacketActorCellChange::PacketActorCellChange() : ActorPacket()
{
    packetID = ID_ACTOR_CELL_CHANGE;
}

void PacketActorCellChange::Actor(BaseActor &actor, bool send)
{
    bool readOk = RW(actor.cell.mData, send, true);
    if (!readOk)
        return;

    readOk = RW(actor.cell.mName, send, true);
    if (!readOk)
        return;

    readOk = RW(actor.positionSequence, send);
    if (!readOk)
        return;

    readOk = RW(actor.position, send, true);
    if (!readOk)
        return;

    readOk = RW(actor.direction, send, true);
    if (!readOk)
        return;

    readOk = RW(actor.isFollowerCellChange, send);
    if (!readOk)
        return;

    actor.hasPositionData = true;
}
