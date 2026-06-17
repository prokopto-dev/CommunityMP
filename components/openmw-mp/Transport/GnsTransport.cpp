#include "GnsTransport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <steam/isteamnetworkingutils.h>

namespace
{
    int sInitCount = 0;
    std::mutex sInitMutex;
    std::mutex sCallbackOwnerMutex;
    std::mutex sCallbackPollMutex;
    std::unordered_map<HSteamNetConnection, mwmp::GnsTransport*> sConnectionOwners;
    std::unordered_map<HSteamListenSocket, mwmp::GnsTransport*> sListenSocketOwners;

    constexpr int connectionCloseReason = k_ESteamNetConnectionEnd_App_Generic;
    constexpr int serverFullCloseReason = k_ESteamNetConnectionEnd_AppException_Min + 1;
    constexpr int addressBannedCloseReason = k_ESteamNetConnectionEnd_AppException_Min + 2;
    constexpr int gnsLaneCount = CHANNEL_WORLDSTATE + 1;

    mwmp::PacketGuid readEmbeddedGuid(const unsigned char* data)
    {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < mwmp::packetGuidSize(); ++index)
            value = (value << 8) | data[index + 1];
        return mwmp::makePacketGuid(value);
    }

    uint16 laneForOrderChannel(int8_t orderChannel)
    {
        if (orderChannel < 0 || orderChannel >= gnsLaneCount)
            return 0;

        return static_cast<uint16>(orderChannel);
    }

    std::string makeEndpoint(const std::string& host, unsigned short port)
    {
        if (host.find(':') != std::string::npos && (host.empty() || host.front() != '['))
            return "[" + host + "]:" + std::to_string(port);

        return host + ":" + std::to_string(port);
    }

    std::string gaiErrorMessage(int error)
    {
#ifdef _WIN32
        return gai_strerrorA(error);
#else
        return gai_strerror(error);
#endif
    }

    mwmp::PacketId connectionEndPacket(const SteamNetConnectionStatusChangedCallback_t& callback,
        mwmp::PacketId fallback)
    {
        switch (callback.m_info.m_eEndReason)
        {
            case serverFullCloseReason:
                return ID_NO_FREE_INCOMING_CONNECTIONS;
            case addressBannedCloseReason:
                return ID_CONNECTION_BANNED;
            default:
                break;
        }

        if (callback.m_eOldState == k_ESteamNetworkingConnectionState_Connecting)
            return ID_CONNECTION_ATTEMPT_FAILED;

        return fallback;
    }
}

mwmp::GnsTransport::GnsTransport(GnsMode mode, bool useEmbeddedPacketGuid)
    : mMode(mode)
    , mSockets(nullptr)
    , mListenSocket(k_HSteamListenSocket_Invalid)
    , mPollGroup(k_HSteamNetPollGroup_Invalid)
    , mServerConnection(k_HSteamNetConnection_Invalid)
    , mMyGuid(makeLocalGuid(mode))
    , mMaxConnections(0)
    , mPort(0)
    , mConnected(false)
    , mUseEmbeddedPacketGuid(useEmbeddedPacketGuid)
{
    {
        std::lock_guard lock(sInitMutex);
        if (sInitCount++ == 0)
        {
            SteamDatagramErrMsg errMsg;
            if (!GameNetworkingSockets_Init(nullptr, errMsg))
            {
                --sInitCount;
                throw std::runtime_error(std::string("Could not initialize GameNetworkingSockets: ") + errMsg);
            }
        }
    }

    mSockets = SteamNetworkingSockets();
    {
        std::lock_guard lock(sInitMutex);
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&GnsTransport::connectionStatusChanged);
    }
}

mwmp::GnsTransport::~GnsTransport()
{
    std::lock_guard transportLock(mMutex);

    for (ReceivedPacket* packet : mQueuedPackets)
        deallocatePacket(packet);

    if (mSockets)
    {
        if (mServerConnection != k_HSteamNetConnection_Invalid)
        {
            mSockets->CloseConnection(mServerConnection, connectionCloseReason, "CommunityMP GNS transport shutting down", false);
            unregisterConnectionOwner(mServerConnection, this);
        }

        for (const auto& [connection, guid] : mConnectionGuids)
        {
            mSockets->CloseConnection(connection, connectionCloseReason, "CommunityMP GNS transport shutting down", false);
            unregisterConnectionOwner(connection, this);
        }

        if (mListenSocket != k_HSteamListenSocket_Invalid)
        {
            unregisterListenSocketOwner(mListenSocket, this);
            mSockets->CloseListenSocket(mListenSocket);
        }

        if (mPollGroup != k_HSteamNetPollGroup_Invalid)
            mSockets->DestroyPollGroup(mPollGroup);
    }

    {
        std::lock_guard lock(sInitMutex);
        if (--sInitCount == 0)
            GameNetworkingSockets_Kill();
    }
}

