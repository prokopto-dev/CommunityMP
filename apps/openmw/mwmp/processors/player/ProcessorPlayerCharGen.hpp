#ifndef OPENMW_PROCESSORPLAYERCHARGEN_HPP
#define OPENMW_PROCESSORPLAYERCHARGEN_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerCharGen final: public PlayerProcessor
    {
    public:
        ProcessorPlayerCharGen()
        {
            BPP_INIT(ID_PLAYER_CHARGEN)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal() && player == nullptr && !isRequest())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Received ID_PLAYER_CHARGEN before the packet GUID mapped to a player; applying it to LocalPlayer");
                player = getLocalPlayer();
                packet.setPlayer(player);
                packet.Read();
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERCHARGEN_HPP

