#ifndef OPENMW_PROCESSORPLAYERCHARCLASS_HPP
#define OPENMW_PROCESSORPLAYERCHARCLASS_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Script/Script.hpp"

namespace mwmp
{
    class ProcessorPlayerCharClass : public PlayerProcessor
    {
    public:
        ProcessorPlayerCharClass()
        {
            BPP_INIT(ID_PLAYER_CHARCLASS)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            Script::Call<Script::CallbackIdentity("OnPlayerCharClass")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCHARCLASS_HPP
