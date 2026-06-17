#include "../Main.hpp"
#include "../Networking.hpp"

#include "ObjectProcessor.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ObjectProcessor::~ObjectProcessor()
{

}

bool ObjectProcessor::Process(ReceivedPacket& packet, ObjectList &objectList)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;
    objectList.guid = guid;

    ObjectPacket *myPacket = Main::get().getNetworking()->getObjectPacket(packet.id());

    myPacket->setObjectList(&objectList);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor: processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length() == myPacket->headerSize();

            objectList.isValid = true;

            if (!request && !processor.second->avoidReading)
                myPacket->Read();

            if (objectList.isValid && myPacket->isPacketValid())
                processor.second->Do(*myPacket, objectList);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());

            return true;
        }
    }
    return false;
}

