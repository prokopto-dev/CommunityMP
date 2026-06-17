#include "WorldstateProcessor.hpp"
#include "ServerNetworking.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void WorldstateProcessor::Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate)
{
    packet.Send(true);
}

bool WorldstateProcessor::Process(ReceivedPacket& packet, BaseWorldstate &worldstate) noexcept
{
    worldstate.guid = packet.guid();

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            Player *player = Players::getPlayer(packet.guid());
            if (player == nullptr)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Received %s from missing player session and ignored!",
                    processor.second->strPacketID.c_str());
                return true;
            }

            WorldstatePacket *myPacket = ServerNetworking::get().getWorldstatePacketController()->GetPacket(packet.id());

            myPacket->setWorldstate(&worldstate);
            worldstate.isValid = true;

            if (!processor.second->avoidReading)
                myPacket->Read();

            if (worldstate.isValid && myPacket->isPacketValid())
                processor.second->Do(*myPacket, *player, worldstate);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
            
            return true;
        }
    }
    return false;
}
