#include "PacketWorldDestinationOverride.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxDestinationOverrides = 3000;
}

PacketWorldDestinationOverride::PacketWorldDestinationOverride() : WorldstatePacket()
{
    packetID = ID_WORLD_DESTINATION_OVERRIDE;
    orderChannel = CHANNEL_WORLDSTATE;
}

void PacketWorldDestinationOverride::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t destinationCount = 0;

    if (send)
        destinationCount = static_cast<uint32_t>(worldstate->destinationOverrides.size());

    if (!RW(destinationCount, send))
        return;

    if (!send)
    {
        if (destinationCount > maxDestinationOverrides)
        {
            packetValid = false;
            worldstate->destinationOverrides.clear();
            return;
        }

        worldstate->destinationOverrides.clear();
    }

    std::string mapIndex;
    std::string mapValue;

    if (send)
    {
        for (auto &&destinationOverride : worldstate->destinationOverrides)
        {
            mapIndex = destinationOverride.first;
            mapValue = destinationOverride.second;
            RW(mapIndex, send, false);
            RW(mapValue, send, false);
        }
    }
    else
    {
        for (unsigned int n = 0; n < destinationCount; n++)
        {
            mapIndex.clear();
            mapValue.clear();

            if (!RW(mapIndex, send, false) || !RW(mapValue, send, false))
                return;

            worldstate->destinationOverrides[mapIndex] = mapValue;
        }
    }
}
