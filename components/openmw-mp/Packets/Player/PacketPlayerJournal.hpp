#ifndef OPENMW_PACKETPLAYERJOURNAL_HPP
#define OPENMW_PACKETPLAYERJOURNAL_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerJournal : public PlayerPacket
    {
    public:
        PacketPlayerJournal();

        virtual void Packet(PacketStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERJOURNAL_HPP
