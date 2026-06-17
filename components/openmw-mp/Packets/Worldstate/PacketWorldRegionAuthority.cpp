#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldRegionAuthority.hpp"

mwmp::PacketWorldRegionAuthority::PacketWorldRegionAuthority() : WorldstatePacket()
{
    packetID = ID_WORLD_REGION_AUTHORITY;
    // Make sure the priority is lower than PlayerCellChange's, so it doesn't get sent before it
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
}

void mwmp::PacketWorldRegionAuthority::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    RW(worldstate->authorityRegion, send, true);
}
