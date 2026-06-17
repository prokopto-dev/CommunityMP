#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectActivate.hpp"

using namespace mwmp;

PacketObjectActivate::PacketObjectActivate() : ObjectPacket()
{
    packetID = ID_OBJECT_ACTIVATE;
    hasCellData = true;
}

void PacketObjectActivate::Packet(PacketStream *newBitstream, bool send)
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

        if (!RW(baseObject.activatingActor.isPlayer, send))
        {
            objectList->isValid = false;
            return;
        }

        if (baseObject.activatingActor.isPlayer)
        {
            if (!RW(baseObject.activatingActor.guid, send))
            {
                objectList->isValid = false;
                return;
            }
        }
        else
        {
            if (!RW(baseObject.activatingActor.refId, send, true) || !RW(baseObject.activatingActor.refNum, send)
                || !RW(baseObject.activatingActor.mpNum, send) || !RW(baseObject.activatingActor.name, send))
            {
                objectList->isValid = false;
                return;
            }
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
