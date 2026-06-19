#include <apps/openmw/mwclass/creature.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include <vector>

#include "../mwbase/environment.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "CellController.hpp"
#include "DedicatedPlayer.hpp"
#include "GUIController.hpp"
#include "Main.hpp"
#include "PlayerList.hpp"

using namespace mwmp;

std::map<PacketGuid, DedicatedPlayer*> PlayerList::playerList;

void PlayerList::update(float dt)
{
    for (auto& playerEntry : playerList)
    {
        DedicatedPlayer* player = playerEntry.second;
        if (player == nullptr)
            continue;

        player->update(dt);
    }
}

DedicatedPlayer* PlayerList::newPlayer(PacketGuid guid)
{
    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new DedicatedPlayer with guid %s", packetGuidToString(guid).c_str());

    if (DedicatedPlayer* existingPlayer = getPlayer(guid))
        deletePlayer(guid);

    playerList[guid] = new DedicatedPlayer(guid);

    LOG_APPEND(TimedLog::LOG_INFO, "- There are now %i DedicatedPlayers", playerList.size());

    return playerList[guid];
}

void PlayerList::deletePlayer(PacketGuid guid)
{
    auto player = playerList.find(guid);
    if (player == playerList.end())
        return;

    if (player->second != nullptr && player->second->reference)
        player->second->deleteReference();

    delete player->second;
    playerList.erase(player);
}

std::vector<PacketGuid> PlayerList::deletePlayersByNameExcept(const std::string& name, PacketGuid preservedGuid)
{
    if (name.empty())
        return {};

    std::vector<PacketGuid> stalePlayers;
    for (const auto& playerEntry : playerList)
    {
        if (playerEntry.first == preservedGuid || playerEntry.second == nullptr)
            continue;

        if (playerEntry.second->npc.mName == name)
            stalePlayers.push_back(playerEntry.first);
    }

    for (PacketGuid staleGuid : stalePlayers)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Deleting stale remote avatar for %s with guid %s",
            name.c_str(), packetGuidToString(staleGuid).c_str());
        deletePlayer(staleGuid);
    }

    return stalePlayers;
}

void PlayerList::cleanUp()
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second != nullptr && playerEntry.second->reference)
            playerEntry.second->deleteReference();
        delete playerEntry.second;
    }
    playerList.clear();
}

DedicatedPlayer* PlayerList::getPlayer(PacketGuid guid)
{
    auto player = playerList.find(guid);
    if (player == playerList.end())
        return nullptr;

    return player->second;
}

DedicatedPlayer* PlayerList::getPlayer(const MWWorld::Ptr& ptr)
{
    if (ptr.mRef == nullptr)
        return nullptr;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (playerEntry.second->getPtr() == ptr)
            return playerEntry.second;
    }

    return nullptr;
}

DedicatedPlayer* PlayerList::getPlayer(int actorId)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWWorld::Ptr playerPtr = playerEntry.second->getPtr();
        int playerActorId = static_cast<int>(playerPtr.getCellRef().getRefNum().mIndex);

        if (actorId == playerActorId)
            return playerEntry.second;
    }

    return nullptr;
}

std::vector<PacketGuid> PlayerList::getPlayersInCell(const ESM::Cell& cell)
{
    std::vector<PacketGuid> playersInCell;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr)
            continue;

        if (isPacketGuidAssigned(playerEntry.first))
        {
            if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
            {
                playersInCell.push_back(playerEntry.first);
            }
        }
    }

    return playersInCell;
}

bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr& ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    return (getPlayer(ptr) != nullptr);
}

void PlayerList::enableMarkers(const ESM::Cell& cell)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
        {
            playerEntry.second->enableMarker();
        }
    }
}

/*
    Go through all DedicatedPlayers checking if their mHitAttemptActorId matches this one
    and set it to -1 if it does

    This resets the combat target for a DedicatedPlayer's followers in Actors::update()
*/
void PlayerList::clearHitAttemptActorId(int actorId)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWMechanics::CreatureStats& playerCreatureStats
            = playerEntry.second->getPtr().getClass().getCreatureStats(playerEntry.second->getPtr());

        if (static_cast<int>(playerCreatureStats.getHitAttemptActor().mIndex) == actorId)
            playerCreatureStats.setHitAttemptActor(ESM::RefNum());
    }
}
