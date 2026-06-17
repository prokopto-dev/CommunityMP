#include <components/openmw-mp/NetworkMessages.hpp>
#include <cstddef>
#include <iostream>
#include <limits>
#include "MasterData.hpp"
#include "PacketMasterQuery.hpp"
#include "ProxyMasterPacket.hpp"


using namespace mwmp;

namespace
{
    int32_t packetCount(std::size_t value)
    {
        if (value > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();

        return static_cast<int32_t>(value);
    }
}

PacketMasterQuery::PacketMasterQuery() : BasePacket()
{
    packetID = ID_MASTER_QUERY;
    orderChannel = CHANNEL_MASTER;
    reliability = PacketReliability::ReliableOrderedWithAckReceipt;
}

void PacketMasterQuery::Packet(PacketStream *newBitstream, bool send)
{
    bs = newBitstream;
    if (send)
        bs->Write(packetID);

    int32_t serversCount = packetCount(servers->size());

    RW(serversCount, send);

    std::map<PacketAddress, QueryData>::iterator serverIt;
    if (send)
        serverIt = servers->begin();

    QueryData server;
    std::string addr;
    uint16_t port;
    while (serversCount--)
    {
        if (send)
        {
            addr = packetAddressToString(serverIt->first, false);
            port = packetAddressPort(serverIt->first);
            server = serverIt->second;
        }
        RW(addr, send);
        RW(port, send);

        ProxyMasterPacket::addServer(this, server, send);

        if(addr.empty())
        {
            std::cerr << "Address empty. Aborting PacketMasterQuery::Packet" << std::endl;
            return;
        }

        if (send)
            serverIt++;
        else
            servers->insert(std::pair<PacketAddress, QueryData>(makePacketAddress(addr.c_str(), port), server));
    }

}

void PacketMasterQuery::SetServers(std::map<PacketAddress, QueryData> *serverMap)
{
    servers = serverMap;
}