void mwmp::GnsTransport::startupServer(const std::string& address, unsigned short port, unsigned int maxConnections)
{
    std::lock_guard lock(mMutex);

    SteamNetworkingIPAddr listenAddress;
    listenAddress.Clear();
    listenAddress.m_port = port;

    if (!address.empty() && address != "0.0.0.0" && address != "::" && address != "[::]")
    {
        try
        {
            listenAddress = resolveAddress(address, port);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Could not parse GNS listen address: " + makeEndpoint(address, port) + " (" + e.what()
                + ")");
        }
    }

    mPollGroup = mSockets->CreatePollGroup();
    if (mPollGroup == k_HSteamNetPollGroup_Invalid)
        throw std::runtime_error("Could not create GNS poll group");

    mListenSocket = mSockets->CreateListenSocketIP(listenAddress, 0, nullptr);
    if (mListenSocket == k_HSteamListenSocket_Invalid)
        throw std::runtime_error("Could not create GNS listen socket");
    registerListenSocketOwner(mListenSocket, this);

    SteamNetworkingIPAddr boundAddress;
    if (mSockets->GetListenSocketAddress(mListenSocket, &boundAddress))
        port = boundAddress.m_port;

    mMaxConnections = maxConnections;
    mPort = port;
    mConnected = true;
}

void mwmp::GnsTransport::connect(const std::string& ip, unsigned short port)
{
    std::lock_guard lock(mMutex);

    SteamNetworkingIPAddr remoteAddress;
    try
    {
        remoteAddress = resolveAddress(ip, port);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Could not parse GNS server address: " + makeEndpoint(ip, port) + " (" + e.what() + ")");
    }

    mServerConnection = mSockets->ConnectByIPAddress(remoteAddress, 0, nullptr);
    if (mServerConnection == k_HSteamNetConnection_Invalid)
        throw std::runtime_error("Could not start GNS connection to server");

    registerConnectionOwner(mServerConnection, this);
    setConnectionAddress(mServerConnection, remoteAddress);
    mPort = port;
}

bool mwmp::GnsTransport::isConnected() const
{
    std::lock_guard lock(mMutex);

    return mConnected;
}

uint32_t mwmp::GnsTransport::send(const unsigned char* data, std::size_t length, PacketPriority priority,
    PacketReliability reliability, int8_t orderChannel, const PacketDestination& destination, bool broadcast)
{
    std::lock_guard lock(mMutex);

    if (data == nullptr || length == 0)
        return 0;

    if (mMode == GnsMode::Client)
        return sendToConnection(mServerConnection, data, length, priority, reliability, orderChannel);

    if (broadcast)
    {
        uint32_t sent = 0;
        for (const auto& [connection, guid] : mConnectionGuids)
        {
            const bool excludedByGuid = destination.hasGuid() && guid == destination.guid();
            const bool excludedByAddress = destination.hasAddress() && addressForConnection(connection) == destination.address();

            if (!excludedByGuid && !excludedByAddress)
                sent += sendToConnection(connection, data, length, priority, reliability, orderChannel);
        }
        return sent;
    }

    const HSteamNetConnection connection = resolveConnection(destination);
    if (connection == k_HSteamNetConnection_Invalid)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS could not resolve destination guid=%llu address=%s",
            static_cast<unsigned long long>(packetGuidValue(destination.guid())),
            packetAddressToString(destination.address(), true).c_str());
        return 0;
    }

    return sendToConnection(connection, data, length, priority, reliability, orderChannel);
}

