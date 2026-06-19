#ifndef OPENMW_PLAYER_HPP
#define OPENMW_PLAYER_HPP

#include <map>
#include <mutex>
#include <string>
#include <chrono>
#include <utility>
#include <vector>

#include <components/esm3/npcstats.hpp>
#include <components/esm3/cellid.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcell.hpp>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include "Cell.hpp"
#include "CellController.hpp"

typedef std::map<mwmp::PacketGuid, Player*> TPlayers;
typedef std::map<unsigned short, Player*> TSlots;

class Players
{
public:
    static void newPlayer(mwmp::PacketGuid guid);
    static void deletePlayer(mwmp::PacketGuid guid);
    static Player *getPlayer(mwmp::PacketGuid guid);
    static Player *getPlayer(unsigned short id);
    static TPlayers *getPlayers();
    static std::pair<unsigned int, std::vector<std::string>> getMasterListSnapshot();
    static unsigned short getLastPlayerId();
    static bool doesPlayerExist(mwmp::PacketGuid guid);

private:
    static TPlayers players;
    static TSlots slots;
    static std::mutex mutex;
};

class Player : public mwmp::BasePlayer
{
    friend class Cell;
    unsigned short id;
public:

    enum
    {
        NOTLOADED=0,
        LOADED,
        POSTLOADED,
        KICKED
    };
    Player(mwmp::PacketGuid guid);

    unsigned short getId();
    void setId(unsigned short id);

    bool isHandshaked();
    int getHandshakeAttempts();
    void incrementHandshakeAttempts();
    void setHandshake();
    bool hasPendingLoaded() const;
    void setPendingLoaded(bool pendingLoaded);
    const std::string& getLoginName() const;
    void setLoginName(std::string name);
    const std::string& getLoginPasswordHash() const;
    void setLoginPasswordHash(std::string passwordHash);
    void clearLoginPasswordHash();

    void setLoadState(int state);
    int getLoadState() const;

    virtual ~Player();

    CellController::TContainer *getCells();
    void sendToGuid(mwmp::PlayerPacket *myPacket, mwmp::PacketGuid targetGuid);
    void sendToGuidWithReliability(mwmp::PlayerPacket *myPacket, mwmp::PacketGuid targetGuid,
        mwmp::PacketReliability reliability);
    void sendToLoaded(mwmp::PlayerPacket *myPacket);
    void sendToLoadedWithReliability(mwmp::PlayerPacket *myPacket, mwmp::PacketReliability reliability);
    void sendToLoadedAndGuid(mwmp::PlayerPacket *myPacket, mwmp::PacketGuid targetGuid);
    void sendToLoadedAndRecentCellVisitorsWithReliability(mwmp::PlayerPacket *myPacket,
        mwmp::PacketReliability reliability);

    void forEachLoaded(std::function<void(Player *pl, Player *other)> func);

private:
    CellController::TContainer cells;
    std::string loginName;
    std::string loginPasswordHash;
    int loadState;
    int handshakeCounter;
    bool pendingLoaded;

};

#endif //OPENMW_PLAYER_HPP
