#include "PacketPlayerEquipment.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxEquipmentIndexes = equipmentSlotCount;
}

PacketPlayerEquipment::PacketPlayerEquipment() : PlayerPacket()
{
    packetID = ID_PLAYER_EQUIPMENT;
    // Equipment is validated against server-authoritative inventory, so keep
    // it ordered with inventory/player state instead of movement snapshots.
    orderChannel = CHANNEL_PLAYER;
}

void PacketPlayerEquipment::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    std::uint32_t equipmentSequence = player->equipmentSequence;
    if (!RW(equipmentSequence, send))
        return;

    bool exchangeFullInfo = player->exchangeFullInfo;
    if (!RW(exchangeFullInfo, send))
        return;

    if (exchangeFullInfo)
    {
        if (send)
        {
            for (auto &&equipmentItem : player->equipmentItems)
            {
                if (!ExchangeItemInformation(equipmentItem, send))
                    return;
            }
        }
        else
        {
            std::array<Item, equipmentSlotCount> receivedEquipmentItems;
            for (Item& equipmentItem : receivedEquipmentItems)
            {
                if (!ExchangeItemInformation(equipmentItem, send))
                    return;
                if (!isValidEquipmentItem(equipmentItem))
                {
                    packetValid = false;
                    return;
                }
            }

            player->equipmentSequence = equipmentSequence;
            player->exchangeFullInfo = exchangeFullInfo;
            for (int slot = 0; slot < equipmentSlotCount; ++slot)
                player->equipmentItems[slot] = receivedEquipmentItems[slot];
        }
    }
    else
    {
        uint32_t count = 0;
        if (send)
            count = static_cast<uint32_t>(player->equipmentIndexChanges.size());

        if (!RW(count, send))
            return;

        if (!send)
        {
            if (count > maxEquipmentIndexes)
            {
                packetValid = false;
                player->equipmentIndexChanges.clear();
                return;
            }

            player->equipmentIndexChanges.clear();
        }

        std::vector<int> receivedEquipmentIndexChanges;
        std::vector<std::pair<int, Item>> receivedEquipmentChanges;
        if (!send)
        {
            receivedEquipmentIndexChanges.reserve(count);
            receivedEquipmentChanges.reserve(count);
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            int equipmentIndex = 0;
            if (send)
                equipmentIndex = player->equipmentIndexChanges.at(i);

            if (!RW(equipmentIndex, send))
            {
                if (!send)
                    player->equipmentIndexChanges.clear();
                return;
            }
            if (equipmentIndex < 0 || static_cast<uint32_t>(equipmentIndex) >= maxEquipmentIndexes)
            {
                packetValid = false;
                if (!send)
                    player->equipmentIndexChanges.clear();
                return;
            }

            Item equipmentItem;
            if (send)
                equipmentItem = player->equipmentItems[equipmentIndex];

            if (!ExchangeItemInformation(equipmentItem, send))
            {
                if (!send)
                    player->equipmentIndexChanges.clear();
                return;
            }

            if (!send)
            {
                if (!isValidEquipmentItem(equipmentItem))
                {
                    packetValid = false;
                    player->equipmentIndexChanges.clear();
                    return;
                }

                receivedEquipmentIndexChanges.push_back(equipmentIndex);
                receivedEquipmentChanges.emplace_back(equipmentIndex, equipmentItem);
            }
        }

        if (!send)
        {
            player->equipmentSequence = equipmentSequence;
            player->exchangeFullInfo = exchangeFullInfo;
            player->equipmentIndexChanges = std::move(receivedEquipmentIndexChanges);
            for (const auto& [equipmentIndex, equipmentItem] : receivedEquipmentChanges)
                player->equipmentItems[equipmentIndex] = equipmentItem;
        }
    }
}

bool PacketPlayerEquipment::ExchangeItemInformation(Item &item, bool send)
{
    return RW(item.refId, send, true)
        && RW(item.count, send)
        && RW(item.charge, send)
        && RW(item.enchantmentCharge, send);
}
