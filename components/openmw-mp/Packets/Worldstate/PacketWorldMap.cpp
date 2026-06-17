#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketWorldMap.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxMapTileChanges = 3000;
}

PacketWorldMap::PacketWorldMap() : WorldstatePacket()
{
    packetID = ID_WORLD_MAP;
}

void PacketWorldMap::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t changesCount = 0;

    if (send)
        changesCount = static_cast<uint32_t>(worldstate->mapTiles.size());

    if (!RW(changesCount, send))
        return;

    if (!send)
    {
        if (changesCount > maxMapTileChanges)
        {
            packetValid = false;
            worldstate->mapTiles.clear();
            return;
        }

        worldstate->mapTiles.clear();
        worldstate->mapTiles.resize(changesCount);
    }

    for (auto &&mapTile : worldstate->mapTiles)
    {
        RW(mapTile.x, send);
        RW(mapTile.y, send);

        uint32_t imageDataSize = 0;

        if (send)
            imageDataSize = static_cast<uint32_t>(mapTile.imageData.size());

        if (!RW(imageDataSize, send))
            return;

        if (imageDataSize > mwmp::maxImageDataSize)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Processed invalid ID_WORLD_MAP packet where tile %i, %i had an imageDataSize of %i",
                mapTile.x, mapTile.y, imageDataSize);
            LOG_APPEND(TimedLog::LOG_ERROR, "- The packet was ignored after that point");
            packetValid = false;
            return;
        }

        if (!send)
        {
            mapTile.imageData.clear();
            mapTile.imageData.resize(imageDataSize);
        }

        for (auto &&imageChar : mapTile.imageData)
        {
            RW(imageChar, send);
        }
    }
}
