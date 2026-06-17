#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerJournal.hpp"

using namespace mwmp;

namespace
{
    constexpr uint32_t maxJournalChanges = 3000;
}

PacketPlayerJournal::PacketPlayerJournal() : PlayerPacket()
{
    packetID = ID_PLAYER_JOURNAL;
}

void PacketPlayerJournal::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->journalChangesAreLoad, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->journalChanges.size());

    if (!RW(count, send))
        return;

    if (!send)
    {
        if (count > maxJournalChanges)
        {
            packetValid = false;
            player->journalChanges.clear();
            return;
        }

        player->journalChanges.clear();
        player->journalChanges.resize(count);
    }

    for (auto &&journalItem : player->journalChanges)
    {
        RW(journalItem.type, send);
        RW(journalItem.quest, send, true);
        RW(journalItem.index, send);

        if (journalItem.type == JournalItem::ENTRY)
        {
            RW(journalItem.actorRefId, send, true);

            RW(journalItem.hasTimestamp, send);

            if (journalItem.hasTimestamp)
            {
                RW(journalItem.timestamp.daysPassed, send);
                RW(journalItem.timestamp.month, send);
                RW(journalItem.timestamp.day, send);
            }
        }
        else if (journalItem.type == JournalItem::FINISHED)
            RW(journalItem.isFinished, send);
    }
}
