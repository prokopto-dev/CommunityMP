#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectHit.hpp"

using namespace mwmp;

PacketObjectHit::PacketObjectHit() : ObjectPacket()
{
    packetID = ID_OBJECT_HIT;
    hasCellData = true;
}

void PacketObjectHit::Packet(PacketStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        if (!RW(baseObject.isPlayer, send))
        {
            objectList->isValid = false;
            return;
        }

        if (baseObject.isPlayer)
        {
            if (!RW(baseObject.guid, send))
            {
                objectList->isValid = false;
                return;
            }
        }
        else
        {
            Object(baseObject, send);

            if (!packetValid)
            {
                objectList->isValid = false;
                return;
            }
        }

        if (!RW(baseObject.hittingActor.isPlayer, send))
        {
            objectList->isValid = false;
            return;
        }

        if (baseObject.hittingActor.isPlayer)
        {
            if (!RW(baseObject.hittingActor.guid, send))
            {
                objectList->isValid = false;
                return;
            }
        }
        else
        {
            if (!RW(baseObject.hittingActor.refId, send, true) || !RW(baseObject.hittingActor.refNum, send)
                || !RW(baseObject.hittingActor.mpNum, send) || !RW(baseObject.hittingActor.name, send))
            {
                objectList->isValid = false;
                return;
            }
        }

        if (!RW(baseObject.hitAttack.success, send))
        {
            objectList->isValid = false;
            return;
        }

        if (baseObject.hitAttack.success)
        {
            if (!RW(baseObject.hitAttack.damage, send) || !RW(baseObject.hitAttack.block, send)
                || !RW(baseObject.hitAttack.knockdown, send))
            {
                objectList->isValid = false;
                return;
            }
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
