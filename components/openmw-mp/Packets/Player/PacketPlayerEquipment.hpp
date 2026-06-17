#ifndef OPENMW_PACKETPLAYEREQUIPMENT_HPP
#define OPENMW_PACKETPLAYEREQUIPMENT_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerEquipment : public PlayerPacket
    {
    public:
        PacketPlayerEquipment();

        virtual void Packet(PacketStream *newBitstream, bool send);
        bool ExchangeItemInformation(Item &item, bool send);
    };
}

#endif //OPENMW_PACKETPLAYEREQUIPMENT_HPP
