#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include <components/openmw-mp/Master/MasterData.hpp>
#include <components/openmw-mp/Master/PacketMasterAnnounce.hpp>
#include <components/openmw-mp/Master/PacketMasterQuery.hpp>
#include <components/openmw-mp/Master/PacketMasterUpdate.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

using namespace std;
using namespace mwmp;

namespace
{
    constexpr unsigned short gamePort = 25565;

    bool hasConsoleInput()
    {
#ifdef _WIN32
        return _kbhit() != 0;
#else
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(STDIN_FILENO, &readSet);

        timeval timeout {};
        return select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout) > 0;
#endif
    }

    bool waitForConnection(GnsTransport& transport)
    {
        const auto started = chrono::steady_clock::now();
        while (chrono::steady_clock::now() - started < chrono::seconds(5))
        {
            for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
                 transport.deallocatePacket(receivedPacket), receivedPacket = transport.receive())
            {
                switch (receivedPacket->id())
                {
                    case ID_CONNECTION_REQUEST_ACCEPTED:
                        printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n",
                            packetAddressToString(receivedPacket->address(), true).c_str(),
                            packetGuidToString(receivedPacket->guid()).c_str());
                        return true;
                    case ID_CONNECTION_ATTEMPT_FAILED:
                        printf("Connection attempt failed\n");
                        return false;
                    case ID_CONNECTION_BANNED:
                        printf("We are banned from this server.\n");
                        return false;
                    case ID_NO_FREE_INCOMING_CONNECTIONS:
                        printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
                        return false;
                    case ID_CONNECTION_LOST:
                    case ID_DISCONNECTION_NOTIFICATION:
                        printf("Disconnected before connection completed\n");
                        return false;
                    default:
                        break;
                }
            }

            this_thread::sleep_for(chrono::milliseconds(10));
        }

        printf("Connection attempt timed out\n");
        return false;
    }

    bool sendMasterRequest(GnsTransport& transport, PacketStream& send, const PacketAddress& masterAddr)
    {
        return transport.send(send.data(), send.size(), PacketPriority::High, PacketReliability::ReliableOrdered,
            CHANNEL_MASTER, masterAddr, false)
            != 0;
    }
}

int main()
{
    cout << "Server test" << endl;

    PacketAddress masterAddr = makePacketAddress("127.0.0.1", 25560);
    GnsTransport transport(GnsMode::Client, false);

    try
    {
        transport.connect(packetAddressToString(masterAddr, false), packetAddressPort(masterAddr));
    }
    catch (const std::exception& e)
    {
        cerr << "Could not connect to master server: " << e.what() << endl;
        return 1;
    }

    if (!waitForConnection(transport))
        return 1;

    BasePacket::SetPacketTransport(&transport);

    string message;
    PacketStream send;

    PacketMasterQuery pmq;
    pmq.SetSendStream(&send);

    PacketMasterAnnounce pma;
    pma.SetSendStream(&send);
    pma.SetAdvertisedPort(gamePort);

    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(30));

        if (hasConsoleInput())
        {
            getline(cin, message);

            if (message == "quit")
            {
                puts("Quitting.");
                break;
            }
            else if (message == "send")
            {
                puts("Sending data about server");
                QueryData server;
                server.SetName("Super Server");
                server.SetPlayers(0);
                server.SetMaxPlayers(0);

                pma.SetServer(&server);
                pma.SetFunc(PacketMasterAnnounce::FUNCTION_ANNOUNCE);
                pma.SetAdvertisedPort(gamePort);
                pma.Send(masterAddr);
            }
            else if (message == "get")
            {
                puts("Request query info");
                send.Reset();
                send.Write(static_cast<unsigned char>(ID_MASTER_QUERY));
                sendMasterRequest(transport, send, masterAddr);
            }
            else if (message == "getme")
            {
                send.Reset();
                send.Write(static_cast<unsigned char>(ID_MASTER_UPDATE));
                writePacketAddress(send, makePacketAddress("127.0.0.1", gamePort));
                sendMasterRequest(transport, send, masterAddr);
            }
            else if (message == "status")
            {
                cout << (transport.isConnected() ? "Connected" : "Not connected") << endl;
            }
            else if (message == "keep")
            {
                cout << "Sending keep alive" << endl;
                pma.SetFunc(PacketMasterAnnounce::FUNCTION_KEEP);
                pma.SetAdvertisedPort(gamePort);
                pma.Send(masterAddr);
            }
        }

        for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
             transport.deallocatePacket(receivedPacket), receivedPacket = transport.receive())
        {
            PacketStream data(receivedPacket->data(), receivedPacket->length());
            unsigned char packetID;
            data.Read(packetID);
            switch (packetID)
            {
                case ID_DISCONNECTION_NOTIFICATION:
                    printf("ID_DISCONNECTION_NOTIFICATION\n");
                    break;
                case ID_ALREADY_CONNECTED:
                    printf("ID_ALREADY_CONNECTED with guid %llu\n",
                        static_cast<unsigned long long>(packetGuidValue(receivedPacket->guid())));
                    break;
                case ID_INCOMPATIBLE_PROTOCOL_VERSION:
                    printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
                    break;
                case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                    printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
                    break;
                case ID_REMOTE_CONNECTION_LOST:
                    printf("ID_REMOTE_CONNECTION_LOST\n");
                    break;
                case ID_REMOTE_NEW_INCOMING_CONNECTION:
                    printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
                    break;
                case ID_CONNECTION_BANNED:
                    printf("We are banned from this server.\n");
                    break;
                case ID_CONNECTION_ATTEMPT_FAILED:
                    printf("Connection attempt failed\n");
                    break;
                case ID_NO_FREE_INCOMING_CONNECTIONS:
                    printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
                    break;

                case ID_INVALID_PASSWORD:
                    printf("ID_INVALID_PASSWORD\n");
                    break;

                case ID_CONNECTION_LOST:
                    printf("ID_CONNECTION_LOST\n");
                    BasePacket::SetPacketTransport(nullptr);
                    return 0;

                case ID_CONNECTION_REQUEST_ACCEPTED:
                    printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n",
                        packetAddressToString(receivedPacket->address(), true).c_str(),
                        packetGuidToString(receivedPacket->guid()).c_str());
                    break;
                case ID_MASTER_QUERY:
                {
                    map<PacketAddress, QueryData> servers;

                    pmq.SetReadStream(&data);
                    pmq.SetServers(&servers);
                    pmq.Read();

                    cout << "Received query data about " << servers.size() << " servers" << endl;

                    for (const auto& serv : servers)
                        cout << serv.second.GetName() << endl;

                    break;
                }
                case ID_MASTER_UPDATE:
                {
                    pair<PacketAddress, QueryData> serverPair;
                    PacketMasterUpdate pmu;
                    pmu.SetReadStream(&data);
                    pmu.SetServer(&serverPair);
                    pmu.Read();
                    cout << "Received info about " << packetAddressToString(serverPair.first, true) << endl;
                    cout << serverPair.second.GetName() << endl;
                    break;
                }
                default:
                    cout << "Wrong packet" << endl;
            }
        }
    }

    transport.closeConnection(masterAddr, true);
    BasePacket::SetPacketTransport(nullptr);
}
