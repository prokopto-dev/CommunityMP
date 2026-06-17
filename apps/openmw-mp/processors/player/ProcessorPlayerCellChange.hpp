#ifndef OPENMW_PROCESSORPLAYERCELLCHANGE_HPP
#define OPENMW_PROCESSORPLAYERCELLCHANGE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerEventDispatcher.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>

namespace mwmp
{
    class ProcessorPlayerCellChange : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerCellChange()
        {
            BPP_INIT(ID_PLAYER_CELL_CHANGE)
            playerController = ServerNetworking::get().getPlayerPacketController();
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            LOG_APPEND(TimedLog::LOG_INFO, "- Moved to %s", player.cell.getDescription().c_str());

            if (!ServerNetworking::getPtr()->getServerSimulation().acceptPlayerCellChange(player, packet))
                return;

            ServerEvents::playerCellChange(player.getId());

            const bool previousPlayerExchangeFullInfo = player.exchangeFullInfo;
            player.exchangeFullInfo = true;

            player.forEachLoaded([this](Player *pl, Player *other) {

                LOG_APPEND(TimedLog::LOG_INFO, "- Started information exchange with %s", other->npc.mName.c_str());

                const bool previousOtherExchangeFullInfo = other->exchangeFullInfo;
                other->exchangeFullInfo = true;

                playerController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_POSITION)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_SKILL)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(other);
                playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(other);

                playerController->GetPacket(ID_PLAYER_BASEINFO)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(pl->guid);
                playerController->GetPacket(ID_PLAYER_SKILL)->Send(pl->guid);
                other->sendToGuidWithReliability(
                    playerController->GetPacket(ID_PLAYER_POSITION), pl->guid, PacketReliability::ReliableOrdered);
                other->sendToGuidWithReliability(
                    playerController->GetPacket(ID_PLAYER_ANIM_FLAGS), pl->guid, PacketReliability::ReliableOrdered);

                playerController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_POSITION)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_SKILL)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(pl);
                playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(pl);

                playerController->GetPacket(ID_PLAYER_BASEINFO)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(other->guid);
                playerController->GetPacket(ID_PLAYER_SKILL)->Send(other->guid);
                pl->sendToGuidWithReliability(
                    playerController->GetPacket(ID_PLAYER_POSITION), other->guid, PacketReliability::ReliableOrdered);
                pl->sendToGuidWithReliability(
                    playerController->GetPacket(ID_PLAYER_ANIM_FLAGS), other->guid, PacketReliability::ReliableOrdered);

                other->exchangeFullInfo = previousOtherExchangeFullInfo;

                LOG_APPEND(TimedLog::LOG_INFO, "- Finished information exchange with %s", other->npc.mName.c_str());
            });

            packet.setPlayer(&player);
            player.sendToLoadedAndRecentCellVisitorsWithReliability(&packet, PacketReliability::ReliableOrdered);

            LOG_APPEND(TimedLog::LOG_INFO, "- Finished processing ID_PLAYER_CELL_CHANGE", player.cell.getDescription().c_str());

            player.exchangeFullInfo = previousPlayerExchangeFullInfo;
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCELLCHANGE_HPP