mwmp::ReceivedPacket* mwmp::GnsTransport::receive()
{
    pollCallbacks();

    {
        std::lock_guard lock(mMutex);
        if (!mQueuedPackets.empty())
        {
            ReceivedPacket* packet = mQueuedPackets.front();
            mQueuedPackets.pop_front();
            return packet;
        }
    }

    GnsMode mode;
    HSteamNetPollGroup pollGroup;
    HSteamNetConnection serverConnection;
    {
        std::lock_guard lock(mMutex);
        mode = mMode;
        pollGroup = mPollGroup;
        serverConnection = mServerConnection;
    }

    SteamNetworkingMessage_t* messages[1] = {};
    int messageCount = 0;

    if (mode == GnsMode::Server && pollGroup != k_HSteamNetPollGroup_Invalid)
        messageCount = mSockets->ReceiveMessagesOnPollGroup(pollGroup, messages, 1);
    else if (mode == GnsMode::Client && serverConnection != k_HSteamNetConnection_Invalid)
        messageCount = mSockets->ReceiveMessagesOnConnection(serverConnection, messages, 1);

    if (messageCount <= 0)
        return nullptr;

    return makePacketFromMessage(messages[0]);
}

void mwmp::GnsTransport::deallocatePacket(ReceivedPacket* packet)
{
    delete packet;
}

void mwmp::GnsTransport::closeConnection(const PacketDestination& destination, bool sendNotification)
{
    std::lock_guard lock(mMutex);

    const HSteamNetConnection connection = resolveConnection(destination);
    if (connection != k_HSteamNetConnection_Invalid)
    {
        mSockets->CloseConnection(connection, connectionCloseReason, "CommunityMP closed connection", sendNotification);
        if (mMode == GnsMode::Client && connection == mServerConnection)
        {
            mConnected = false;
            mServerConnection = k_HSteamNetConnection_Invalid;
        }
        forgetConnection(connection);
    }
}

void mwmp::GnsTransport::banAddress(const char* ipAddress)
{
    if (ipAddress == nullptr || *ipAddress == '\0')
        return;

    const std::string normalizedAddress = normalizeAddressKey(ipAddress);
    {
        std::lock_guard lock(mMutex);
        mBannedAddresses.insert(normalizedAddress);
    }
    closeBannedConnections(normalizedAddress);
}

void mwmp::GnsTransport::unbanAddress(const char* ipAddress)
{
    if (ipAddress == nullptr || *ipAddress == '\0')
        return;

    const std::string normalizedAddress = normalizeAddressKey(ipAddress);
    std::lock_guard lock(mMutex);

    mBannedAddresses.erase(normalizedAddress);
}

mwmp::PacketAddress mwmp::GnsTransport::getPacketAddress(PacketGuid guid) const
{
    std::lock_guard lock(mMutex);

    const auto connectionIt = mGuidConnections.find(packetGuidValue(guid));
    if (connectionIt == mGuidConnections.end())
        return unassignedPacketAddress();

    return addressForConnection(connectionIt->second);
}

mwmp::PacketGuid mwmp::GnsTransport::getMyGuid() const
{
    std::lock_guard lock(mMutex);

    return mMyGuid;
}

unsigned short mwmp::GnsTransport::numberOfConnections() const
{
    std::lock_guard lock(mMutex);

    return static_cast<unsigned short>(mConnectionGuids.size());
}

unsigned int mwmp::GnsTransport::maxConnections() const
{
    std::lock_guard lock(mMutex);

    return mMaxConnections;
}

int mwmp::GnsTransport::averagePing(const PacketDestination& destination) const
{
    const HSteamNetConnection connection = resolveConnection(destination);
    if (connection == k_HSteamNetConnection_Invalid)
        return 0;

    SteamNetConnectionRealTimeStatus_t status;
    if (mSockets->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK)
        return 0;

    return std::max(status.m_nPing, 0);
}

unsigned short mwmp::GnsTransport::port() const
{
    std::lock_guard lock(mMutex);

    return mPort;
}

void mwmp::GnsTransport::connectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* callback)
{
    if (callback == nullptr)
        return;

    if (GnsTransport* owner = ownerForCallback(*callback))
        owner->handleConnectionStatusChanged(*callback);
}

void mwmp::GnsTransport::registerConnectionOwner(HSteamNetConnection connection, GnsTransport* owner)
{
    if (connection == k_HSteamNetConnection_Invalid || owner == nullptr)
        return;

    std::lock_guard lock(sCallbackOwnerMutex);
    sConnectionOwners[connection] = owner;
}

void mwmp::GnsTransport::unregisterConnectionOwner(HSteamNetConnection connection, const GnsTransport* owner)
{
    if (connection == k_HSteamNetConnection_Invalid)
        return;

    std::lock_guard lock(sCallbackOwnerMutex);
    const auto it = sConnectionOwners.find(connection);
    if (it != sConnectionOwners.end() && it->second == owner)
        sConnectionOwners.erase(it);
}

