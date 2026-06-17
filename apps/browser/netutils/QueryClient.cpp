#include "QueryClient.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <chrono>
#include <thread>
#include <qdebug.h>

using namespace std;
using namespace mwmp;

QueryClient::QueryClient()
{
    pmq = new PacketMasterQuery();
    pmu = new PacketMasterUpdate();
    status = -1;
}

QueryClient::~QueryClient()
{
    delete pmq;
    delete pmu;
}

void QueryClient::SetServer(const string &addr, unsigned short port)
{
    masterHost = addr;
    masterPort = port;
    masterAddr = makePacketAddress(addr.c_str(), port);
}

QueryClient &QueryClient::Get()
{
    static QueryClient myInstance;
    return myInstance;
}

map<PacketAddress, QueryData> QueryClient::Query()
{
    map<PacketAddress, QueryData> query;
    PacketStream bs;
    bs.Write((unsigned char) (ID_MASTER_QUERY));
    qDebug() << "Locking mutex in QueryClient::Query()";
    std::lock_guard lock(mxServers);
    status = -1;
    int attempts = 3;
    do
    {
        if (!Connect())
        {
            qDebug() << "Unlocking mutex in QueryClient::Query()";
            return query;
        }

        if (transport->send(bs.data(), bs.size(), PacketPriority::High,
                PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, masterAddr, false) == 0)
        {
            qDebug() << "Unlocking mutex in QueryClient::Query()";
            CloseConnection();
            return query;
        }

        pmq->SetServers(&query);
        status = GetAnswer(ID_MASTER_QUERY);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    while(status != ID_MASTER_QUERY && attempts-- > 0);
    if(status != ID_MASTER_QUERY)
        qDebug() << "Getting query was failed";
    qDebug() << "Unlocking mutex in QueryClient::Query()";
    CloseConnection();
    qDebug() <<"Answer" << (status == ID_MASTER_QUERY ? "ok." : "wrong.");

    return query;
}

pair<PacketAddress, QueryData> QueryClient::Update(const PacketAddress &addr)
{
    qDebug() << "Locking mutex in QueryClient::Update(PacketAddress addr)";
    pair<PacketAddress, QueryData> serverInfo;
    PacketStream bs;
    bs.Write((unsigned char) (ID_MASTER_UPDATE));
    writePacketAddress(bs, addr);

    std::lock_guard lock(mxServers);
    status = -1;
    int attempts = 3;
    pmu->SetServer(&serverInfo);
    do
    {
        if (!Connect())
        {
            qDebug() << "Unlocking mutex in QueryClient::Update(PacketAddress addr)";
            return serverInfo;
        }

        if (transport->send(bs.data(), bs.size(), PacketPriority::High, PacketReliability::ReliableOrderedWithAckReceipt,
                CHANNEL_MASTER, masterAddr, false) == 0)
        {
            CloseConnection();
            qDebug() << "Unlocking mutex in QueryClient::Update(PacketAddress addr)";
            return serverInfo;
        }
        status = GetAnswer(ID_MASTER_UPDATE);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    while(status != ID_MASTER_UPDATE && attempts-- > 0);
    if(status != ID_MASTER_UPDATE)
        qDebug() << "Getting update was failed";
    CloseConnection();
    qDebug() << "Unlocking mutex in QueryClient::Update(PacketAddress addr)";
    return serverInfo;
}

void QueryClient::CloseConnection()
{
    if (transport)
    {
        transport->closeConnection(masterAddr, true);
        transport.reset();
    }
}

MASTER_PACKETS QueryClient::GetAnswer(MASTER_PACKETS waitingPacket)
{
    bool update = true;
    unsigned char pid = 0;
    int id = -1;
    auto started = std::chrono::steady_clock::now();
    while (update)
    {
        for (ReceivedPacket* receivedPacket = transport->receive(); receivedPacket;
             transport->deallocatePacket(receivedPacket), receivedPacket = transport->receive())
        {
            PacketStream data(receivedPacket->data(), receivedPacket->length());
            pmq->SetReadStream(&data);
            pmu->SetReadStream(&data);
            data.Read(pid);
            switch(pid)
            {
                case ID_CONNECTION_LOST:
                    qDebug() << "ID_CONNECTION_LOST";
                case ID_DISCONNECTION_NOTIFICATION:
                    qDebug() << "Disconnected";
                    update = false;
                    break;
                case ID_MASTER_QUERY:
                    qDebug() << "ID_MASTER_QUERY";
                    if (waitingPacket == ID_MASTER_QUERY)
                        pmq->Read();
                    else
                        qDebug() << "Got wrong packet";
                    update = false;
                    id = pid;
                    break;
                case ID_MASTER_UPDATE:
                    qDebug() << "ID_MASTER_UPDATE";
                    if (waitingPacket == ID_MASTER_UPDATE)
                        pmu->Read();
                    else
                        qDebug() << "Got wrong packet";
                    update = false;
                    id = pid;
                    break;
                case ID_MASTER_ANNOUNCE:
                    qDebug() << "ID_MASTER_ANNOUNCE";
                    update = false;
                    id = pid;
                    break;
                case ID_CONNECTION_REQUEST_ACCEPTED:
                    qDebug() << "ID_CONNECTION_REQUEST_ACCEPTED";
                    break;
                default:
                    break;
            }
        }
        if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(5))
            update = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return (MASTER_PACKETS)(id);
}

bool QueryClient::Connect()
{
    try
    {
        transport = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client, false);
        transport->connect(masterHost, masterPort);
    }
    catch (const std::exception& e)
    {
        qDebug() << "Cannot connect to the master server:" << e.what();
        transport.reset();
        return false;
    }

    auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
    {
        for (ReceivedPacket* receivedPacket = transport->receive(); receivedPacket;
             transport->deallocatePacket(receivedPacket), receivedPacket = transport->receive())
        {
            switch (receivedPacket->id())
            {
                case ID_CONNECTION_REQUEST_ACCEPTED:
                    qDebug() << "Connected";
                    return true;
                case ID_CONNECTION_LOST:
                case ID_DISCONNECTION_NOTIFICATION:
                case ID_CONNECTION_ATTEMPT_FAILED:
                    qDebug() << "Cannot connect to the master server.";
                    transport.reset();
                    return false;
                default:
                    break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qDebug() << "Cannot connect to the master server. Timed out.";
    transport.reset();
    return false;
}

int QueryClient::Status()
{
    return status;
}
