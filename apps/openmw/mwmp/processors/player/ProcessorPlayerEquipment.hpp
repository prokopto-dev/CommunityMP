#ifndef OPENMW_PROCESSORPLAYEREQUIPMENT_HPP
#define OPENMW_PROCESSORPLAYEREQUIPMENT_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerEquipment final: public PlayerProcessor
    {
    public:
        ProcessorPlayerEquipment()
        {
            BPP_INIT(ID_PLAYER_EQUIPMENT)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (isLocal())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_EQUIPMENT about LocalPlayer from server");

                if (isRequest())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                        "Server requested a full local equipment snapshot");
                    static_cast<LocalPlayer*>(player)->updateEquipment(true);
                    return;
                }
                else if (player->acceptEquipmentPacket())
                {
                    static_cast<LocalPlayer*>(player)->setEquipment();
                    static_cast<LocalPlayer*>(player)->completeServerEquipmentReload();
                }
            }
            else if (player != 0 && player->acceptEquipmentPacket())
                static_cast<DedicatedPlayer*>(player)->setEquipment();
        }
    };
}

#endif //OPENMW_PROCESSORPLAYEREQUIPMENT_HPP