void mwmp::GnsTransport::registerListenSocketOwner(HSteamListenSocket socket, GnsTransport* owner)
{
    if (socket == k_HSteamListenSocket_Invalid || owner == nullptr)
        return;

    std::lock_guard lock(sCallbackOwnerMutex);
    sListenSocketOwners[socket] = owner;
}

void mwmp::GnsTransport::unregisterListenSocketOwner(HSteamListenSocket socket, const GnsTransport* owner)
{
    if (socket == k_HSteamListenSocket_Invalid)
        return;

    std::lock_guard lock(sCallbackOwnerMutex);
    const auto it = sListenSocketOwners.find(socket);
    if (it != sListenSocketOwners.end() && it->second == owner)
        sListenSocketOwners.erase(it);
}

mwmp::GnsTransport* mwmp::GnsTransport::ownerForCallback(const SteamNetConnectionStatusChangedCallback_t& callback)
{
    std::lock_guard lock(sCallbackOwnerMutex);

    const auto connectionIt = sConnectionOwners.find(callback.m_hConn);
    if (connectionIt != sConnectionOwners.end())
        return connectionIt->second;

    const auto listenSocketIt = sListenSocketOwners.find(callback.m_info.m_hListenSocket);
    if (listenSocketIt != sListenSocketOwners.end())
        return listenSocketIt->second;

    return nullptr;
}

