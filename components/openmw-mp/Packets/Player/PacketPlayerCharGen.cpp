#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCharGen.hpp"

mwmp::PacketPlayerCharGen::PacketPlayerCharGen() : PlayerPacket()
{
    packetID = ID_PLAYER_CHARGEN;
}

void mwmp::PacketPlayerCharGen::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->charGenState, send);

}
