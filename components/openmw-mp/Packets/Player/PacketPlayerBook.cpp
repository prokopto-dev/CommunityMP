#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerBook.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxBookChanges = 3000;
}

PacketPlayerBook::PacketPlayerBook() : PlayerPacket()
{
    packetID = ID_PLAYER_BOOK;
}

void PacketPlayerBook::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->bookChangesAreLoad, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->bookChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxBookChanges)
        {
            packetValid = false;
            player->bookChanges.clear();
            return;
        }

        player->bookChanges.clear();
        player->bookChanges.resize(count);
    }

    for (auto &&book : player->bookChanges)
    {
        RW(book.bookId, send, true);
    }
}