void mwmp::GnsTransport::handleConnectionStatusChanged(const SteamNetConnectionStatusChangedCallback_t& callback)
{
    std::lock_guard lock(mMutex);

    const HSteamNetConnection connection = callback.m_hConn;
    setConnectionAddress(connection, callback.m_info.m_addrRemote);

    switch (callback.m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (mMode == GnsMode::Server)
            {
                if (isAddressBanned(callback.m_info.m_addrRemote))
                {
                    mSockets->CloseConnection(connection, addressBannedCloseReason, "CommunityMP address is banned", false);
                    forgetConnection(connection);
                    return;
                }

                if (mConnectionGuids.size() >= mMaxConnections)
                {
                    mSockets->CloseConnection(connection, serverFullCloseReason, "CommunityMP server is full", false);
                    forgetConnection(connection);
                    return;
                }

                if (mSockets->AcceptConnection(connection) == k_EResultOK)
                {
                    registerConnectionOwner(connection, this);
                    mSockets->SetConnectionPollGroup(connection, mPollGroup);
                    configureConnectionLanes(connection);
                    setConnectionGuid(connection, makeLocalGuid(GnsMode::Server));
                    queueSystemPacket(ID_NEW_INCOMING_CONNECTION, connection);
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            if (mMode == GnsMode::Client && connection == mServerConnection)
            {
                mConnected = true;
                configureConnectionLanes(connection);
                queueSystemPacket(ID_CONNECTION_REQUEST_ACCEPTED, connection);
            }
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            if (mMode == GnsMode::Client || mConnectionGuids.find(connection) != mConnectionGuids.end())
                queueSystemPacket(connectionEndPacket(callback, ID_DISCONNECTION_NOTIFICATION), connection);
            if (mMode == GnsMode::Client && connection == mServerConnection)
            {
                mConnected = false;
                mServerConnection = k_HSteamNetConnection_Invalid;
            }
            mSockets->CloseConnection(connection, 0, nullptr, false);
            forgetConnection(connection);
            break;

        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            if (mMode == GnsMode::Client || mConnectionGuids.find(connection) != mConnectionGuids.end())
                queueSystemPacket(connectionEndPacket(callback, ID_CONNECTION_LOST), connection);
            if (mMode == GnsMode::Client && connection == mServerConnection)
            {
                mConnected = false;
                mServerConnection = k_HSteamNetConnection_Invalid;
            }
            mSockets->CloseConnection(connection, 0, nullptr, false);
            forgetConnection(connection);
            break;

        default:
            break;
    }
}

void mwmp::GnsTransport::queueSystemPacket(PacketId id, HSteamNetConnection connection)
{
    std::lock_guard lock(mMutex);

    mQueuedPackets.push_back(makePacket(id, connection));
}

mwmp::ReceivedPacket* mwmp::GnsTransport::makePacket(PacketId id, HSteamNetConnection connection)
{
    std::lock_guard lock(mMutex);

    return new ReceivedPacket(std::vector<unsigned char>{ id }, guidForConnection(connection),
        addressForConnection(connection));
}

mwmp::ReceivedPacket* mwmp::GnsTransport::makePacketFromMessage(SteamNetworkingMessage_t* message)
{
    std::lock_guard lock(mMutex);

    PacketGuid guid = guidForConnection(message->m_conn);

    const auto data = static_cast<unsigned char*>(message->m_pData);

    if (mUseEmbeddedPacketGuid && data != nullptr
        && message->m_cbSize >= static_cast<int>(1 + packetGuidSize()))
    {
        const PacketGuid headerGuid = readEmbeddedGuid(data);

        if (mMode == GnsMode::Server && isPacketGuidAssigned(headerGuid))
        {
            setConnectionGuid(message->m_conn, headerGuid);
            guid = headerGuid;
        }
    }

    std::vector<unsigned char> packetData;
    if (data != nullptr && message->m_cbSize > 0)
        packetData.assign(data, data + message->m_cbSize);
    const PacketAddress address = addressForConnection(message->m_conn);

    message->Release();
    return new ReceivedPacket(std::move(packetData), guid, address);
}

mwmp::PacketGuid mwmp::GnsTransport::guidForConnection(HSteamNetConnection connection) const
{
    std::lock_guard lock(mMutex);

    const auto it = mConnectionGuids.find(connection);
    if (it != mConnectionGuids.end())
        return it->second;

    return unassignedPacketGuid();
}

mwmp::PacketAddress mwmp::GnsTransport::addressForConnection(HSteamNetConnection connection) const
{
    std::lock_guard lock(mMutex);

    const auto it = mConnectionAddresses.find(connection);
    if (it != mConnectionAddresses.end())
        return it->second;

    return unassignedPacketAddress();
}

void mwmp::GnsTransport::setConnectionGuid(HSteamNetConnection connection, PacketGuid guid)
{
    std::lock_guard lock(mMutex);

    const auto claimedGuid = mGuidConnections.find(packetGuidValue(guid));
    if (claimedGuid != mGuidConnections.end() && claimedGuid->second != connection)
    {
        const HSteamNetConnection previousConnection = claimedGuid->second;
        if (mSockets)
            mSockets->CloseConnection(previousConnection, connectionCloseReason, "CommunityMP duplicate client GUID", true);
        forgetConnection(previousConnection);
    }

    const auto existing = mConnectionGuids.find(connection);
    if (existing != mConnectionGuids.end())
        mGuidConnections.erase(packetGuidValue(existing->second));

    mConnectionGuids[connection] = guid;
    mGuidConnections[packetGuidValue(guid)] = connection;
}

bool mwmp::GnsTransport::configureConnectionLanes(HSteamNetConnection connection)
{
    std::lock_guard lock(mMutex);

    if (connection == k_HSteamNetConnection_Invalid)
        return false;

    if (mLaneConfiguredConnections.find(connection) != mLaneConfiguredConnections.end())
        return true;

    const EResult result = mSockets->ConfigureConnectionLanes(connection, gnsLaneCount, nullptr, nullptr);
    if (result != k_EResultOK)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS failed to configure %d ordering lanes on connection %u: result %d",
            gnsLaneCount, static_cast<unsigned int>(connection), static_cast<int>(result));
        return false;
    }

    mLaneConfiguredConnections.insert(connection);
    return true;
}

void mwmp::GnsTransport::setConnectionAddress(HSteamNetConnection connection, const SteamNetworkingIPAddr& address)
{
    std::lock_guard lock(mMutex);

    if (connection == k_HSteamNetConnection_Invalid || address.IsIPv6AllZeros())
        return;

    mConnectionIpAddressKeys[connection] = ipAddressKey(address);

    const PacketAddress systemAddress = toPacketAddress(address);
    if (!isPacketAddressAssigned(systemAddress))
        return;

    const auto existing = mConnectionAddresses.find(connection);
    if (existing != mConnectionAddresses.end())
        mAddressConnections.erase(addressKey(existing->second));

    mConnectionAddresses[connection] = systemAddress;
    mAddressConnections[addressKey(systemAddress)] = connection;
}

