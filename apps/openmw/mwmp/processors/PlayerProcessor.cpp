#include "../Networking.hpp"
#include "PlayerProcessor.hpp"
#include "../Main.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

#include <algorithm>
#include <map>
#include <vector>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

namespace
{
    struct PendingPlayerPacket
    {
        std::vector<unsigned char> data;
        PacketGuid transportGuid;
        PacketAddress address;
    };

    std::map<PacketGuid, std::vector<PendingPlayerPacket>> sPendingRemotePlayerPackets;

    int pendingPacketReplayPriority(PacketId packetId)
    {
        switch (packetId)
        {
            case ID_PLAYER_CELL_CHANGE:
                return 10;
            case ID_PLAYER_EQUIPMENT:
                return 20;
            case ID_PLAYER_SHAPESHIFT:
                return 30;
            case ID_PLAYER_STATS_DYNAMIC:
            case ID_PLAYER_ATTRIBUTE:
            case ID_PLAYER_SKILL:
                return 40;
            case ID_PLAYER_POSITION:
                return 80;
            case ID_PLAYER_ANIM_FLAGS:
                return 90;
            default:
                return 50;
        }
    }

    bool shouldQueuePendingRemotePlayerPacket(PacketId packetId, bool request)
    {
        if (request)
            return false;

        switch (packetId)
        {
            case ID_PLAYER_CELL_CHANGE:
            case ID_PLAYER_EQUIPMENT:
            case ID_PLAYER_SHAPESHIFT:
            case ID_PLAYER_STATS_DYNAMIC:
            case ID_PLAYER_ATTRIBUTE:
            case ID_PLAYER_SKILL:
            case ID_PLAYER_POSITION:
            case ID_PLAYER_ANIM_FLAGS:
                return true;
            default:
                return false;
        }
    }

    void queuePendingRemotePlayerPacket(ReceivedPacket& packet, PacketGuid playerGuid)
    {
        if (packet.data() == nullptr || packet.length() == 0)
            return;

        auto& pendingPackets = sPendingRemotePlayerPackets[playerGuid];
        const PacketId packetId = packet.id();
        if (packetId == ID_PLAYER_POSITION || packetId == ID_PLAYER_ANIM_FLAGS)
        {
            auto existingPacket = std::find_if(pendingPackets.rbegin(), pendingPackets.rend(),
                [packetId](const PendingPlayerPacket& pendingPacket) {
                    return !pendingPacket.data.empty() && pendingPacket.data.front() == packetId;
                });

            if (existingPacket != pendingPackets.rend())
            {
                existingPacket->data.assign(packet.data(), packet.data() + packet.length());
                existingPacket->transportGuid = packet.guid();
                existingPacket->address = packet.address();
                return;
            }
        }

        constexpr std::size_t maxPendingPacketsPerRemotePlayer = 32;
        if (pendingPackets.size() >= maxPendingPacketsPerRemotePlayer)
            pendingPackets.erase(pendingPackets.begin());

        pendingPackets.push_back(PendingPlayerPacket{
            std::vector<unsigned char>(packet.data(), packet.data() + packet.length()),
            packet.guid(),
            packet.address()
        });
    }
}

PlayerProcessor::~PlayerProcessor()
{

}

void PlayerProcessor::replayPendingPacketsForPlayer(PacketGuid playerGuid)
{
    auto pendingIt = sPendingRemotePlayerPackets.find(playerGuid);
    if (pendingIt == sPendingRemotePlayerPackets.end())
        return;

    std::vector<PendingPlayerPacket> pendingPackets = std::move(pendingIt->second);
    sPendingRemotePlayerPackets.erase(pendingIt);

    std::stable_sort(pendingPackets.begin(), pendingPackets.end(),
        [](const PendingPlayerPacket& left, const PendingPlayerPacket& right) {
            const PacketId leftId = !left.data.empty() ? left.data.front() : 0;
            const PacketId rightId = !right.data.empty() ? right.data.front() : 0;
            return pendingPacketReplayPriority(leftId) < pendingPacketReplayPriority(rightId);
        });

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Replaying %u pending packets for newly created remote player",
        static_cast<unsigned int>(pendingPackets.size()));

    for (PendingPlayerPacket& pendingPacket : pendingPackets)
    {
        if (pendingPacket.data.empty())
            continue;

        ReceivedPacket replayPacket(std::move(pendingPacket.data), pendingPacket.transportGuid, pendingPacket.address);
        Process(replayPacket);
    }
}

void PlayerProcessor::clearPendingPacketsForPlayer(PacketGuid playerGuid)
{
    sPendingRemotePlayerPackets.erase(playerGuid);
}

bool PlayerProcessor::Process(ReceivedPacket& packet)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;

    PlayerPacket *myPacket = Main::get().getNetworking()->getPlayerPacket(packet.id());
    myPacket->SetReadStream(&bsIn);

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length() == myPacket->headerSize();

            BasePlayer *player = 0;
            if (guid != myGuid)
                player = PlayerList::getPlayer(guid);
            else
                player = Main::get().getLocalPlayer();

            if (player == nullptr && guid != myGuid
                && shouldQueuePendingRemotePlayerPacket(packet.id(), request))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Queued %s for remote player that has not been created locally yet",
                    processor.second->strPacketID.c_str());
                queuePendingRemotePlayerPacket(packet, guid);
                return true;
            }

            if (request && guid == myGuid)
            {
                LocalPlayer* localPlayer = Main::get().getLocalPlayer();
                if (localPlayer != nullptr && !localPlayer->isLoggedIn())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                        "Ignoring %s request for LocalPlayer until character login completes",
                        processor.second->strPacketID.c_str());
                    return true;
                }
            }

            if (!request && !processor.second->avoidReading && player != 0)
            {
                myPacket->setPlayer(player);
                myPacket->Read();

                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                        "Received %s that failed integrity check and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
            }

            processor.second->Do(*myPacket, player);
            return true;
        }
    }
    return false;
}

