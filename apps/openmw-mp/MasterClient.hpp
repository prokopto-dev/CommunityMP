#ifndef OPENMW_MASTERCLIENT_HPP
#define OPENMW_MASTERCLIENT_HPP

#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <thread>
#include <components/openmw-mp/Master/MasterData.hpp>
#include <components/openmw-mp/Master/PacketMasterAnnounce.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

class MasterClient
{
public:
    static const unsigned int step_rate = 1000;
    static const unsigned int min_rate = 1000;
    static const unsigned int max_rate = 60000;
public:
    MasterClient(std::string queryAddr, unsigned short queryPort);
    void SetPlayers(unsigned pl);
    void SetMaxPlayers(unsigned pl);
    void SetHostname(std::string hostname);
    void SetModname(std::string hostname);
    void SetRuleString(std::string key, std::string value);
    void SetRuleValue(std::string key, double value);
    void PushPlugin(Plugin plugin);

    bool Process(mwmp::ReceivedPacket* packet);
    void Start();
    void Stop();
    void SetUpdateRate(unsigned int rate);

private:
    bool ProcessPacket(mwmp::ReceivedPacket* packet, bool requireMasterAddress);
    void Send(mwmp::PacketMasterAnnounce::Func func, const QueryData& data);
    void Thread();
private:
    std::string masterHost;
    unsigned short masterPort;
    mwmp::PacketAddress masterServer;
    QueryData queryData;
    std::atomic<unsigned int> timeout;
    std::atomic_bool mRun;
    std::mutex mutexData;
    std::mutex mutexPacket;
    std::thread thrQuery;
    mwmp::PacketMasterAnnounce pma;
    mwmp::PacketStream writeStream;
    std::unique_ptr<mwmp::GnsTransport> masterTransport;
    bool updated;
};


#endif //OPENMW_MASTERCLIENT_HPP
