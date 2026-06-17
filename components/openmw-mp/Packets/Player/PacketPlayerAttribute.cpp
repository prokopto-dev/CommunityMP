#include "PacketPlayerAttribute.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxAttributeIndexes = 8;
}

PacketPlayerAttribute::PacketPlayerAttribute() : PlayerPacket()
{
    packetID = ID_PLAYER_ATTRIBUTE;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerAttribute::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->exchangeFullInfo, send);

    if (player->exchangeFullInfo)
    {
        RW(player->creatureStats.mAttributes, send);
        RW(player->npcStats.mSkillIncrease, send);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->attributeIndexChanges.size());

        if (!RW(count, send))
            return;

        if (!send)
        {
            if (count > maxAttributeIndexes)
            {
                packetValid = false;
                player->attributeIndexChanges.clear();
                return;
            }

            player->attributeIndexChanges.clear();
            player->attributeIndexChanges.resize(count);
        }

        for (auto &&attributeIndex : player->attributeIndexChanges)
        {
            if (!RW(attributeIndex, send))
            {
                if (!send)
                    player->attributeIndexChanges.clear();
                return;
            }

            if (attributeIndex >= maxAttributeIndexes)
            {
                packetValid = false;
                return;
            }

            if (!RW(player->creatureStats.mAttributes[attributeIndex], send)
                || !RW(player->npcStats.mSkillIncrease[attributeIndex], send))
                return;
        }
    }
}
