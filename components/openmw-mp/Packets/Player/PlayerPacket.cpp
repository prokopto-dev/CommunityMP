#include <components/openmw-mp/NetworkMessages.hpp>
#include "PlayerPacket.hpp"

using namespace mwmp;

PlayerPacket::PlayerPacket() : BasePacket()
{
    packetID = 0;
    priority = PacketPriority::High;
    reliability = PacketReliability::ReliableOrdered;
    orderChannel = CHANNEL_PLAYER;
}

PlayerPacket::~PlayerPacket()
{

}

void PlayerPacket::setPlayer(BasePlayer *newPlayer)
{
    player = newPlayer;
    guid = player->guid;
}

BasePlayer *PlayerPacket::getPlayer()
{
    return player;
}
