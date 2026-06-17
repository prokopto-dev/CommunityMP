#ifndef OPENMW_PACKETCONTAINER_HPP
#define OPENMW_PACKETCONTAINER_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    bool isContainerPacketAllowedFromClient(const BaseObjectList& objectList);

    class PacketContainer : public ObjectPacket
    {
    public:
        PacketContainer();

        virtual void Packet(PacketStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETCONTAINER_HPP
