#ifndef OPENMW_MP_SERVEREVENTDISPATCHER_HPP
#define OPENMW_MP_SERVEREVENTDISPATCHER_HPP

namespace mwmp::ServerEvents
{
    void serverInit();
    void serverPostInit();
    void serverExit(bool restart);
    void requestDataFileList();

    void cellLoad(unsigned short playerId, const char* cellDescription);
    void cellUnload(unsigned short playerId, const char* cellDescription);
    void cellDeletion(const char* cellDescription);
}

#endif // OPENMW_MP_SERVEREVENTDISPATCHER_HPP
