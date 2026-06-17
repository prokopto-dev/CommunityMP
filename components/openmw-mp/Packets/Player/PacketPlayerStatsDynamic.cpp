#include "PacketPlayerStatsDynamic.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxStatsDynamicIndexes = 3;
}

PacketPlayerStatsDynamic::PacketPlayerStatsDynamic() : PlayerPacket()
{
    packetID = ID_PLAYER_STATS_DYNAMIC;
    orderChannel = CHANNEL_MOVEMENT;
}

void PacketPlayerStatsDynamic::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    bool readOk = RW(player->statsDynamicSequence, send);
    if (!readOk)
        return;

    readOk = RW(player->creatureStats.mDead, send);
    if (!readOk)
        return;

    readOk = RW(player->exchangeFullInfo, send);
    if (!readOk)
        return;

    if (player->exchangeFullInfo)
    {
        readOk = RW(player->creatureStats.mDynamic, send);
        if (!readOk)
            return;
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->statsDynamicIndexChanges.size());

        if (!RW(count, send))
            return;

        if (!send)
        {
            if (count > maxStatsDynamicIndexes)
            {
                packetValid = false;
                player->statsDynamicIndexChanges.clear();
                return;
            }

            player->statsDynamicIndexChanges.clear();
            player->statsDynamicIndexChanges.resize(count);
        }

        for (auto &&statsDynamicIndex : player->statsDynamicIndexChanges)
        {
            if (!RW(statsDynamicIndex, send))
            {
                if (!send)
                    player->statsDynamicIndexChanges.clear();
                return;
            }
            if (statsDynamicIndex >= 3)
            {
                packetValid = false;
                return;
            }
            if (!RW(player->creatureStats.mDynamic[statsDynamicIndex], send))
                return;
        }
    }

    if (!send && !player->hasFiniteDynamicStats())
    {
        player->restoreAcceptedStatsDynamicPacket();
        packetValid = false;
        return;
    }
}
