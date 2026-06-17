#ifndef OPENMW_PACKETMASTERUPDATE_HPP
#define OPENMW_PACKETMASTERUPDATE_HPP

#include "../Packets/BasePacket.hpp"
#include "MasterData.hpp"

namespace mwmp
{
    class ProxyMasterPacket;
    class PacketMasterUpdate : public BasePacket
    {
        friend class ProxyMasterPacket;
    public:
        PacketMasterUpdate();

        void Packet(PacketStream *newBitstream, bool send) override;

        void SetServer(std::pair<PacketAddress, QueryData> *serverPair);
    private:
        std::pair<PacketAddress, QueryData> *server;
    };
}

#endif //OPENMW_PACKETMASTERUPDATE_HPP
