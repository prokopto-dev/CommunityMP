#include "../Packets/Worldstate/PacketCellReset.hpp"
#include "../Packets/Worldstate/PacketClientScriptGlobal.hpp"
#include "../Packets/Worldstate/PacketClientScriptSettings.hpp"
#include "../Packets/Worldstate/PacketRecordDynamic.hpp"
#include "../Packets/Worldstate/PacketWorldCollisionOverride.hpp"
#include "../Packets/Worldstate/PacketWorldDestinationOverride.hpp"
#include "../Packets/Worldstate/PacketWorldKillCount.hpp"
#include "../Packets/Worldstate/PacketWorldMap.hpp"
#include "../Packets/Worldstate/PacketWorldRegionAuthority.hpp"
#include "../Packets/Worldstate/PacketWorldTime.hpp"
#include "../Packets/Worldstate/PacketWorldWeather.hpp"

#include "WorldstatePacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::WorldstatePacketController::packets_t *packets)
{
    T *packet = new T();
    typedef mwmp::WorldstatePacketController::packets_t::value_type value_t;
    packets->insert(value_t(packet->GetPacketID(), value_t::second_type(packet)));
}

mwmp::WorldstatePacketController::WorldstatePacketController()
{
    AddPacket<PacketCellReset>(&packets);
    AddPacket<PacketClientScriptGlobal>(&packets);
    AddPacket<PacketClientScriptSettings>(&packets);
    AddPacket<PacketRecordDynamic>(&packets);
    AddPacket<PacketWorldCollisionOverride>(&packets);
    AddPacket<PacketWorldDestinationOverride>(&packets);
    AddPacket<PacketWorldKillCount>(&packets);
    AddPacket<PacketWorldMap>(&packets);
    AddPacket<PacketWorldRegionAuthority>(&packets);
    AddPacket<PacketWorldTime>(&packets);
    AddPacket<PacketWorldWeather>(&packets);
}


mwmp::WorldstatePacket *mwmp::WorldstatePacketController::GetPacket(PacketId id)
{
    return packets[id].get();
}

void mwmp::WorldstatePacketController::SetStream(PacketStream *inStream, PacketStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

bool mwmp::WorldstatePacketController::ContainsPacket(PacketId id)
{
    for(const auto &packet : packets)
    {
        if (packet.first == id)
            return true;
    }
    return false;
}
