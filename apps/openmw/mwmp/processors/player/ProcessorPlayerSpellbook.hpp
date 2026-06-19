#ifndef OPENMW_PROCESSORPLAYERSPELLBOOK_HPP
#define OPENMW_PROCESSORPLAYERSPELLBOOK_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerSpellbook final: public PlayerProcessor
    {
    public:
        ProcessorPlayerSpellbook()
        {
            BPP_INIT(ID_PLAYER_SPELLBOOK)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal()) return;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_SPELLBOOK about LocalPlayer from server");

            if (isRequest())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Ignoring server request for full local spellbook snapshot; server is authoritative");
                return;
            }
            else
            {
                LocalPlayer &localPlayer = static_cast<LocalPlayer&>(*player);
                
                int spellbookAction = localPlayer.spellbookChanges.action;
                localPlayer.acceptCurrentSpellbookPacket();

                if (spellbookAction == SpellbookChanges::ADD)
                    localPlayer.addSpells();
                else if (spellbookAction == SpellbookChanges::REMOVE)
                    localPlayer.removeSpells();
                else // SpellbookChanges::SET
                    localPlayer.setSpellbook();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSPELLBOOK_HPP