void mwmp::GnsTransport::forgetConnection(HSteamNetConnection connection)
{
    std::lock_guard lock(mMutex);

    unregisterConnectionOwner(connection, this);
    mLaneConfiguredConnections.erase(connection);

    const auto guidIt = mConnectionGuids.find(connection);
    if (guidIt != mConnectionGuids.end())
    {
        mGuidConnections.erase(packetGuidValue(guidIt->second));
        mConnectionGuids.erase(guidIt);
    }

    const auto addressIt = mConnectionAddresses.find(connection);
    if (addressIt != mConnectionAddresses.end())
    {
        mAddressConnections.erase(addressKey(addressIt->second));
        mConnectionAddresses.erase(addressIt);
    }

    mConnectionIpAddressKeys.erase(connection);
}

HSteamNetConnection mwmp::GnsTransport::resolveConnection(const PacketDestination& destination) const
{
    std::lock_guard lock(mMutex);

    if (mMode == GnsMode::Client)
        return mServerConnection;

    if (destination.hasGuid())
    {
        const auto guidIt = mGuidConnections.find(packetGuidValue(destination.guid()));
        if (guidIt != mGuidConnections.end())
            return guidIt->second;
    }

    if (destination.hasAddress())
    {
        const auto addressIt = mAddressConnections.find(addressKey(destination.address()));
        if (addressIt != mAddressConnections.end())
            return addressIt->second;
    }

    return k_HSteamNetConnection_Invalid;
}

bool mwmp::GnsTransport::isAddressBanned(const PacketAddress& address) const
{
    std::lock_guard lock(mMutex);

    if (!isPacketAddressAssigned(address))
        return false;

    return mBannedAddresses.find(ipAddressKey(address)) != mBannedAddresses.end();
}

bool mwmp::GnsTransport::isAddressBanned(const SteamNetworkingIPAddr& address) const
{
    std::lock_guard lock(mMutex);

    if (address.IsIPv6AllZeros())
        return false;

    return mBannedAddresses.find(ipAddressKey(address)) != mBannedAddresses.end();
}

void mwmp::GnsTransport::closeBannedConnections(const std::string& ipAddress)
{
    std::vector<HSteamNetConnection> bannedConnections;
    {
        std::lock_guard lock(mMutex);
        for (const auto& [connection, address] : mConnectionIpAddressKeys)
        {
            if (address == ipAddress)
                bannedConnections.push_back(connection);
        }
    }

    for (HSteamNetConnection connection : bannedConnections)
    {
        mSockets->CloseConnection(connection, addressBannedCloseReason, "CommunityMP address was banned", true);
        forgetConnection(connection);
    }
}

uint32_t mwmp::GnsTransport::sendToConnection(HSteamNetConnection connection, const unsigned char* data, std::size_t length,
    PacketPriority priority, PacketReliability reliability, int8_t orderChannel)
{
    if (connection == k_HSteamNetConnection_Invalid)
        return 0;

    if (length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS refused oversized %zu-byte message", length);
        return 0;
    }

    SteamNetworkingMessage_t* message = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(length));
    if (message == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS failed to allocate %zu-byte message", length);
        return 0;
    }

    std::memcpy(message->m_pData, data, length);
    message->m_conn = connection;
    message->m_nFlags = sendFlags(priority, reliability);
    message->m_idxLane = laneForOrderChannel(orderChannel);

    if (message->m_idxLane != 0 && !configureConnectionLanes(connection))
        message->m_idxLane = 0;

    SteamNetworkingMessage_t* messages[1] = { message };
    int64 messageResult = 0;
    mSockets->SendMessages(1, messages, &messageResult, true);

    if (messageResult <= 0)
    {
        const int result = messageResult < 0 ? static_cast<int>(-messageResult) : 0;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS failed to send %zu bytes to connection %u: result %d", length,
            static_cast<unsigned int>(connection), result);
        return 0;
    }

    if (shouldFlushImmediately(priority))
    {
        const EResult flushResult = mSockets->FlushMessagesOnConnection(connection);
        if (flushResult != k_EResultOK)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "GNS failed to flush messages to connection %u: result %d",
                static_cast<unsigned int>(connection), static_cast<int>(flushResult));
        }
    }

    return 1;
}

void mwmp::GnsTransport::pollCallbacks()
{
    std::lock_guard lock(sCallbackPollMutex);

    if (mSockets)
        mSockets->RunCallbacks();
}

