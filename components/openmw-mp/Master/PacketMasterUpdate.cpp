#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketMasterUpdate.hpp"
#include "ProxyMasterPacket.hpp"

using namespace mwmp;

PacketMasterUpdate::PacketMasterUpdate() : BasePacket()
{
    packetID = ID_MASTER_UPDATE;
    orderChannel = CHANNEL_MASTER;
    reliability = PacketReliability::ReliableOrderedWithAckReceipt;
}

void PacketMasterUpdate::Packet(PacketStream *newBitstream, bool send)
{
    bs = newBitstream;
    if (send)
        bs->Write(packetID);

    std::string addr = packetAddressToString(server->first, false);
    uint16_t port = packetAddressPort(server->first);

    RW(addr, send);
    RW(port, send);

    if (!send)
        server->first = makePacketAddress(addr.c_str(), port);

    ProxyMasterPacket::addServer(this, server->second, send);

}

void PacketMasterUpdate::SetServer(std::pair<PacketAddress, QueryData> *serverPair)
{
    server = serverPair;
}
