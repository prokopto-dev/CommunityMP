#include "ActorProcessor.hpp"
#include "../Networking.hpp"
#include "../Main.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ActorProcessor::~ActorProcessor()
{

}

bool ActorProcessor::Process(ReceivedPacket& packet, ActorList &actorList)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;
    actorList.guid = guid;

    ActorPacket *myPacket = Main::get().getNetworking()->getActorPacket(packet.id());

    myPacket->setActorList(&actorList);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length() == myPacket->headerSize();

            actorList.isValid = true;

            if (!request && !processor.second->avoidReading)
            {
                myPacket->Read();
            }

            if (actorList.isValid && myPacket->isPacketValid())
                processor.second->Do(*myPacket, actorList);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());

            return true;
        }
    }
    return false;
}

