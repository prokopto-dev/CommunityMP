#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerClass.hpp"

mwmp::PacketPlayerClass::PacketPlayerClass() : PlayerPacket()
{
    packetID = ID_PLAYER_CHARCLASS;
}

void mwmp::PacketPlayerClass::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream,  send);

    RW(player->charClass.mId, send);

    if (player->charClass.mId.empty()) // custom class
    {
        RW(player->charClass.mName, send, true);
        RW(player->charClass.mDescription, send, true);
        RW(player->charClass.mData, send, true);
    }
}
