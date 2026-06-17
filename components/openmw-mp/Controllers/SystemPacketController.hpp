#ifndef OPENMW_SYSTEMPACKETCONTROLLER_HPP
#define OPENMW_SYSTEMPACKETCONTROLLER_HPP


#include "../Packets/System/SystemPacket.hpp"
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    class SystemPacketController
    {
    public:
        SystemPacketController();
        SystemPacket *GetPacket(PacketId id);
        void SetStream(PacketStream *inStream, PacketStream *outStream);

        bool ContainsPacket(PacketId id);

        typedef std::unordered_map<PacketId, std::unique_ptr<SystemPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_SYSTEMPACKETCONTROLLER_HPP
