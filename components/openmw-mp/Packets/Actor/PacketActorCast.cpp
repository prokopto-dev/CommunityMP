#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorCast.hpp"

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

PacketActorCast::PacketActorCast() : ActorPacket()
{
    packetID = ID_ACTOR_CAST;
}

void PacketActorCast::Actor(BaseActor &actor, bool send)
{
    if (!RW(actor.combatSequence, send))
        return;

    if (!send)
        actor.hasCombatData = true;

    if (!RW(actor.hasPositionData, send))
        return;

    if (actor.hasPositionData)
    {
        if (!RW(actor.positionSequence, send) || !RW(actor.position, send, true)
            || !RW(actor.direction, send, true))
            return;
    }

    if (!RW(actor.cast.target.isPlayer, send))
        return;

    if (actor.cast.target.isPlayer)
    {
        if (!RW(actor.cast.target.guid, send))
            return;
    }
    else
    {
        if (!RW(actor.cast.target.refId, send, true) || !RW(actor.cast.target.refNum, send)
            || !RW(actor.cast.target.mpNum, send))
            return;
    }

    if (!RW(actor.cast.type, send))
        return;

    if (!send && !isValidCastType(actor.cast.type))
    {
        packetValid = false;
        return;
    }

    if (actor.cast.type == mwmp::Cast::ITEM)
    {
        if (!RW(actor.cast.itemId, send, true))
            return;

        if (!send && actor.cast.itemId.empty())
        {
            packetValid = false;
            return;
        }
    }
    else
    {
        if (!RW(actor.cast.pressed, send) || !RW(actor.cast.success, send))
            return;

        if (!RW(actor.cast.instant, send) || !RW(actor.cast.spellId, send, true))
            return;

        if (!send && actor.cast.spellId.empty())
        {
            packetValid = false;
            return;
        }
    }

    if (!RW(actor.cast.hasProjectile, send))
        return;

    if (actor.cast.hasProjectile)
    {
        if (!RW(actor.cast.projectileOrigin.origin[0], send) || !RW(actor.cast.projectileOrigin.origin[1], send)
            || !RW(actor.cast.projectileOrigin.origin[2], send)
            || !RW(actor.cast.projectileOrigin.orientation[0], send)
            || !RW(actor.cast.projectileOrigin.orientation[1], send)
            || !RW(actor.cast.projectileOrigin.orientation[2], send)
            || !RW(actor.cast.projectileOrigin.orientation[3], send))
            return;

        if (!send && !isFiniteProjectileOrigin(actor.cast.projectileOrigin))
        {
            packetValid = false;
            return;
        }
    }
}
