#ifndef OPENMW_PACKETPLAYERSPEECH_HPP
#define OPENMW_PACKETPLAYERSPEECH_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerSpeech : public PlayerPacket
    {
    public:
        PacketPlayerSpeech();

        virtual void Packet(PacketStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERSPEECH_HPP
