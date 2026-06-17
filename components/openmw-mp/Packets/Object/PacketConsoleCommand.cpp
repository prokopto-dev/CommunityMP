#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketConsoleCommand.hpp"

using namespace mwmp;

PacketConsoleCommand::PacketConsoleCommand() : ObjectPacket()
{
    packetID = ID_CONSOLE_COMMAND;
    hasCellData = true;
}

void PacketConsoleCommand::Packet(PacketStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    if (!RW(objectList->consoleCommand, send, true))
    {
        objectList->isValid = false;
        return;
    }

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

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
