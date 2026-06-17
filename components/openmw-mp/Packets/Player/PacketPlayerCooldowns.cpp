#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCooldowns.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxCooldownChanges = 3000;
}

PacketPlayerCooldowns::PacketPlayerCooldowns() : PlayerPacket()
{
    packetID = ID_PLAYER_COOLDOWNS;
}

void PacketPlayerCooldowns::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cooldownChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxCooldownChanges)
        {
            packetValid = false;
            player->cooldownChanges.clear();
            return;
        }

        player->cooldownChanges.clear();
        player->cooldownChanges.resize(count);
    }

    for (auto &&spell : player->cooldownChanges)
    {
        RW(spell.id, send, true);
        RW(spell.startTimestampDay, send);
        RW(spell.startTimestampHour, send);
    }
}
