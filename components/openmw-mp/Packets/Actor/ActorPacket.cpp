#include <components/openmw-mp/NetworkMessages.hpp>
#include "ActorPacket.hpp"

using namespace mwmp;

ActorPacket::ActorPacket() : BasePacket()
{
    packetID = 0;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_ACTOR;
}

ActorPacket::~ActorPacket()
{

}

void ActorPacket::setActorList(BaseActorList *newActorList)
{
    actorList = newActorList;
    guid = actorList->guid;
}

void ActorPacket::Packet(PacketStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseActor actor;

    for (unsigned int i = 0; i < actorList->count; i++)
    {
        if (send)
            actor = actorList->baseActors.at(i);

        if (!RW(actor.refNum, send) || !RW(actor.mpNum, send))
        {
            actorList->isValid = false;
            return;
        }

        Actor(actor, send);

        if (!packetValid)
        {
            actorList->isValid = false;
            return;
        }

        if (!send)
            actorList->baseActors.push_back(actor);
    }
}

bool ActorPacket::PacketHeader(PacketStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    if (!RW(actorList->cell.mData, send, true) || !RW(actorList->cell.mName, send, true))
    {
        actorList->isValid = false;
        return false;
    }

    if (!send && actorList->cell.isExterior() && !actorList->cell.mName.empty())
    {
        packetValid = false;
        actorList->isValid = false;
        return false;
    }

    if (send)
        actorList->count = (unsigned int)(actorList->baseActors.size());
    else
    {
        actorList->baseActors.clear();
        actorList->count = 0;
    }

    if (!RW(actorList->count, send))
    {
        actorList->isValid = false;
        return false;
    }

    if (actorList->count > maxActors)
    {
        actorList->isValid = false;
        return false;
    }

    return true;
}


void ActorPacket::Actor(BaseActor &actor, bool send)
{

}
