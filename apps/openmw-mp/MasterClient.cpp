#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include "MasterClient.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Master/PacketMasterAnnounce.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include "Networking.hpp"

using namespace mwmp;

MasterClient::MasterClient(std::string queryAddr, unsigned short queryPort) :
        masterHost(std::move(queryAddr)), masterPort(queryPort), masterServer(makePacketAddress(masterHost.c_str(), masterPort)),
        pma()
{
    timeout = 15000; // every 15 seconds
    mRun.store(false);
    pma.SetSendStream(&writeStream);
    pma.SetServer(&queryData);
    updated = true;
}

void MasterClient::SetPlayers(unsigned pl)
{
    std::lock_guard lock(mutexData);
    const int players = static_cast<int>(pl);
    if (queryData.GetPlayers() != players)
    {
        queryData.SetPlayers(players);
        updated = true;
    }
}

void MasterClient::SetMaxPlayers(unsigned pl)
{
    std::lock_guard lock(mutexData);
    const int players = static_cast<int>(pl);
    if (queryData.GetMaxPlayers() != players)
    {
        queryData.SetMaxPlayers(players);
        updated = true;
    }
}

void MasterClient::SetHostname(std::string hostname)
{
    std::lock_guard lock(mutexData);
    std::string substr = hostname.substr(0, 200);
    if (queryData.GetName() != substr)
    {
        queryData.SetName(substr.c_str());
        updated = true;
    }
}

void MasterClient::SetModname(std::string modname)
{
    std::lock_guard lock(mutexData);
    std::string substr = modname.substr(0, 200);
    if (queryData.GetGameMode() != substr)
    {
        queryData.SetGameMode(substr.c_str());
        updated = true;
    }
}

void MasterClient::SetRuleString(std::string key, std::string value)
{
    std::lock_guard lock(mutexData);
    if (queryData.rules.find(key) == queryData.rules.end() || queryData.rules[key].type != 's'
        || queryData.rules[key].str != value)
    {
        ServerRule rule;
        rule.str = value;
        rule.type = ServerRule::Type::string;
        queryData.rules[key] = rule;
        updated = true;
    }
}

void MasterClient::SetRuleValue(std::string key, double value)
{
    std::lock_guard lock(mutexData);
    if (queryData.rules.find(key) == queryData.rules.end() || queryData.rules[key].type != 'v'
        || queryData.rules[key].val != value)
    {
        ServerRule rule;
        rule.val = value;
        rule.type = ServerRule::Type::number;
        queryData.rules[key] = rule;
        updated = true;
    }
}

void MasterClient::PushPlugin(Plugin plugin)
{
    std::lock_guard lock(mutexData);
    queryData.plugins.push_back(plugin);
    updated = true;
}

bool MasterClient::Process(ReceivedPacket* packet)
{
    return ProcessPacket(packet, true);
}

bool MasterClient::ProcessPacket(ReceivedPacket* packet, bool requireMasterAddress)
{
    if (!mRun.load())
        return false;

    if (requireMasterAddress && (!isPacketAddressAssigned(masterServer) || packet->address() != masterServer))
        return false;

    PacketStream rs(packet->data(), packet->length());
    unsigned char pid;
    rs.Read(pid);
    switch (pid)
    {
        case ID_SND_RECEIPT_ACKED:
        case ID_CONNECTION_ATTEMPT_FAILED:
        case ID_CONNECTION_REQUEST_ACCEPTED:
        case ID_DISCONNECTION_NOTIFICATION:
            break;
        case ID_MASTER_QUERY:
            break;
        case ID_MASTER_ANNOUNCE:
        {
            int func = PacketMasterAnnounce::FUNCTION_KEEP;
            {
                std::lock_guard lock(mutexPacket);
                pma.SetReadStream(&rs);
                pma.Read();
                func = pma.GetFunc();
            }

            if (func == PacketMasterAnnounce::FUNCTION_KEEP)
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Server data successfully updated on master server");
            else if (func == PacketMasterAnnounce::FUNCTION_DELETE)
            {
                if (timeout.load() != 0)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Update rate is too low,"
                            " and the master server has deleted information about the server. Trying low rate...");
                    if ((timeout.load() - step_rate) >= step_rate)
                        SetUpdateRate(timeout.load() - step_rate);
                    std::lock_guard lock(mutexData);
                    updated = true;
                }
            }
            break;
        }
        default:
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received wrong packet from master server with id: %d",
                packet->id());
            return false;
    }
    return true;
}

