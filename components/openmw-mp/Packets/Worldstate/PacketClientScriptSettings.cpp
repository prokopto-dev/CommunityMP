#include "PacketClientScriptSettings.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxSynchronizedClientScriptIds = 3000;
    constexpr uint32_t maxSynchronizedClientGlobalIds = 3000;
}

PacketClientScriptSettings::PacketClientScriptSettings() : WorldstatePacket()
{
    packetID = ID_CLIENT_SCRIPT_SETTINGS;
    orderChannel = CHANNEL_WORLDSTATE;
}

void PacketClientScriptSettings::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t clientScriptsCount = 0;

    if (send)
        clientScriptsCount = static_cast<uint32_t>(worldstate->synchronizedClientScriptIds.size());

    if (!RW(clientScriptsCount, send))
        return;

    if (!send)
    {
        if (clientScriptsCount > maxSynchronizedClientScriptIds)
        {
            packetValid = false;
            worldstate->synchronizedClientScriptIds.clear();
            return;
        }

        worldstate->synchronizedClientScriptIds.clear();
        worldstate->synchronizedClientScriptIds.resize(clientScriptsCount);
    }

    for (auto &&clientScriptId : worldstate->synchronizedClientScriptIds)
    {
        RW(clientScriptId, send, true);
    }

    uint32_t clientGlobalsCount = 0;

    if (send)
        clientGlobalsCount = static_cast<uint32_t>(worldstate->synchronizedClientGlobalIds.size());

    if (!RW(clientGlobalsCount, send))
        return;

    if (!send)
    {
        if (clientGlobalsCount > maxSynchronizedClientGlobalIds)
        {
            packetValid = false;
            worldstate->synchronizedClientGlobalIds.clear();
            return;
        }

        worldstate->synchronizedClientGlobalIds.clear();
        worldstate->synchronizedClientGlobalIds.resize(clientGlobalsCount);
    }

    for (auto &&clientGlobalId : worldstate->synchronizedClientGlobalIds)
    {
        RW(clientGlobalId, send, true);
    }
}
