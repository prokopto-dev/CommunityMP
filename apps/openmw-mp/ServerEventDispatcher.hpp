#ifndef OPENMW_MP_SERVEREVENTDISPATCHER_HPP
#define OPENMW_MP_SERVEREVENTDISPATCHER_HPP

namespace mwmp::ServerEvents
{
    void serverInit();
    void serverPostInit();
    void serverExit(bool restart);
    void requestDataFileList();
    void mpNumIncrement(int mpNum);

    void playerConnect(unsigned short playerId);
    void playerDisconnect(unsigned short playerId);
    void playerBaseInfo(unsigned short playerId);
    void playerCellChange(unsigned short playerId);
    void playerEndCharGen(unsigned short playerId);
    void playerDeath(unsigned short playerId);
    void playerStatsDynamic(unsigned short playerId);
    void actorCellChange(unsigned short playerId, const char* cellDescription);
    void actorStatsDynamic(unsigned short playerId, const char* cellDescription);

    void cellLoad(unsigned short playerId, const char* cellDescription);
    void cellUnload(unsigned short playerId, const char* cellDescription);
    void cellDeletion(const char* cellDescription);

    void playerEvent(const char* eventName, unsigned short playerId);
    void actorEvent(const char* eventName, unsigned short playerId, const char* cellDescription);
    void objectEvent(const char* eventName, unsigned short playerId, const char* cellDescription);
}

#endif // OPENMW_MP_SERVEREVENTDISPATCHER_HPP
