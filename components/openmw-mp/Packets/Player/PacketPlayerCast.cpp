#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCast.hpp"

#include <components/openmw-mp/TimedLog.hpp>

#include <cmath>

using namespace mwmp;

namespace
{
    bool isValidCastType(char type)
    {
        return type == mwmp::Cast::REGULAR || type == mwmp::Cast::ITEM;
    }

    bool isFiniteProjectileOrigin(const mwmp::ProjectileOrigin& projectileOrigin)
    {
        return std::isfinite(projectileOrigin.origin[0]) && std::isfinite(projectileOrigin.origin[1])
            && std::isfinite(projectileOrigin.origin[2]) && std::isfinite(projectileOrigin.orientation[0])
            && std::isfinite(projectileOrigin.orientation[1]) && std::isfinite(projectileOrigin.orientation[2])
            && std::isfinite(projectileOrigin.orientation[3]);
    }
}

PacketPlayerCast::PacketPlayerCast() : PlayerPacket()
{
    packetID = ID_PLAYER_CAST;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerCast::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    if (!RW(player->combatSequence, send))
        return;

    if (!RW(player->positionSequence, send) || !RW(player->position, send, true)
        || !RW(player->direction, send, true))
        return;

    if (!RW(player->cast.target.isPlayer, send))
        return;

    if (player->cast.target.isPlayer)
    {
        if (!RW(player->cast.target.guid, send))
            return;
    }
    else
    {
        if (!RW(player->cast.target.refId, send, true) || !RW(player->cast.target.refNum, send)
            || !RW(player->cast.target.mpNum, send))
            return;
    }

    if (!RW(player->cast.type, send))
        return;

    if (!send && !isValidCastType(player->cast.type))
    {
        packetValid = false;
        return;
    }

    if (player->cast.type == mwmp::Cast::ITEM)
    {
        if (!RW(player->cast.itemId, send, true))
            return;

        if (!send && player->cast.itemId.empty())
        {
            packetValid = false;
            return;
        }
    }
    else
    {
        if (!RW(player->cast.pressed, send) || !RW(player->cast.success, send))
            return;

        if (!RW(player->cast.instant, send) || !RW(player->cast.spellId, send, true))
            return;

        if (!send && player->cast.spellId.empty())
        {
            packetValid = false;
            return;
        }
    }

    if (!RW(player->cast.hasProjectile, send))
        return;

    if (player->cast.hasProjectile)
    {
        if (!RW(player->cast.projectileOrigin.origin[0], send)
            || !RW(player->cast.projectileOrigin.origin[1], send)
            || !RW(player->cast.projectileOrigin.origin[2], send)
            || !RW(player->cast.projectileOrigin.orientation[0], send)
            || !RW(player->cast.projectileOrigin.orientation[1], send)
            || !RW(player->cast.projectileOrigin.orientation[2], send)
            || !RW(player->cast.projectileOrigin.orientation[3], send))
            return;

        if (!send && !isFiniteProjectileOrigin(player->cast.projectileOrigin))
        {
            packetValid = false;
            return;
        }
    }
}
