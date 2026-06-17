#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellState.hpp"


namespace
{
    constexpr uint32_t maxCellStateChanges = 3000;
}

mwmp::PacketPlayerCellState::PacketPlayerCellState() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_STATE;
    priority = PacketPriority::Immediate;
    reliability = PacketReliability::ReliableOrdered;
}

void mwmp::PacketPlayerCellState::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cellStateChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxCellStateChanges)
        {
            packetValid = false;
            player->cellStateChanges.clear();
            return;
        }

        player->cellStateChanges.clear();
        player->cellStateChanges.resize(count);
    }

    for (auto &&cellState : player->cellStateChanges)
    {
        RW(cellState.type, send);
        RW(cellState.cell.mData, send, true);
        RW(cellState.cell.mName, send, true);
    }
}
