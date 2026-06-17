#ifndef OPENMW_PACKETPLAYERREPUTATION_HPP
#define OPENMW_PACKETPLAYERREPUTATION_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerReputation : public PlayerPacket
    {
    public:
        PacketPlayerReputation();

        virtual void Packet(PacketStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERREPUTATION_HPP
