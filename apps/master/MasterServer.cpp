#include <chrono>
#include <iostream>
#include <thread>
#include "MasterServer.hpp"

#include <components/openmw-mp/Master/PacketMasterQuery.hpp>
#include <components/openmw-mp/Master/PacketMasterUpdate.hpp>
#include <components/openmw-mp/Master/PacketMasterAnnounce.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

using namespace std;
using namespace mwmp;
using namespace chrono;

MasterServer::MasterServer(unsigned short maxConnections, unsigned short port)
{
    transport = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Server, false);
    transport->startupServer("", port, maxConnections);
    run = false;
}

MasterServer::~MasterServer()
{
    Stop(true);
}

void MasterServer::Thread()
{
    auto makeQuerySnapshot = [this]() {
        map<PacketAddress, QueryData> snapshot;
        for (const auto& [address, server] : servers)
            snapshot.emplace(address, static_cast<const QueryData&>(server));
        return snapshot;
    };

    unsigned char packetId = 0;

    auto startTime = chrono::steady_clock::now();

    PacketStream send;
    PacketMasterQuery pmq;
    PacketMasterUpdate pmu;
    PacketMasterAnnounce pma;

    auto sendPacket = [&](auto& masterPacket, const PacketAddress& destination) {
        send.Reset();
        masterPacket.Packet(&send, true);
        transport->send(send.data(), send.size(), PacketPriority::High, PacketReliability::ReliableOrderedWithAckReceipt,
            CHANNEL_MASTER, destination, false);
    };

    while (run.load())
    {
        ReceivedPacket* receivedPacket = transport->receive();

        auto now = steady_clock::now();
        if (now - startTime >= 60s)
        {
            startTime = steady_clock::now();
            for (auto it = servers.begin(); it != servers.end();)
            {
                if (it->second.lastUpdate + 60s <= now)
                    servers.erase(it++);
                else
                    ++it;
            }
        }

        if (receivedPacket == nullptr)
            this_thread::sleep_for(10ms);
        else
        {
            for (; receivedPacket; transport->deallocatePacket(receivedPacket), receivedPacket = transport->receive())
            {
                PacketStream data(receivedPacket->data(), receivedPacket->length());
                data.Read(packetId);
                switch (packetId)
                {
                    case ID_NEW_INCOMING_CONNECTION:
                        cout << "New incoming connection: " << packetAddressToString(receivedPacket->address(), true) << endl;
                        break;
                    case ID_DISCONNECTION_NOTIFICATION:
                        cout << "Disconnected: " << packetAddressToString(receivedPacket->address(), true) << endl;
                        break;
                    case ID_CONNECTION_LOST:
                        cout << "Connection lost: " << packetAddressToString(receivedPacket->address(), true) << endl;
                        break;
                    case ID_MASTER_QUERY:
                    {
                        auto serverSnapshot = makeQuerySnapshot();
                        pmq.SetServers(&serverSnapshot);
                        sendPacket(pmq, receivedPacket->address());

                        cout << "Sent info about all " << servers.size() << " servers to "
                             << packetAddressToString(receivedPacket->address(), true) << endl;
                        break;
                    }
                    case ID_MASTER_UPDATE:
                    {
                        PacketAddress addr;
                        if (!readPacketAddress(data, addr))
                        {
                            transport->closeConnection(receivedPacket->destination(), true);
                            break;
                        }

                        ServerIter it = servers.find(addr);
                        if (it != servers.end())
                        {
                            pair<PacketAddress, QueryData> pairPtr(it->first, static_cast<QueryData>(it->second));
                            pmu.SetServer(&pairPtr);
                            sendPacket(pmu, receivedPacket->address());
                            cout << "Sent info about " << packetAddressToString(addr, true) << " to "
                                 << packetAddressToString(receivedPacket->address(), true) << endl;
                        }
                        else
                            transport->closeConnection(receivedPacket->destination(), true);
                        break;
                    }
                    case ID_MASTER_ANNOUNCE:
                    {
                        pma.SetReadStream(&data);
                        SServer server;
                        pma.SetServer(&server);
                        pma.Read();

                        PacketAddress serverAddress = receivedPacket->address();
                        if (pma.GetAdvertisedPort() != 0)
                            setPacketAddressPortHostOrder(serverAddress, pma.GetAdvertisedPort());

                        ServerIter iter = servers.find(serverAddress);

                        auto keepAliveFunc = [&]() {
                            iter->second.lastUpdate = now;
                            pma.SetFunc(PacketMasterAnnounce::FUNCTION_KEEP);
                            sendPacket(pma, receivedPacket->address());
                        };

                        if (iter != servers.end())
                        {
                            if (pma.GetFunc() == PacketMasterAnnounce::FUNCTION_DELETE)
                            {
                                servers.erase(iter);
                                cout << "Deleted";
                                sendPacket(pma, receivedPacket->address());
                            }
                            else if (pma.GetFunc() == PacketMasterAnnounce::FUNCTION_ANNOUNCE)
                            {
                                cout << "Updated";
                                iter->second = server;
                                keepAliveFunc();
                            }
                            else
                            {
                                cout << "Keeping alive";
                                keepAliveFunc();
                            }
                        }
                        else if (pma.GetFunc() == PacketMasterAnnounce::FUNCTION_ANNOUNCE)
                        {
                            cout << "Added";
                            iter = servers.insert({serverAddress, server}).first;
                            keepAliveFunc();
                        }
                        else
                        {
                            cout << "Unknown";
                            pma.SetFunc(PacketMasterAnnounce::FUNCTION_DELETE);
                            sendPacket(pma, receivedPacket->address());
                        }
                        cout << " server " << packetAddressToString(serverAddress, true) << endl;
                        break;
                    }
                    default:
                        cout << "Wrong packet. id " << (unsigned) receivedPacket->id() << " packet length "
                             << receivedPacket->length() << " from "
                             << packetAddressToString(receivedPacket->address(), true) << endl;
                        transport->closeConnection(receivedPacket->destination(), true);
                }
            }
        }
    }
    cout << "Server thread stopped" << endl;
}

void MasterServer::Start()
{
    if (!run.exchange(true))
    {
        tMasterThread = thread(&MasterServer::Thread, this);
        cout << "Started" << endl;
    }
}

void MasterServer::Stop(bool wait)
{
    if (run.exchange(false))
    {
        if (wait && tMasterThread.joinable())
            tMasterThread.join();
    }
}

bool MasterServer::isRunning()
{
    return run.load();
}

void MasterServer::Wait()
{
    if (run.load())
    {
        if (tMasterThread.joinable())
            tMasterThread.join();
    }
}

MasterServer::ServerMap *MasterServer::GetServers()
{
    return &servers;
}
