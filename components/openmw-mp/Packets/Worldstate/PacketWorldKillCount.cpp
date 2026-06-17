#include "PacketWorldKillCount.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxKillChanges = 3000;
}

PacketWorldKillCount::PacketWorldKillCount() : WorldstatePacket()
{
    packetID = ID_WORLD_KILL_COUNT;
    orderChannel = CHANNEL_SYSTEM;
}

void PacketWorldKillCount::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t killChangesCount = 0;

    if (send)
        killChangesCount = static_cast<uint32_t>(worldstate->killChanges.size());

    if (!RW(killChangesCount, send))
        return;

    if (!send)
    {
        if (killChangesCount > maxKillChanges)
        {
            packetValid = false;
            worldstate->killChanges.clear();
            return;
        }

        worldstate->killChanges.clear();
        worldstate->killChanges.resize(killChangesCount);
    }

    for (auto &&killChange : worldstate->killChanges)
    {
        RW(killChange.refId, send, true);
        RW(killChange.number, send);
    }
}
