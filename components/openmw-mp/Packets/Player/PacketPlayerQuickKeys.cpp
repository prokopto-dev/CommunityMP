#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerQuickKeys.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxQuickKeyChanges = 10;
}

PacketPlayerQuickKeys::PacketPlayerQuickKeys() : PlayerPacket()
{
    packetID = ID_PLAYER_QUICKKEYS;
}

void PacketPlayerQuickKeys::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->quickKeyChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxQuickKeyChanges)
        {
            packetValid = false;
            player->quickKeyChanges.clear();
            return;
        }

        player->quickKeyChanges.clear();
        player->quickKeyChanges.resize(count);
    }

    for (auto &&quickKey : player->quickKeyChanges)
    {
        RW(quickKey.type, send);
        RW(quickKey.slot, send);

        if (quickKey.type != QuickKey::UNASSIGNED)
            RW(quickKey.itemId, send);
    }
}
