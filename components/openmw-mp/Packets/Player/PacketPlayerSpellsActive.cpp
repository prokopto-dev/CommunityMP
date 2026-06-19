#include "PacketPlayerSpellsActive.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

#include <cmath>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxActiveSpells = 3000;

    bool hasFiniteActiveSpellValues(const ActiveSpell& activeSpell)
    {
        if (!std::isfinite(activeSpell.timestampHour))
            return false;

        for (const ESM::ActiveEffect& effect : activeSpell.params.mEffects)
        {
            if (!std::isfinite(effect.mMagnitude) || !std::isfinite(effect.mDuration)
                || !std::isfinite(effect.mTimeLeft))
                return false;
        }

        return true;
    }
}

PacketPlayerSpellsActive::PacketPlayerSpellsActive() : PlayerPacket()
{
    packetID = ID_PLAYER_SPELLS_ACTIVE;
}

void PacketPlayerSpellsActive::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->spellsActiveChanges.action, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->spellsActiveChanges.activeSpells.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxActiveSpells)
        {
            packetValid = false;
            player->spellsActiveChanges.activeSpells.clear();
            return;
        }

        player->spellsActiveChanges.activeSpells.clear();
        player->spellsActiveChanges.activeSpells.resize(count);
    }

    for (auto&& activeSpell : player->spellsActiveChanges.activeSpells)
    {
        RW(activeSpell.id, send, true);
        RW(activeSpell.params.mActiveSpellId, send, true);
        RW(activeSpell.params.mSourceSpellId, send, true);
        RW(activeSpell.isStackingSpell, send);
        RW(activeSpell.timestampDay, send);
        RW(activeSpell.timestampHour, send);
        RW(activeSpell.params.mDisplayName, send, true);

        RW(activeSpell.caster.isPlayer, send);

        if (activeSpell.caster.isPlayer)
        {
            RW(activeSpell.caster.guid, send);
        }
        else
        {
            RW(activeSpell.caster.refId, send, true);
            RW(activeSpell.caster.refNum, send);
            RW(activeSpell.caster.mpNum, send);
        }

        uint32_t effectCount = 0;

        if (send)
            effectCount = static_cast<uint32_t>(activeSpell.params.mEffects.size());

        if (!RW(effectCount, send))
            return;

        if (effectCount > maxEffects)
        {
            packetValid = false;
            player->spellsActiveChanges.activeSpells.clear();
            return;
        }

        if (!send)
        {
            activeSpell.params.mEffects.clear();
            activeSpell.params.mEffects.resize(effectCount);
        }

        for (auto&& effect : activeSpell.params.mEffects)
        {
            RW(effect.mEffectId, send);
            RW(effect.mArg, send);
            RW(effect.mMagnitude, send);
            RW(effect.mDuration, send);
            RW(effect.mTimeLeft, send);
        }

        if (!send && !hasFiniteActiveSpellValues(activeSpell))
        {
            packetValid = false;
            player->spellsActiveChanges.activeSpells.clear();
            return;
        }
    }
}
