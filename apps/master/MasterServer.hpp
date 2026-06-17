#ifndef NEWMASTERPROTO_MASTERSERVER_HPP
#define NEWMASTERPROTO_MASTERSERVER_HPP

#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <components/openmw-mp/Master/MasterData.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

class MasterServer
{
public:
    struct Ban
    {
        mwmp::PacketAddress sa;
        bool permanent;
        struct Date
        {
        } date;
    };
    struct SServer : QueryData
    {
        std::chrono::steady_clock::time_point lastUpdate;
    };
    typedef std::map<mwmp::PacketAddress, SServer> ServerMap;
    //typedef ServerMap::const_iterator ServerCIter;
    typedef ServerMap::iterator ServerIter;

    MasterServer(unsigned short maxConnections, unsigned short port);
    ~MasterServer();

    void Start();
    void Stop(bool wait = false);
    bool isRunning();
    void Wait();

    ServerMap* GetServers();

private:
    void Thread();

private:
    std::thread tMasterThread;
    std::unique_ptr<mwmp::GnsTransport> transport;
    ServerMap servers;
    std::atomic_bool run;
};


#endif //NEWMASTERPROTO_MASTERSERVER_HPP
