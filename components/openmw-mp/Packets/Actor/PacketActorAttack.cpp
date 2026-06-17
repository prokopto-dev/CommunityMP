#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAttack.hpp"

#include <cmath>

using namespace mwmp;

namespace
{
    bool isValidAttackType(char type)
    {
        return type == mwmp::Attack::MELEE || type == mwmp::Attack::RANGED;
    }

    bool isFinitePosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2]);
    }

    bool isFiniteProjectileOrigin(const mwmp::ProjectileOrigin& projectileOrigin)
    {
        return std::isfinite(projectileOrigin.origin[0]) && std::isfinite(projectileOrigin.origin[1])
            && std::isfinite(projectileOrigin.origin[2]) && std::isfinite(projectileOrigin.orientation[0])
            && std::isfinite(projectileOrigin.orientation[1]) && std::isfinite(projectileOrigin.orientation[2])
            && std::isfinite(projectileOrigin.orientation[3]);
    }
}

PacketActorAttack::PacketActorAttack() : ActorPacket()
{
    packetID = ID_ACTOR_ATTACK;
    orderChannel = CHANNEL_COMBAT;
}

void PacketActorAttack::Actor(BaseActor &actor, bool send)
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

        float sampleInterval = sanitizeMovementSampleIntervalSeconds(actor.movementSampleIntervalSeconds);
        if (!RW(sampleInterval, send))
            return;
        actor.movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(sampleInterval);

        float latencySeconds = sanitizeMovementLatencySeconds(actor.movementLatencySeconds);
        if (!RW(latencySeconds, send))
            return;
        actor.movementLatencySeconds = sanitizeMovementLatencySeconds(latencySeconds);
    }

    if (!RW(actor.attack.target.isPlayer, send))
        return;

    if (actor.attack.target.isPlayer)
    {
        if (!RW(actor.attack.target.guid, send))
            return;
    }
    else
    {
        if (!RW(actor.attack.target.refId, send, true) || !RW(actor.attack.target.refNum, send)
            || !RW(actor.attack.target.mpNum, send))
            return;
    }

    if (!RW(actor.attack.type, send))
        return;

    if (!send && !isValidAttackType(actor.attack.type))
    {
        packetValid = false;
        return;
    }

    if (!RW(actor.attack.pressed, send) || !RW(actor.attack.success, send))
        return;

    if (!RW(actor.attack.isHit, send))
        return;

    if (actor.attack.type == mwmp::Attack::MELEE)
    {
        if (!RW(actor.attack.attackAnimation, send, true) || !RW(actor.attack.attackStrength, send)
            || !RW(actor.attack.rangedWeaponId, send, true))
            return;
    }
    else if (actor.attack.type == mwmp::Attack::RANGED)
    {
        if (!RW(actor.attack.attackStrength, send) || !RW(actor.attack.rangedWeaponId, send, true)
            || !RW(actor.attack.rangedAmmoId, send, true))
            return;

        if (!RW(actor.attack.projectileOrigin.origin[0], send) || !RW(actor.attack.projectileOrigin.origin[1], send)
            || !RW(actor.attack.projectileOrigin.origin[2], send)
            || !RW(actor.attack.projectileOrigin.orientation[0], send)
            || !RW(actor.attack.projectileOrigin.orientation[1], send)
            || !RW(actor.attack.projectileOrigin.orientation[2], send)
            || !RW(actor.attack.projectileOrigin.orientation[3], send))
            return;

        if (!send && !isFiniteProjectileOrigin(actor.attack.projectileOrigin))
        {
            packetValid = false;
            return;
        }
    }

    if (actor.attack.isHit)
    {
        if (!RW(actor.attack.damage, send) || !RW(actor.attack.block, send)
            || !RW(actor.attack.knockdown, send) || !RW(actor.attack.applyWeaponEnchantment, send))
            return;

        if (actor.attack.type == mwmp::Attack::RANGED)
        {
            if (!RW(actor.attack.applyAmmoEnchantment, send))
                return;
        }

        if (!RW(actor.attack.hitPosition.pos[0], send) || !RW(actor.attack.hitPosition.pos[1], send)
            || !RW(actor.attack.hitPosition.pos[2], send))
            return;

        if (!send && (!std::isfinite(actor.attack.damage) || actor.attack.damage < 0.f
            || !isFinitePosition(actor.attack.hitPosition)))
        {
            packetValid = false;
            return;
        }
    }
}
