#ifndef OPENMW_PACKETPLAYERLUAEVENT_HPP
#define OPENMW_PACKETPLAYERLUAEVENT_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerLuaEvent final : public PlayerPacket
    {
    public:
        PacketPlayerLuaEvent();

        void Packet(PacketStream* newBitstream, bool send) override;

    private:
        bool ExchangeBoundedString(std::string& value, bool send, std::string::size_type maxSize);
    };
}

#endif // OPENMW_PACKETPLAYERLUAEVENT_HPP