void MasterClient::Send(mwmp::PacketMasterAnnounce::Func func, const QueryData& data)
{
    QueryData packetData = data;

    try
    {
        if (!masterTransport)
            masterTransport = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client, false);
        else
            masterTransport->closeConnection(masterServer, false);

        masterTransport->connect(masterHost, masterPort);

        bool connected = false;
        auto waitStart = std::chrono::steady_clock::now();
        while (!connected && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(5))
        {
            for (ReceivedPacket* receivedPacket = masterTransport->receive(); receivedPacket;
                 masterTransport->deallocatePacket(receivedPacket), receivedPacket = masterTransport->receive())
            {
                if (receivedPacket->id() == ID_CONNECTION_REQUEST_ACCEPTED)
                    connected = true;
                else
                    ProcessPacket(receivedPacket, false);
            }

            if (!connected)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!connected)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cannot connect to master server: %s:%hu", masterHost.c_str(),
                masterPort);
            masterTransport->closeConnection(masterServer, false);
            return;
        }

        {
            std::lock_guard lock(mutexPacket);
            writeStream.Reset();
            pma.SetServer(&packetData);
            pma.SetFunc(func);
            pma.SetAdvertisedPort(Networking::get().getPort());
            pma.Packet(&writeStream, true);

            if (masterTransport->send(writeStream.data(), writeStream.size(), PacketPriority::High,
                    PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, masterServer, false)
                == 0)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Could not send master server update to %s:%hu",
                    masterHost.c_str(), masterPort);
                masterTransport->closeConnection(masterServer, false);
                return;
            }
        }

        bool gotAnswer = false;
        waitStart = std::chrono::steady_clock::now();
        while (!gotAnswer && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(5))
        {
            for (ReceivedPacket* receivedPacket = masterTransport->receive(); receivedPacket;
                 masterTransport->deallocatePacket(receivedPacket), receivedPacket = masterTransport->receive())
            {
                if (receivedPacket->id() == ID_MASTER_ANNOUNCE)
                    gotAnswer = true;
                ProcessPacket(receivedPacket, false);
            }

            if (!gotAnswer)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!gotAnswer)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Master server did not acknowledge update from %s:%hu",
                masterHost.c_str(), masterPort);

        masterTransport->closeConnection(masterServer, false);
    }
    catch (const std::exception& e)
    {
        if (masterTransport)
            masterTransport->closeConnection(masterServer, false);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Cannot update master server %s:%hu: %s", masterHost.c_str(),
            masterPort, e.what());
    }
}

void MasterClient::Thread()
{
    {
        std::lock_guard lock(mutexData);
        queryData.SetPassword((int) Networking::get().isPassworded());
        queryData.SetVersion(TES3MP_VERSION);
    }

    while (mRun.load())
    {
        const auto [playerCount, playerNames] = Players::getMasterListSnapshot();
        QueryData snapshot;
        bool shouldAnnounce = false;

        {
            std::lock_guard lock(mutexData);
            if (queryData.GetPlayers() != static_cast<int>(playerCount))
            {
                queryData.SetPlayers(static_cast<int>(playerCount));
                updated = true;
            }

            if (queryData.players != playerNames)
            {
                queryData.players = playerNames;
                updated = true;
            }

            shouldAnnounce = updated;
            updated = false;
            snapshot = queryData;
        }

        Send(shouldAnnounce ? PacketMasterAnnounce::FUNCTION_ANNOUNCE : PacketMasterAnnounce::FUNCTION_KEEP, snapshot);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout.load()));
    }
}

void MasterClient::Start()
{
    bool expected = false;
    if (!mRun.compare_exchange_strong(expected, true))
        return;

    thrQuery = std::thread(&MasterClient::Thread, this);
}

void MasterClient::Stop()
{
    if (!mRun.exchange(false))
        return;

    if (thrQuery.joinable())
        thrQuery.join();

    QueryData snapshot;
    {
        std::lock_guard lock(mutexData);
        snapshot = queryData;
    }
    Send(PacketMasterAnnounce::FUNCTION_DELETE, snapshot);
}

void MasterClient::SetUpdateRate(unsigned int rate)
{
    if (rate < min_rate)
        rate = min_rate;
    else if (rate > max_rate)
        rate = max_rate;
    timeout.store(rate);
}
