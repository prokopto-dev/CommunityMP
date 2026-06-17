#ifndef OPENMW_OBJECTPACKETCONTROLLER_HPP
#define OPENMW_OBJECTPACKETCONTROLLER_HPP


#include "../Packets/Object/ObjectPacket.hpp"
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    class ObjectPacketController
    {
    public:
        ObjectPacketController();
        ObjectPacket *GetPacket(PacketId id);
        void SetStream(PacketStream *inStream, PacketStream *outStream);

        bool ContainsPacket(PacketId id);

        typedef std::unordered_map<PacketId, std::unique_ptr<ObjectPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_OBJECTPACKETCONTROLLER_HPP
