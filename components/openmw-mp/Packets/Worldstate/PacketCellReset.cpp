#include "PacketCellReset.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxCellsToReset = 3000;
}

PacketCellReset::PacketCellReset() : WorldstatePacket()
{
    packetID = ID_CELL_RESET;
    orderChannel = CHANNEL_SYSTEM;
}

void PacketCellReset::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t cellCount = 0;

    if (send)
        cellCount = static_cast<uint32_t>(worldstate->cellsToReset.size());

    if (!RW(cellCount, send))
        return;

    if (!send)
    {
        if (cellCount > maxCellsToReset)
        {
            packetValid = false;
            worldstate->cellsToReset.clear();
            return;
        }

        worldstate->cellsToReset.clear();
        worldstate->cellsToReset.resize(cellCount);
    }

    for (auto &&cellToReset : worldstate->cellsToReset)
    {
        RW(cellToReset.mData, send, true);
        RW(cellToReset.mName, send, true);
    }
}
