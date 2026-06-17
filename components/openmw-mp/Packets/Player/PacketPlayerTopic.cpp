#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerTopic.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxTopicChanges = 3000;
}

PacketPlayerTopic::PacketPlayerTopic() : PlayerPacket()
{
    packetID = ID_PLAYER_TOPIC;
}

void PacketPlayerTopic::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->topicChangesAreLoad, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->topicChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxTopicChanges)
        {
            packetValid = false;
            player->topicChanges.clear();
            return;
        }

        player->topicChanges.clear();
        player->topicChanges.resize(count);
    }

    for (auto &&topic : player->topicChanges)
    {
        RW(topic.topicId, send, true);
    }
}
