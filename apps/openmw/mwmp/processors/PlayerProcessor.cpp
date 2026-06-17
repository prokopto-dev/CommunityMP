#include "../Networking.hpp"
#include "PlayerProcessor.hpp"
#include "../Main.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

PlayerProcessor::~PlayerProcessor()
{

}

bool PlayerProcessor::Process(ReceivedPacket& packet)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;

    PlayerPacket *myPacket = Main::get().getNetworking()->getPlayerPacket(packet.id());
    myPacket->SetReadStream(&bsIn);

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length() == myPacket->headerSize();

            BasePlayer *player = 0;
            if (guid != myGuid)
                player = PlayerList::getPlayer(guid);
            else
                player = Main::get().getLocalPlayer();

            if (request && guid == myGuid)
            {
                LocalPlayer* localPlayer = Main::get().getLocalPlayer();
                if (localPlayer != nullptr && !localPlayer->isLoggedIn())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                        "Ignoring %s request for LocalPlayer until character login completes",
                        processor.second->strPacketID.c_str());
                    return true;
                }
            }

            if (!request && !processor.second->avoidReading && player != 0)
            {
                myPacket->setPlayer(player);
                myPacket->Read();

                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                        "Received %s that failed integrity check and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
            }

            processor.second->Do(*myPacket, player);
            return true;
        }
    }
    return false;
}

