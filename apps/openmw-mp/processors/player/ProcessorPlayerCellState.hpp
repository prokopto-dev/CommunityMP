#ifndef OPENMW_PROCESSORPLAYERCELLSTATE_HPP
#define OPENMW_PROCESSORPLAYERCELLSTATE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/Script/Script.hpp"
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>

namespace mwmp
{
    class ProcessorPlayerCellState : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerCellState()
        {
            BPP_INIT(ID_PLAYER_CELL_STATE)
            playerController = ServerNetworking::get().getPlayerPacketController();
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            CellController::get()->update(&player);

            player.forEachLoaded([this](Player *pl, Player *other) {
                sendFullPlayerState(*other, pl->guid);
                sendFullPlayerState(*pl, other->guid);
            });
        }

    private:
        void sendFullPlayerState(Player& subject, mwmp::PacketGuid targetGuid)
        {
            if (targetGuid == mwmp::unassignedPacketGuid() || targetGuid == subject.guid)
                return;

            const bool previousExchangeFullInfo = subject.exchangeFullInfo;
            subject.exchangeFullInfo = true;

            playerController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_SKILL)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(&subject);
            playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(&subject);

            playerController->GetPacket(ID_PLAYER_BASEINFO)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_SHAPESHIFT)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(targetGuid);
            playerController->GetPacket(ID_PLAYER_SKILL)->Send(targetGuid);
            subject.sendToGuidWithReliability(
                playerController->GetPacket(ID_PLAYER_POSITION), targetGuid, PacketReliability::ReliableOrdered);
            subject.sendToGuidWithReliability(
                playerController->GetPacket(ID_PLAYER_ANIM_FLAGS), targetGuid, PacketReliability::ReliableOrdered);

            subject.exchangeFullInfo = previousExchangeFullInfo;
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCELLSTATE_HPP
