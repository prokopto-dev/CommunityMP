#ifndef OPENMW_PACKETMASTERQUERY_HPP
#define OPENMW_PACKETMASTERQUERY_HPP

#include "../Packets/BasePacket.hpp"
#include "MasterData.hpp"

namespace mwmp
{
    class ProxyMasterPacket;
    class PacketMasterQuery : public BasePacket
    {
        friend class ProxyMasterPacket;
    public:
        PacketMasterQuery();

        void Packet(PacketStream *newBitstream, bool send) override;

        void SetServers(std::map<PacketAddress, QueryData> *serverMap);
    private:
        std::map<PacketAddress, QueryData> *servers;
    };
}

#endif //OPENMW_PACKETMASTERQUERY_HPP
