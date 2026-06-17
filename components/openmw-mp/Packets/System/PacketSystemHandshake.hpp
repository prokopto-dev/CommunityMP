#ifndef OPENMW_PACKETSYSTEMHANDSHAKE_HPP
#define OPENMW_PACKETSYSTEMHANDSHAKE_HPP

#include <components/openmw-mp/Packets/System/SystemPacket.hpp>

namespace mwmp
{
    class PacketSystemHandshake : public SystemPacket
    {
    public:
        PacketSystemHandshake();

        virtual void Packet(PacketStream *newBitstream, bool send);

        const static uint32_t maxNameLength = 256;
        const static uint32_t maxPasswordLength = 256;
        const static uint32_t maxAccountPasswordHashLength = 256;
    };
}

#endif //OPENMW_PACKETSYSTEMHANDSHAKE_HPP
