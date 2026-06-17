#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Transport/PacketTransport.hpp>
#include "BasePacket.hpp"

using namespace mwmp;

PacketTransport *BasePacket::sTransport = nullptr;

BasePacket::BasePacket()
{
    packetID = 0;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_SYSTEM;
}

void BasePacket::Packet(PacketStream *newBitstream, bool send)
{
    bs = newBitstream;
    packetValid = true;

    if (send)
    {
        bs->Write(packetID);
        writePacketGuid(*bs, guid);
    }
}

void BasePacket::SetReadStream(PacketStream *bitStream)
{
    bsRead = bitStream;
}

void BasePacket::SetSendStream(PacketStream *bitStream)
{
    bsSend = bitStream;
}

void BasePacket::SetStreams(PacketStream *inStream, PacketStream *outStream)
{
    if (inStream != nullptr)
        bsRead = inStream;
    if (outStream != nullptr)
        bsSend = outStream;
}

uint32_t BasePacket::RequestData(PacketGuid targetGuid)
{
    bsSend->ResetWritePointer();
    bsSend->Write(packetID);
    writePacketGuid(*bsSend, targetGuid);
    if (!sTransport)
        return 0;

    return sTransport->send(bsSend->data(), bsSend->size(), PacketPriority::High, PacketReliability::ReliableOrdered,
        orderChannel, PacketDestination(targetGuid), false);
}

uint32_t BasePacket::Send(const PacketDestination& destination)
{
    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!sTransport)
        return 0;

    return sTransport->send(bsSend->data(), bsSend->size(), priority, reliability, orderChannel,
        destination, false);
}

uint32_t BasePacket::Send(bool toOther)
{
    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!sTransport)
        return 0;

    return sTransport->send(bsSend->data(), bsSend->size(), priority, reliability, orderChannel,
        PacketDestination(guid), toOther);
}

uint32_t BasePacket::SendWithReliability(bool toOther, PacketReliability forcedReliability)
{
    const PacketReliability previousReliability = reliability;
    reliability = forcedReliability;
    const uint32_t sent = Send(toOther);
    reliability = previousReliability;
    return sent;
}

uint32_t BasePacket::SendWithReliability(const PacketDestination& destination, PacketReliability forcedReliability)
{
    const PacketReliability previousReliability = reliability;
    reliability = forcedReliability;
    const uint32_t sent = Send(destination);
    reliability = previousReliability;
    return sent;
}

void BasePacket::Read()
{
    Packet(bsRead, false);
}

void BasePacket::setGUID(PacketGuid newGuid)
{
    guid = newGuid;
}

PacketGuid BasePacket::getGUID()
{
    return guid;
}

void BasePacket::SetPacketTransport(PacketTransport *transport)
{
    sTransport = transport;
}

PacketTransport *BasePacket::GetPacketTransport()
{
    return sTransport;
}