SteamNetworkingIPAddr mwmp::GnsTransport::resolveAddress(const std::string& host, unsigned short port)
{
    SteamNetworkingIPAddr address;
    const std::string endpoint = makeEndpoint(host, port);
    if (address.ParseString(endpoint.c_str()))
        return address;

    if (host == "localhost")
    {
        address.Clear();
        address.SetIPv4(0x7f000001, port);
        return address;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    const std::string service = std::to_string(port);
    const int error = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
    if (error != 0)
        throw std::runtime_error(gaiErrorMessage(error));

    for (const addrinfo* item = result; item != nullptr; item = item->ai_next)
    {
        if (item->ai_family == AF_INET)
        {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
            address.Clear();
            address.SetIPv4(ntohl(ipv4->sin_addr.s_addr), port);
            freeaddrinfo(result);
            return address;
        }

        if (item->ai_family == AF_INET6)
        {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(item->ai_addr);
            address.Clear();
            address.SetIPv6(ipv6->sin6_addr.s6_addr, port);
            freeaddrinfo(result);
            return address;
        }
    }

    freeaddrinfo(result);
    throw std::runtime_error("host did not resolve to an IPv4 or IPv6 address");
}

mwmp::PacketAddress mwmp::GnsTransport::toPacketAddress(const SteamNetworkingIPAddr& address)
{
    char host[SteamNetworkingIPAddr::k_cchMaxString] = {};
    address.ToString(host, sizeof(host), false);

    std::string hostText(host);
    if (hostText.size() >= 2 && hostText.front() == '[' && hostText.back() == ']')
        hostText = hostText.substr(1, hostText.size() - 2);

    return makePacketAddress(hostText.c_str(), address.m_port);
}

std::string mwmp::GnsTransport::normalizeAddressKey(const std::string& address)
{
    try
    {
        return ipAddressKey(resolveAddress(address, 0));
    }
    catch (const std::exception&)
    {
    }

    return address;
}

std::string mwmp::GnsTransport::ipAddressKey(const SteamNetworkingIPAddr& address)
{
    char host[SteamNetworkingIPAddr::k_cchMaxString] = {};
    address.ToString(host, sizeof(host), false);

    std::string hostText(host);
    if (hostText.size() >= 2 && hostText.front() == '[' && hostText.back() == ']')
        hostText = hostText.substr(1, hostText.size() - 2);

    return hostText;
}

std::string mwmp::GnsTransport::ipAddressKey(const PacketAddress& address)
{
    return packetAddressToString(address, false);
}

std::string mwmp::GnsTransport::addressKey(const PacketAddress& address)
{
    return packetAddressToString(address, true, '|');
}

bool mwmp::GnsTransport::isUnreliable(PacketReliability reliability)
{
    switch (reliability)
    {
        case PacketReliability::Unreliable:
        case PacketReliability::UnreliableSequenced:
        case PacketReliability::UnreliableWithAckReceipt:
            return true;

        default:
            return false;
    }
}

bool mwmp::GnsTransport::shouldBypassNagle(PacketPriority priority)
{
    switch (priority)
    {
        case PacketPriority::Immediate:
        case PacketPriority::High:
            return true;

        default:
            return false;
    }
}

bool mwmp::GnsTransport::shouldFlushImmediately(PacketPriority priority)
{
    return priority == PacketPriority::Immediate;
}

int mwmp::GnsTransport::sendFlags(PacketPriority priority, PacketReliability reliability)
{
    if (isUnreliable(reliability))
    {
        if (shouldBypassNagle(priority))
            return k_nSteamNetworkingSend_UnreliableNoNagle;

        return k_nSteamNetworkingSend_Unreliable;
    }

    if (shouldBypassNagle(priority))
        return k_nSteamNetworkingSend_ReliableNoNagle;

    return k_nSteamNetworkingSend_Reliable;
}

mwmp::PacketGuid mwmp::GnsTransport::makeLocalGuid(GnsMode mode)
{
    static std::random_device randomDevice;

    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto randomHigh = static_cast<std::uint64_t>(randomDevice()) << 32;
    const auto randomLow = static_cast<std::uint64_t>(randomDevice());

    std::uint64_t value = randomHigh ^ randomLow ^ now;
    value ^= mode == GnsMode::Server ? 0x474e535300000000ULL : 0x474e534300000000ULL;

    if (value == 0 || value == packetGuidValue(unassignedPacketGuid()))
        value ^= 0x0100000000000001ULL;

    return makePacketGuid(value);
}
