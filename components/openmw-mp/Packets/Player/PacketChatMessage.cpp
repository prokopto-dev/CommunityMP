#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketChatMessage.hpp"

mwmp::PacketChatMessage::PacketChatMessage() : PlayerPacket()
{
    packetID = ID_CHAT_MESSAGE;
    orderChannel = CHANNEL_SYSTEM;
}

void mwmp::PacketChatMessage::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->chatMessage, send);
}
