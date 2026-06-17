#ifndef OPENMW_PLAYERPACKETCONTROLLER_HPP
#define OPENMW_PLAYERPACKETCONTROLLER_HPP


#include "../Packets/Player/PlayerPacket.hpp"
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    class PlayerPacketController
    {
    public:
        PlayerPacketController();
        PlayerPacket *GetPacket(PacketId id);
        void SetStream(PacketStream *inStream, PacketStream *outStream);

        bool ContainsPacket(PacketId id);

        typedef std::unordered_map<PacketId, std::unique_ptr<PlayerPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_PLAYERPACKETCONTROLLER_HPP
