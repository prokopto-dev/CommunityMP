#include <components/openmw-mp/NetworkMessages.hpp>
#include "ObjectPacket.hpp"

using namespace mwmp;

ObjectPacket::ObjectPacket() : BasePacket()
{
    hasCellData = false;
    packetID = 0;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_OBJECT;
}

ObjectPacket::~ObjectPacket()
{

}

void ObjectPacket::setObjectList(BaseObjectList *newObjectList)
{
    objectList = newObjectList;
    guid = objectList->guid;
}

bool ObjectPacket::carriesCellData() const
{
    return hasCellData;
}

void ObjectPacket::Packet(PacketStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Object(baseObject, send);

        if (!packetValid)
        {
            objectList->isValid = false;
            return;
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}

bool ObjectPacket::PacketHeader(PacketStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    if (!RW(objectList->packetOrigin, send))
    {
        objectList->isValid = false;
        return false;
    }

    if (objectList->packetOrigin == mwmp::CLIENT_SCRIPT_LOCAL || objectList->packetOrigin == mwmp::CLIENT_SCRIPT_GLOBAL)
    {
        if (!RW(objectList->originClientScript, send, true))
        {
            objectList->isValid = false;
            return false;
        }
    }

    if (send)
        objectList->baseObjectCount = (unsigned int)(objectList->baseObjects.size());
    else
    {
        objectList->baseObjects.clear();
        objectList->baseObjectCount = 0;
    }

    if (!RW(objectList->baseObjectCount, send))
    {
        objectList->isValid = false;
        return false;
    }

    if (objectList->baseObjectCount > maxObjects)
    {
        objectList->isValid = false;
        return false;
    }

    if (hasCellData)
    {
        if (!RW(objectList->cell.mData, send, true) || !RW(objectList->cell.mName, send, true))
        {
            objectList->isValid = false;
            return false;
        }
    }

    return true;
}

void ObjectPacket::Object(BaseObject &baseObject, bool send)
{
    RW(baseObject.refId, send, true);
    RW(baseObject.refNum, send);
    RW(baseObject.mpNum, send);
}
