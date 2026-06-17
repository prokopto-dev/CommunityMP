#ifndef OPENMW_MP_GNSTRANSPORT_HPP
#define OPENMW_MP_GNSTRANSPORT_HPP

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <string>
#include <unordered_map>

#include <steam/steamnetworkingsockets.h>

#include "PacketId.hpp"
#include "PacketTransport.hpp"

namespace mwmp
{
    enum class GnsMode
    {
        Client,
        Server
    };

    class GnsTransport final : public PacketTransport
    {
    public:
        explicit GnsTransport(GnsMode mode, bool useEmbeddedPacketGuid = true);
        ~GnsTransport() override;

        void startupServer(const std::string& address, unsigned short port, unsigned int maxConnections);
        void connect(const std::string& ip, unsigned short port);
        bool isConnected() const;

        uint32_t send(const unsigned char* data, std::size_t length, PacketPriority priority,
            PacketReliability reliability, int8_t orderChannel, const PacketDestination& destination, bool broadcast) override;

        ReceivedPacket* receive() override;
        void deallocatePacket(ReceivedPacket* packet) override;
        void closeConnection(const PacketDestination& destination, bool sendNotification) override;
        void banAddress(const char* ipAddress) override;
        void unbanAddress(const char* ipAddress) override;

        PacketAddress getPacketAddress(PacketGuid guid) const override;
        PacketGuid getMyGuid() const override;
        unsigned short numberOfConnections() const override;
        unsigned int maxConnections() const override;
        int averagePing(const PacketDestination& destination) const override;
        unsigned short port() const override;

    private:
        static void connectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* callback);
        static void registerConnectionOwner(HSteamNetConnection connection, GnsTransport* owner);
        static void unregisterConnectionOwner(HSteamNetConnection connection, const GnsTransport* owner);
        static void registerListenSocketOwner(HSteamListenSocket socket, GnsTransport* owner);
        static void unregisterListenSocketOwner(HSteamListenSocket socket, const GnsTransport* owner);
        static GnsTransport* ownerForCallback(const SteamNetConnectionStatusChangedCallback_t& callback);

        void handleConnectionStatusChanged(const SteamNetConnectionStatusChangedCallback_t& callback);
        void queueSystemPacket(PacketId id, HSteamNetConnection connection);
        ReceivedPacket* makePacket(PacketId id, HSteamNetConnection connection);
        ReceivedPacket* makePacketFromMessage(SteamNetworkingMessage_t* message);

        PacketGuid guidForConnection(HSteamNetConnection connection) const;
        PacketAddress addressForConnection(HSteamNetConnection connection) const;
        bool configureConnectionLanes(HSteamNetConnection connection);
        void setConnectionGuid(HSteamNetConnection connection, PacketGuid guid);
        void setConnectionAddress(HSteamNetConnection connection, const SteamNetworkingIPAddr& address);
        void forgetConnection(HSteamNetConnection connection);
        HSteamNetConnection resolveConnection(const PacketDestination& destination) const;
        bool isAddressBanned(const PacketAddress& address) const;
        bool isAddressBanned(const SteamNetworkingIPAddr& address) const;
        void closeBannedConnections(const std::string& ipAddress);

        uint32_t sendToConnection(HSteamNetConnection connection, const unsigned char* data, std::size_t length,
            PacketPriority priority, PacketReliability reliability, int8_t orderChannel);
        void pollCallbacks();

        static SteamNetworkingIPAddr resolveAddress(const std::string& host, unsigned short port);
        static PacketAddress toPacketAddress(const SteamNetworkingIPAddr& address);
        static std::string normalizeAddressKey(const std::string& address);
        static std::string ipAddressKey(const SteamNetworkingIPAddr& address);
        static std::string ipAddressKey(const PacketAddress& address);
        static std::string addressKey(const PacketAddress& address);
        static bool isUnreliable(PacketReliability reliability);
        static bool shouldBypassNagle(PacketPriority priority);
        static bool shouldFlushImmediately(PacketPriority priority);
        static int sendFlags(PacketPriority priority, PacketReliability reliability);
        static PacketGuid makeLocalGuid(GnsMode mode);

        GnsMode mMode;
        ISteamNetworkingSockets* mSockets;
        HSteamListenSocket mListenSocket;
        HSteamNetPollGroup mPollGroup;
        HSteamNetConnection mServerConnection;
        PacketGuid mMyGuid;
        unsigned int mMaxConnections;
        unsigned short mPort;
        bool mConnected;
        bool mUseEmbeddedPacketGuid;

        mutable std::recursive_mutex mMutex;
        std::unordered_map<HSteamNetConnection, PacketGuid> mConnectionGuids;
        std::unordered_map<std::uint64_t, HSteamNetConnection> mGuidConnections;
        std::unordered_map<HSteamNetConnection, PacketAddress> mConnectionAddresses;
        std::unordered_map<std::string, HSteamNetConnection> mAddressConnections;
        std::unordered_map<HSteamNetConnection, std::string> mConnectionIpAddressKeys;
        std::unordered_set<HSteamNetConnection> mLaneConfiguredConnections;
        std::unordered_set<std::string> mBannedAddresses;
        std::deque<ReceivedPacket*> mQueuedPackets;
    };
}

#endif
