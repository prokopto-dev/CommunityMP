#include "../Main.hpp"
#include "../Networking.hpp"

#include "WorldstateProcessor.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

WorldstateProcessor::~WorldstateProcessor()
{

}

bool WorldstateProcessor::Process(ReceivedPacket& packet, Worldstate &worldstate)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;
    worldstate.guid = guid;

    WorldstatePacket *myPacket = Main::get().getNetworking()->getWorldstatePacket(packet.id());

    myPacket->setWorldstate(&worldstate);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length() == myPacket->headerSize();

            worldstate.isValid = true;

            if (!request && !processor.second->avoidReading)
                myPacket->Read();

            if (worldstate.isValid && myPacket->isPacketValid())
                processor.second->Do(*myPacket, worldstate);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());

            return true;
        }
    }
    return false;
}

