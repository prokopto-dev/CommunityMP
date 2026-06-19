#ifndef OPENMW_PROCESSORPLAYERDEATH_HPP
#define OPENMW_PROCESSORPLAYERDEATH_HPP

#include "../PlayerProcessor.hpp"
#include "PlayerMovementSnapshot.hpp"
#include "apps/openmw-mp/ServerEventDispatcher.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include <components/openmw-mp/Utils.hpp>
#include <chrono>
#include <vector>

namespace mwmp
{
    class ProcessorPlayerDeath : public PlayerProcessor
    {
        static void sendAcceptedStatsDynamicCorrection(Player& player)
        {
            if (!player.hasAcceptedStatsDynamicPacket)
                return;

            const bool previousExchangeFullInfo = player.exchangeFullInfo;
            const std::vector<uint8_t> previousStatsDynamicIndexChanges = player.statsDynamicIndexChanges;

            player.restoreAcceptedStatsDynamicPacket();
            player.exchangeFullInfo = true;

            PlayerPacket* packet = ServerNetworking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_STATS_DYNAMIC);
            packet->setPlayer(&player);
            packet->SendWithReliability(player.guid, PacketReliability::ReliableOrdered);

            player.exchangeFullInfo = previousExchangeFullInfo;
            player.statsDynamicIndexChanges = previousStatsDynamicIndexChanges;
        }

    public:
        ProcessorPlayerDeath()
        {
            BPP_INIT(ID_PLAYER_DEATH)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            if (player.getLoadState() != Player::POSTLOADED)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Rejected %s from %s before the player finished loading",
                    strPacketID.c_str(), player.npc.mName.c_str());
                sendAcceptedStatsDynamicCorrection(player);
                return;
            }

            if (!player.isClientDeathPacketAllowed())
            {
                sendAcceptedStatsDynamicCorrection(player);
                return;
            }

            if (!acceptSequencedPlayerCombatEvent(player))
                return;

            player.creatureStats.mDead = true;
            player.creatureStats.mDynamic[0].mCurrent = 0;
            if (!Utils::vectorContains(player.statsDynamicIndexChanges, 0))
                player.statsDynamicIndexChanges.push_back(0);
            player.acceptCurrentStatsDynamicPacket();

            player.sendToLoaded(&packet);

            ServerEvents::playerDeath(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERDEATH_HPP
