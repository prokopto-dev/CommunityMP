#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketMasterAnnounce.hpp"
#include "ProxyMasterPacket.hpp"

using namespace mwmp;

PacketMasterAnnounce::PacketMasterAnnounce() : BasePacket()
{
    packetID = ID_MASTER_ANNOUNCE;
    orderChannel = CHANNEL_MASTER;
    reliability = PacketReliability::ReliableOrderedWithAckReceipt;
    advertisedPort = 0;
}

void PacketMasterAnnounce::Packet(PacketStream *newBitstream, bool send)
{
    bs = newBitstream;
    if (send)
        bs->Write(packetID);

    RW(func, send);

    RW(advertisedPort, send);

    if (func == FUNCTION_ANNOUNCE)
        ProxyMasterPacket::addServer(this, *server, send);
}

void PacketMasterAnnounce::SetServer(QueryData *_server)
{
    server = _server;
}

void PacketMasterAnnounce::SetFunc(uint32_t _func)
{
    func = _func;
}

int PacketMasterAnnounce::GetFunc()
{
    return func;
}

void PacketMasterAnnounce::SetAdvertisedPort(uint16_t port)
{
    advertisedPort = port;
}

uint16_t PacketMasterAnnounce::GetAdvertisedPort() const
{
    return advertisedPort;
}
