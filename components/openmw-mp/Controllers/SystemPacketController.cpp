#include "../Packets/System/PacketSystemHandshake.hpp"

#include "SystemPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::SystemPacketController::packets_t *packets)
{
    T *packet = new T();
    typedef mwmp::SystemPacketController::packets_t::value_type value_t;
    packets->insert(value_t(packet->GetPacketID(), value_t::second_type(packet)));
}

mwmp::SystemPacketController::SystemPacketController()
{
    AddPacket<PacketSystemHandshake>(&packets);
}


mwmp::SystemPacket *mwmp::SystemPacketController::GetPacket(PacketId id)
{
    return packets[id].get();
}

void mwmp::SystemPacketController::SetStream(PacketStream *inStream, PacketStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

bool mwmp::SystemPacketController::ContainsPacket(PacketId id)
{
    for(const auto &packet : packets)
    {
        if (packet.first == id)
            return true;
    }
    return false;
}
