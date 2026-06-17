#include <components/openmw-mp/NetworkMessages.hpp>
#include "SystemPacket.hpp"

using namespace mwmp;

SystemPacket::SystemPacket() : BasePacket()
{
    packetID = 0;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_SYSTEM;
}

SystemPacket::~SystemPacket()
{

}

void SystemPacket::setSystem(BaseSystem *newSystem)
{
    system = newSystem;
    guid = system->guid;
}

BaseSystem *SystemPacket::getSystem()
{
    return system;
}
