#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectSound.hpp"

using namespace mwmp;

PacketObjectSound::PacketObjectSound() : ObjectPacket()
{
    packetID = ID_OBJECT_SOUND;
    hasCellData = true;
}

void PacketObjectSound::Packet(PacketStream *newBitstream, bool send)
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

        if (!RW(baseObject.soundId, send, true) || !RW(baseObject.volume, send) || !RW(baseObject.pitch, send))
        {
            objectList->isValid = false;
            return;
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
