#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerInventory.hpp"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxInventoryChanges = 3000;
    constexpr int maxInventoryItemStackCount = 1000000;

    bool isValidInventoryAction(int action)
    {
        return action == InventoryChanges::SET || action == InventoryChanges::ADD || action == InventoryChanges::REMOVE;
    }

    bool isValidInventoryItem(const Item& item)
    {
        if (!std::isfinite(item.enchantmentCharge))
            return false;

        if (item.refId.empty() || item.refId.find("$dynamic") != std::string::npos)
            return false;

        return item.count > 0 && item.count <= maxInventoryItemStackCount;
    }
}

PacketPlayerInventory::PacketPlayerInventory() : PlayerPacket()
{
    packetID = ID_PLAYER_INVENTORY;
}

void PacketPlayerInventory::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    std::uint32_t inventorySequence = player->inventorySequence;
    if (!RW(inventorySequence, send))
        return;

    InventoryChanges inventoryChanges;
    if (send)
        inventoryChanges = player->inventoryChanges;

    if (!RW(inventoryChanges.action, send))
        return;

    if (!send && !isValidInventoryAction(inventoryChanges.action))
    {
        packetValid = false;
        return;
    }

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(inventoryChanges.items.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxInventoryChanges)
        {
            packetValid = false;
            return;
        }

        inventoryChanges.items.clear();
        inventoryChanges.items.reserve(count);
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        Item item;

        if (send)
            item = inventoryChanges.items.at(i);

        if (!RW(item.refId, send, true)
            || !RW(item.count, send)
            || !RW(item.charge, send)
            || !RW(item.enchantmentCharge, send)
            || !RW(item.soul, send, true))
        {
            return;
        }

        if (!send && isValidInventoryItem(item))
            inventoryChanges.items.push_back(item);
    }

    if (!send)
    {
        player->inventorySequence = inventorySequence;
        player->inventoryChanges = std::move(inventoryChanges);
    }
}
