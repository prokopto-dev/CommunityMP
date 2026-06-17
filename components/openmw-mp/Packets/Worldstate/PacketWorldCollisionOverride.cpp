#include "PacketWorldCollisionOverride.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxEnforcedCollisionRefIds = 3000;
}

PacketWorldCollisionOverride::PacketWorldCollisionOverride() : WorldstatePacket()
{
    packetID = ID_WORLD_COLLISION_OVERRIDE;
    orderChannel = CHANNEL_WORLDSTATE;
}

void PacketWorldCollisionOverride::Packet(PacketStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    RW(worldstate->hasPlayerCollision, send);
    RW(worldstate->hasActorCollision, send);
    RW(worldstate->hasPlacedObjectCollision, send);
    RW(worldstate->useActorCollisionForPlacedObjects, send);

    uint32_t enforcedCollisionCount = 0;

    if (send)
        enforcedCollisionCount = static_cast<uint32_t>(worldstate->enforcedCollisionRefIds.size());

    if (!RW(enforcedCollisionCount, send))
        return;

    if (!send)
    {
        if (enforcedCollisionCount > maxEnforcedCollisionRefIds)
        {
            packetValid = false;
            worldstate->enforcedCollisionRefIds.clear();
            return;
        }

        worldstate->enforcedCollisionRefIds.clear();
        worldstate->enforcedCollisionRefIds.resize(enforcedCollisionCount);
    }

    for (auto &&enforcedCollisionRefId : worldstate->enforcedCollisionRefIds)
    {
        RW(enforcedCollisionRefId, send, true);
    }
}
