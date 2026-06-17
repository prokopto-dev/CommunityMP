#include "Player.hpp"
#include "Networking.hpp"

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include <algorithm>
#include <limits>

TPlayers Players::players;
TSlots Players::slots;
std::mutex Players::mutex;

void Players::deletePlayer(mwmp::PacketGuid guid)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Deleting player with guid %llu",
        static_cast<unsigned long long>(mwmp::packetGuidValue(guid)));

    Player* player = nullptr;
    unsigned short playerId = 0;
    {
        std::lock_guard lock(mutex);
        const auto playerIt = players.find(guid);
        if (playerIt == players.end() || playerIt->second == nullptr)
            return;

        player = playerIt->second;
        playerId = player->getId();
        slots[playerId] = nullptr;
        players.erase(playerIt);
    }

    CellController::get()->deletePlayer(player);

    LOG_APPEND(TimedLog::LOG_INFO, "- Emptying slot %i", playerId);

    delete player;
}

void Players::newPlayer(mwmp::PacketGuid guid)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Creating new player with guid %llu",
        static_cast<unsigned long long>(mwmp::packetGuidValue(guid)));

    Player* player = new Player(guid);
    player->cell.blank();
    player->npc.blank();
    player->npcStats.blank();
    player->creatureStats.blank();
    player->charClass.blank();
    player->scale = 1;
    player->isWerewolf = false;

    std::lock_guard lock(mutex);
    players[guid] = player;
    const unsigned int maxConnections = std::min<unsigned int>(
        mwmp::Networking::get().maxConnections(), std::numeric_limits<unsigned short>::max());
    for (unsigned short i = 0; i < maxConnections; i++)
    {
        if (slots[i] == 0)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Storing in slot %i", i);

            slots[i] = player;
            slots[i]->setId(i);
            break;
        }
    }
}

Player *Players::getPlayer(mwmp::PacketGuid guid)
{
    auto it = players.find(guid);
    if (it == players.end())
        return nullptr;
    return it->second;
}

TPlayers *Players::getPlayers()
{
    return &players;
}

std::pair<unsigned int, std::vector<std::string>> Players::getMasterListSnapshot()
{
    std::lock_guard lock(mutex);

    std::vector<std::string> playerNames;
    playerNames.reserve(players.size());

    for (const auto& [guid, player] : players)
    {
        if (player != nullptr && !player->npc.mName.empty())
            playerNames.push_back(player->npc.mName);
    }

    return { static_cast<unsigned int>(players.size()), std::move(playerNames) };
}

unsigned short Players::getLastPlayerId()
{
    return slots.rbegin()->first;
}

Player::Player(mwmp::PacketGuid guid) : BasePlayer(guid)
{
    handshakeCounter = 0;
    loadState = NOTLOADED;
    pendingLoaded = false;
}

Player::~Player()
{

}

unsigned short Player::getId()
{
    return id;
}

void Player::setId(unsigned short playerId)
{
    id = playerId;
}

bool Player::isHandshaked()
{
    return handshakeCounter == std::numeric_limits<int>::max();
}

void Player::setHandshake()
{
    handshakeCounter = std::numeric_limits<int>::max();
}

bool Player::hasPendingLoaded() const
{
    return pendingLoaded;
}

void Player::setPendingLoaded(bool loaded)
{
    pendingLoaded = loaded;
}

void Player::incrementHandshakeAttempts()
{
    handshakeCounter++;
}

const std::string& Player::getLoginName() const
{
    return loginName;
}

void Player::setLoginName(std::string name)
{
    loginName = std::move(name);
}

const std::string& Player::getLoginPasswordHash() const
{
    return loginPasswordHash;
}

void Player::setLoginPasswordHash(std::string passwordHash)
{
    loginPasswordHash = std::move(passwordHash);
}

void Player::clearLoginPasswordHash()
{
    loginPasswordHash.clear();
}

int Player::getHandshakeAttempts()
{
    return handshakeCounter;
}


void Player::setLoadState(int state)
{
    loadState = state;
}

int Player::getLoadState()
{
    return loadState;
}

Player *Players::getPlayer(unsigned short id)
{
    auto it = slots.find(id);
    if (it == slots.end())
        return nullptr;
    return it->second;
}

CellController::TContainer *Player::getCells()
{
    return &cells;
}

void Player::sendToLoaded(mwmp::PlayerPacket *myPacket)
{
    std::list <Player*> plList;

    for (auto loadedCell : cells)
    {
        if (loadedCell == nullptr)
            continue;

        for (auto pl : *loadedCell)
        {
            if (pl != nullptr && !pl->npc.mName.empty())
                plList.push_back(pl);
        }
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl == this) continue;
        myPacket->setPlayer(this);
        myPacket->Send(pl->guid);
    }
}

void Player::sendToLoadedAndGuid(mwmp::PlayerPacket *myPacket, mwmp::PacketGuid targetGuid)
{
    std::list <Player*> plList;

    for (auto loadedCell : cells)
    {
        if (loadedCell == nullptr)
            continue;

        for (auto pl : *loadedCell)
        {
            if (pl != nullptr && !pl->npc.mName.empty())
                plList.push_back(pl);
        }
    }

    if (targetGuid != mwmp::unassignedPacketGuid() && targetGuid != guid)
    {
        Player* target = Players::getPlayer(targetGuid);
        if (target != nullptr && !target->npc.mName.empty())
            plList.push_back(target);
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl == this) continue;
        myPacket->setPlayer(this);
        myPacket->Send(pl->guid);
    }
}

void Player::forEachLoaded(std::function<void(Player *pl, Player *other)> func)
{
    std::list <Player*> plList;

    for (auto loadedCell : cells)
    {
        if (loadedCell == nullptr)
            continue;

        for (auto pl : *loadedCell)
        {
            if (pl != nullptr && !pl->npc.mName.empty())
                plList.push_back(pl);
        }
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl == this) continue;
        func(this, pl);
    }
}

bool Players::doesPlayerExist(mwmp::PacketGuid guid)
{
    return players.find(guid) != players.end();
}
