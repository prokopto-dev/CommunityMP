#include "PlayerProcessor.hpp"
#include "Networking.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

bool PlayerProcessor::Process(ReceivedPacket& packet) noexcept
{
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

            PlayerPacket *myPacket = Networking::get().getPlayerPacketController()->GetPacket(packet.id());
            myPacket->setPlayer(player);

            if (!processor.second->avoidReading)
                myPacket->Read();

            if (!myPacket->isPacketValid())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!",
                    processor.second->strPacketID.c_str());
                return true;
            }

            processor.second->Do(*myPacket, *player);
            return true;
        }
    }
    return false;
}
