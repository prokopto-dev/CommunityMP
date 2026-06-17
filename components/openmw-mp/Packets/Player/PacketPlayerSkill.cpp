#include "PacketPlayerSkill.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/esm3/creaturestats.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxSkillIndexes = 27;
}

PacketPlayerSkill::PacketPlayerSkill() : PlayerPacket()
{
    packetID = ID_PLAYER_SKILL;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerSkill::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->exchangeFullInfo, send);

    if (player->exchangeFullInfo)
    {
        RW(player->npcStats.mSkills, send);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->skillIndexChanges.size());

        if (!RW(count, send))
            return;

        if (!send)
        {
            if (count > maxSkillIndexes)
            {
                packetValid = false;
                player->skillIndexChanges.clear();
                return;
            }

            player->skillIndexChanges.clear();
            player->skillIndexChanges.resize(count);
        }

        for (auto &&skillId : player->skillIndexChanges)
        {
            if (!RW(skillId, send))
            {
                if (!send)
                    player->skillIndexChanges.clear();
                return;
            }
            if (skillId >= maxSkillIndexes)
            {
                packetValid = false;
                return;
            }
            if (!RW(player->npcStats.mSkills[skillId], send))
                return;
        }
    }
}
