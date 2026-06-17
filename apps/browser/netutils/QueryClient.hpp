#ifndef OPENMW_QUERYCLIENT_HPP
#define OPENMW_QUERYCLIENT_HPP

#include <memory>
#include <string>
#include <components/openmw-mp/Master/PacketMasterQuery.hpp>
#include <components/openmw-mp/Master/PacketMasterUpdate.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <apps/browser/ServerModel.hpp>
#include <mutex>

class QueryClient
{
public:
    QueryClient(QueryClient const &) = delete;
    QueryClient(QueryClient &&) = delete;
    QueryClient &operator=(QueryClient const &) = delete;
    QueryClient &operator=(QueryClient &&) = delete;

    static QueryClient &Get();
    void SetServer(const std::string &addr, unsigned short port);
    std::map<mwmp::PacketAddress, QueryData> Query();
    std::pair<mwmp::PacketAddress, QueryData> Update(const mwmp::PacketAddress &addr);
    int Status();
private:
    void CloseConnection();
    bool Connect();
    MASTER_PACKETS GetAnswer(MASTER_PACKETS packet);
protected:
    QueryClient();
    ~QueryClient();
private:
    int status;
    std::unique_ptr<mwmp::GnsTransport> transport;
    std::string masterHost;
    unsigned short masterPort = 0;
    mwmp::PacketAddress masterAddr;
    mwmp::PacketMasterQuery *pmq;
    mwmp::PacketMasterUpdate *pmu;
    std::pair<mwmp::PacketAddress, ServerData> server;
    std::mutex mxServers;

};


#endif //OPENMW_QUERYCLIENT_HPP
