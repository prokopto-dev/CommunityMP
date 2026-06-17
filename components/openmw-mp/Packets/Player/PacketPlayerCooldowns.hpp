#ifndef OPENMW_PACKETPLAYERCOOLDOWNS_HPP
#define OPENMW_PACKETPLAYERCOOLDOWNS_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerCooldowns : public PlayerPacket
    {
    public:
        PacketPlayerCooldowns();

        virtual void Packet(PacketStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERCOOLDOWNS_HPP
