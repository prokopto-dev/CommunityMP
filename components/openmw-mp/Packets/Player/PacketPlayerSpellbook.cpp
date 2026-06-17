#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerSpellbook.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxSpellbookChanges = 3000;
}

PacketPlayerSpellbook::PacketPlayerSpellbook() : PlayerPacket()
{
    packetID = ID_PLAYER_SPELLBOOK;
}

void PacketPlayerSpellbook::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->spellbookChanges.action, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->spellbookChanges.spells.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxSpellbookChanges)
        {
            packetValid = false;
            player->spellbookChanges.spells.clear();
            return;
        }

        player->spellbookChanges.spells.clear();
        player->spellbookChanges.spells.resize(count);
    }

    for (auto &&spell : player->spellbookChanges.spells)
    {
        RW(spell.mId, send, true);
    }
}
