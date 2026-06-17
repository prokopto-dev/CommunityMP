#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAttack.hpp"

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

PacketPlayerAttack::PacketPlayerAttack() : PlayerPacket()
{
    packetID = ID_PLAYER_ATTACK;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerAttack::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    if (!RW(player->combatSequence, send))
        return;

    if (!RW(player->positionSequence, send) || !RW(player->position, send, true)
        || !RW(player->direction, send, true))
        return;

    if (!RW(player->attack.target.isPlayer, send))
        return;

    if (player->attack.target.isPlayer)
    {
        if (!RW(player->attack.target.guid, send))
            return;
    }
    else
    {
        if (!RW(player->attack.target.refId, send, true) || !RW(player->attack.target.refNum, send)
            || !RW(player->attack.target.mpNum, send))
            return;
    }

    if (!RW(player->attack.type, send))
        return;

    if (!send && !isValidAttackType(player->attack.type))
    {
        packetValid = false;
        return;
    }

    if (!RW(player->attack.pressed, send) || !RW(player->attack.success, send))
        return;

    if (!RW(player->attack.isHit, send))
        return;

    if (player->attack.type == mwmp::Attack::MELEE)
    {
        if (!RW(player->attack.attackAnimation, send, true) || !RW(player->attack.attackStrength, send)
            || !RW(player->attack.rangedWeaponId, send, true))
            return;
    }
    else if (player->attack.type == mwmp::Attack::RANGED)
    {
        if (!RW(player->attack.attackStrength, send) || !RW(player->attack.rangedWeaponId, send, true)
            || !RW(player->attack.rangedAmmoId, send, true))
            return;

        if (!RW(player->attack.projectileOrigin.origin[0], send)
            || !RW(player->attack.projectileOrigin.origin[1], send)
            || !RW(player->attack.projectileOrigin.origin[2], send)
            || !RW(player->attack.projectileOrigin.orientation[0], send)
            || !RW(player->attack.projectileOrigin.orientation[1], send)
            || !RW(player->attack.projectileOrigin.orientation[2], send)
            || !RW(player->attack.projectileOrigin.orientation[3], send))
            return;

        if (!send && !isFiniteProjectileOrigin(player->attack.projectileOrigin))
        {
            packetValid = false;
            return;
        }
    }

    if (player->attack.isHit)
    {
        if (!RW(player->attack.damage, send) || !RW(player->attack.block, send)
            || !RW(player->attack.knockdown, send) || !RW(player->attack.applyWeaponEnchantment, send))
            return;

        if (player->attack.type == mwmp::Attack::RANGED)
        {
            if (!RW(player->attack.applyAmmoEnchantment, send))
                return;
        }

        if (!RW(player->attack.hitPosition.pos[0], send) || !RW(player->attack.hitPosition.pos[1], send)
            || !RW(player->attack.hitPosition.pos[2], send))
            return;

        if (!send && (!std::isfinite(player->attack.damage) || player->attack.damage < 0.f
            || !isFinitePosition(player->attack.hitPosition)))
        {
            packetValid = false;
            return;
        }
    }
}
