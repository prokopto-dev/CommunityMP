#ifndef OPENMW_WORLDSTATEPACKETCONTROLLER_HPP
#define OPENMW_WORLDSTATEPACKETCONTROLLER_HPP


#include "../Packets/Worldstate/WorldstatePacket.hpp"
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    class WorldstatePacketController
    {
    public:
        WorldstatePacketController();
        WorldstatePacket *GetPacket(PacketId id);
        void SetStream(PacketStream *inStream, PacketStream *outStream);

        bool ContainsPacket(PacketId id);

        typedef std::unordered_map<PacketId, std::unique_ptr<WorldstatePacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_WORLDSTATEPACKETCONTROLLER_HPP
