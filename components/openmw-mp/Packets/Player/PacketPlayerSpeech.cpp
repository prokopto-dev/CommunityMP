#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerSpeech.hpp"

mwmp::PacketPlayerSpeech::PacketPlayerSpeech() : PlayerPacket()
{
    packetID = ID_PLAYER_SPEECH;
}

void mwmp::PacketPlayerSpeech::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->sound, send);
}
