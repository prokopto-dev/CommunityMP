#ifndef OPENMW_PROCESSOR_PLAYER_MOVEMENT_SNAPSHOT_HPP
#define OPENMW_PROCESSOR_PLAYER_MOVEMENT_SNAPSHOT_HPP

#include <components/openmw-mp/NetworkMessages.hpp>

#include "apps/openmw-mp/Networking.hpp"
#include "apps/openmw-mp/Player.hpp"

namespace mwmp
{
    inline void sendAcceptedPlayerPositionCorrection(Player& player)
    {
        if (!player.hasAcceptedPositionPacket)
            return;

        player.restoreAcceptedPositionPacket();

        PlayerPacket* packet = Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_POSITION);
        packet->setPlayer(&player);
        packet->SendWithReliability(player.guid, PacketReliability::ReliableOrdered);
    }

    inline bool normalizePlayerMovementSnapshot(Player& player)
    {
        if (!player.hasFinitePositionPacket())
        {
            if (!player.hasAcceptedPositionPacket)
                return false;

            sendAcceptedPlayerPositionCorrection(player);
            return true;
        }

        if (player.hasStalePositionPacket())
        {
            player.restoreAcceptedPositionPacket();
            return true;
        }

        if (player.acceptPositionPacket())
            return true;

        if (player.hasAcceptedPositionPacket)
            player.restoreAcceptedPositionPacket();

        if (!player.hasAcceptedPositionPacket)
            return false;

        return true;
    }

    inline bool acceptSequencedPlayerCombatEvent(Player& player)
    {
        if (!player.isCombatPacketSequenceAllowed())
        {
            player.acceptCombatPacket();
            return false;
        }

        if (!normalizePlayerMovementSnapshot(player))
            return false;

        player.acceptCurrentCombatPacket();
        return true;
    }
}

#endif // OPENMW_PROCESSOR_PLAYER_MOVEMENT_SNAPSHOT_HPP
