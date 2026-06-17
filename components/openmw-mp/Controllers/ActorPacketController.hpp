#ifndef OPENMW_ACTORPACKETCONTROLLER_HPP
#define OPENMW_ACTORPACKETCONTROLLER_HPP


#include "../Packets/Actor/ActorPacket.hpp"
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    class ActorPacketController
    {
    public:
        ActorPacketController();
        ActorPacket *GetPacket(PacketId id);
        void SetStream(PacketStream *inStream, PacketStream *outStream);

        bool ContainsPacket(PacketId id);

        typedef std::unordered_map<PacketId, std::unique_ptr<ActorPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_ACTORPACKETCONTROLLER_HPP
