#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Master/MasterData.hpp>
#include <components/openmw-mp/Packets/System/PacketSystemHandshake.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace mwmp;

    using Clock = std::chrono::steady_clock;
    constexpr unsigned char channelPayloadMessageId = ID_CHAT_MESSAGE;

    void sleepForNetworkPoll()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    struct ChannelPayload
    {
        unsigned char channel;
        unsigned char sequence;
    };

    struct TrafficSpec
    {
        PacketId messageId;
        unsigned char channel;
        PacketPriority priority;
    };

    struct MixedTrafficPayload
    {
        PacketId messageId;
        unsigned char channel;
        std::uint16_t sequence;
    };

    constexpr std::array<TrafficSpec, CHANNEL_WORLDSTATE + 1> mixedTrafficSpecs = { {
        { ID_SYSTEM_HANDSHAKE, CHANNEL_SYSTEM, PacketPriority::Immediate },
        { ID_ACTOR_POSITION, CHANNEL_ACTOR, PacketPriority::High },
        { ID_PLAYER_POSITION, CHANNEL_MOVEMENT, PacketPriority::High },
        { ID_PLAYER_INVENTORY, CHANNEL_PLAYER, PacketPriority::High },
        { ID_OBJECT_PLACE, CHANNEL_OBJECT, PacketPriority::High },
        { ID_CHAT_MESSAGE, CHANNEL_MASTER, PacketPriority::High },
        { ID_WORLD_TIME, CHANNEL_WORLDSTATE, PacketPriority::High },
    } };

    std::vector<PacketId> receivePacketIds(mwmp::GnsTransport& transport)
    {
        std::vector<PacketId> result;
        for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
             transport.deallocatePacket(receivedPacket), receivedPacket = transport.receive())
        {
            if (receivedPacket->length() > 0)
                result.push_back(receivedPacket->id());
        }
        return result;
    }

    bool containsPacket(const std::vector<PacketId>& packets, PacketId id)
    {
        return std::find(packets.begin(), packets.end(), id) != packets.end();
    }

    bool waitForPacketIds(mwmp::GnsTransport& transport, std::vector<PacketId> expected,
        Clock::duration timeout = std::chrono::seconds(5))
    {
        const auto start = Clock::now();

        while (!expected.empty() && Clock::now() - start < timeout)
        {
            const auto packets = receivePacketIds(transport);
            for (PacketId id : packets)
            {
                const auto expectedIt = std::find(expected.begin(), expected.end(), id);
                if (expectedIt != expected.end())
                    expected.erase(expectedIt);
            }

            if (!expected.empty())
                sleepForNetworkPoll();
        }

        return expected.empty();
    }

    std::unique_ptr<mwmp::GnsTransport> startLocalServer(unsigned int maxConnections,
        const std::string& address = "127.0.0.1")
    {
        const auto seed = static_cast<unsigned int>(Clock::now().time_since_epoch().count());
        const auto start = Clock::now();

        for (unsigned int attempt = 0; Clock::now() - start < std::chrono::seconds(3); ++attempt)
        {
            const auto port = static_cast<unsigned short>(35000 + ((seed + attempt) % 20000));
            auto server = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Server);

            try
            {
                server->startupServer(address, port, maxConnections);
                return server;
            }
            catch (const std::exception&)
            {
                sleepForNetworkPoll();
            }
        }

        return nullptr;
    }

    template <class Predicate>
    bool waitForTransportState(mwmp::GnsTransport& server, mwmp::GnsTransport& client, Predicate predicate)
    {
        const auto start = Clock::now();
        std::vector<PacketId> serverPackets;
        std::vector<PacketId> clientPackets;

        while (Clock::now() - start < std::chrono::seconds(5))
        {
            const auto newServerPackets = receivePacketIds(server);
            serverPackets.insert(serverPackets.end(), newServerPackets.begin(), newServerPackets.end());

            const auto newClientPackets = receivePacketIds(client);
            clientPackets.insert(clientPackets.end(), newClientPackets.begin(), newClientPackets.end());

            if (predicate(serverPackets, clientPackets))
                return true;

            sleepForNetworkPoll();
        }

        return predicate(serverPackets, clientPackets);
    }

    template <class Predicate>
    bool waitForMultiClientTransportState(mwmp::GnsTransport& server,
        const std::vector<std::unique_ptr<mwmp::GnsTransport>>& clients, Predicate predicate)
    {
        const auto start = Clock::now();
        std::vector<PacketId> serverPackets;
        std::vector<std::vector<PacketId>> clientPackets(clients.size());

        while (Clock::now() - start < std::chrono::seconds(5))
        {
            const auto newServerPackets = receivePacketIds(server);
            serverPackets.insert(serverPackets.end(), newServerPackets.begin(), newServerPackets.end());

            for (std::size_t clientIndex = 0; clientIndex < clients.size(); ++clientIndex)
            {
                const auto newClientPackets = receivePacketIds(*clients[clientIndex]);
                clientPackets[clientIndex].insert(clientPackets[clientIndex].end(), newClientPackets.begin(),
                    newClientPackets.end());
            }

            if (predicate(serverPackets, clientPackets))
                return true;

            sleepForNetworkPoll();
        }

        return predicate(serverPackets, clientPackets);
    }

    void connectToServer(mwmp::GnsTransport& server, mwmp::GnsTransport& client,
        const std::string& host = "127.0.0.1")
    {
        ASSERT_NE(server.port(), 0);
        ASSERT_NO_THROW(client.connect(host, server.port()));
    }

    void establishConnection(mwmp::GnsTransport& server, mwmp::GnsTransport& client,
        const std::string& host = "127.0.0.1")
    {
        connectToServer(server, client, host);

        ASSERT_TRUE(waitForTransportState(server, client, [](const auto& serverPackets, const auto& clientPackets) {
            return containsPacket(serverPackets, ID_NEW_INCOMING_CONNECTION)
                && containsPacket(clientPackets, ID_CONNECTION_REQUEST_ACCEPTED);
        }));
    }

    void writePreInit(PacketStream& stream, mwmp::PacketPreInit::PluginContainer& checksums,
        PacketGuid guid)
    {
        mwmp::PacketPreInit packet;
        packet.setChecksums(&checksums);
        packet.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        packet.setGUID(guid);
        packet.Packet(&stream, true);
    }

    ReceivedPacket* waitForPacket(mwmp::GnsTransport& transport, PacketId packetId)
    {
        const auto start = Clock::now();
        while (Clock::now() - start < std::chrono::seconds(5))
        {
            for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
                 receivedPacket = transport.receive())
            {
                if (receivedPacket->length() > 0 && receivedPacket->id() == packetId)
                    return receivedPacket;

                transport.deallocatePacket(receivedPacket);
            }

            sleepForNetworkPoll();
        }

        return nullptr;
    }

    PacketAddress sendClientPreInitAndGetAddress(mwmp::GnsTransport& server, mwmp::GnsTransport& client,
        PacketGuid guid)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { "Morrowind.esm", { 0x7B6AF5B9 } },
            { "Tribunal.esm", { 0xF481F334 } },
        };
        PacketStream clientStream;
        writePreInit(clientStream, sentChecksums, guid);

        EXPECT_EQ(client.send(clientStream.data(), clientStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_SYSTEM, unassignedPacketAddress(), false),
            1);

        ReceivedPacket* receivedServerPacket = waitForPacket(server, ID_GAME_PREINIT);
        EXPECT_NE(receivedServerPacket, nullptr);
        if (receivedServerPacket == nullptr)
            return unassignedPacketAddress();

        EXPECT_EQ(receivedServerPacket->guid(), guid);
        const PacketAddress clientAddress = receivedServerPacket->address();
        server.deallocatePacket(receivedServerPacket);
        return clientAddress;
    }

    PacketAddress sendClientPreInitAndGetAddress(mwmp::GnsTransport& server, mwmp::GnsTransport& client)
    {
        return sendClientPreInitAndGetAddress(server, client, client.getMyGuid());
    }

    void writePacketIdAndGuid(PacketStream& stream, PacketId packetId, PacketGuid guid)
    {
        stream.Write(static_cast<unsigned char>(packetId));
        writePacketGuid(stream, guid);
    }

    void sendChannelPayloads(mwmp::GnsTransport& transport, const mwmp::PacketDestination& destination)
    {
        for (unsigned char sequence = 0; sequence < 2; ++sequence)
        {
            for (unsigned char channel = CHANNEL_SYSTEM; channel <= CHANNEL_WORLDSTATE; ++channel)
            {
                const unsigned char payload[] = { channelPayloadMessageId, channel, sequence };
                ASSERT_EQ(transport.send(payload, sizeof(payload), PacketPriority::High, PacketReliability::ReliableOrderedWithAckReceipt,
                              static_cast<int8_t>(channel), destination, false),
                    1)
                    << "channel " << static_cast<int>(channel) << " sequence " << static_cast<int>(sequence);
            }
        }
    }

    std::vector<ChannelPayload> receiveChannelPayloads(mwmp::GnsTransport& transport, std::size_t expectedCount,
        Clock::duration timeout = std::chrono::seconds(5))
    {
        std::vector<ChannelPayload> result;
        const auto start = Clock::now();

        while (result.size() < expectedCount && Clock::now() - start < timeout)
        {
            for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
                 receivedPacket = transport.receive())
            {
                if (receivedPacket->length() == 3 && receivedPacket->id() == channelPayloadMessageId)
                    result.push_back({ receivedPacket->data()[1], receivedPacket->data()[2] });

                transport.deallocatePacket(receivedPacket);
            }

            if (result.size() < expectedCount)
                sleepForNetworkPoll();
        }

        return result;
    }

    void expectChannelSequences(const std::vector<ChannelPayload>& payloads)
    {
        ASSERT_EQ(payloads.size(), static_cast<std::size_t>((CHANNEL_WORLDSTATE + 1) * 2));

        std::vector<std::vector<unsigned char>> sequencesByChannel(CHANNEL_WORLDSTATE + 1);
        for (const ChannelPayload& payload : payloads)
        {
            ASSERT_LE(payload.channel, static_cast<unsigned char>(CHANNEL_WORLDSTATE));
            sequencesByChannel[payload.channel].push_back(payload.sequence);
        }

        for (unsigned char channel = CHANNEL_SYSTEM; channel <= CHANNEL_WORLDSTATE; ++channel)
            EXPECT_EQ(sequencesByChannel[channel], std::vector<unsigned char>({ 0, 1 }))
                << "channel " << static_cast<int>(channel);
    }

    void writeMixedTrafficPayload(const TrafficSpec& spec, std::uint16_t sequence, std::vector<unsigned char>& payload)
    {
        payload.assign(192 + (sequence % 64), 0);
        payload[0] = spec.messageId;
        payload[1] = spec.channel;
        payload[2] = static_cast<unsigned char>(sequence & 0xff);
        payload[3] = static_cast<unsigned char>((sequence >> 8) & 0xff);

        for (std::size_t index = 4; index < payload.size(); ++index)
            payload[index] = static_cast<unsigned char>((sequence + spec.channel + index) & 0xff);
    }

    bool isMixedTrafficMessage(PacketId messageId)
    {
        return std::any_of(mixedTrafficSpecs.begin(), mixedTrafficSpecs.end(),
            [messageId](const TrafficSpec& spec) { return spec.messageId == messageId; });
    }

    void sendMixedGameplayPayloads(mwmp::GnsTransport& transport, const mwmp::PacketDestination& destination,
        std::uint16_t sequenceCount)
    {
        std::vector<unsigned char> payload;
        for (std::uint16_t sequence = 0; sequence < sequenceCount; ++sequence)
        {
            for (const TrafficSpec& spec : mixedTrafficSpecs)
            {
                writeMixedTrafficPayload(spec, sequence, payload);
                ASSERT_EQ(transport.send(payload.data(), payload.size(), spec.priority, PacketReliability::ReliableOrderedWithAckReceipt,
                              static_cast<int8_t>(spec.channel), destination, false),
                    1)
                    << "channel " << static_cast<int>(spec.channel) << " sequence " << sequence;
            }
        }
    }

    std::vector<MixedTrafficPayload> receiveMixedGameplayPayloads(mwmp::GnsTransport& transport,
        std::size_t expectedCount, Clock::duration timeout = std::chrono::seconds(10))
    {
        std::vector<MixedTrafficPayload> result;
        const auto start = Clock::now();

        while (result.size() < expectedCount && Clock::now() - start < timeout)
        {
            for (ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
                 receivedPacket = transport.receive())
            {
                if (receivedPacket->length() >= 4 && isMixedTrafficMessage(receivedPacket->id()))
                {
                    const std::uint16_t sequence = static_cast<std::uint16_t>(
                        receivedPacket->data()[2] | (static_cast<std::uint16_t>(receivedPacket->data()[3]) << 8));
                    result.push_back({ receivedPacket->id(), receivedPacket->data()[1], sequence });

                    for (unsigned int index = 4; index < receivedPacket->length(); ++index)
                    {
                        EXPECT_EQ(receivedPacket->data()[index],
                            static_cast<unsigned char>((sequence + receivedPacket->data()[1] + index) & 0xff))
                            << "payload byte " << index << " channel " << static_cast<int>(receivedPacket->data()[1])
                            << " sequence " << sequence;
                    }
                }

                transport.deallocatePacket(receivedPacket);
            }

            if (result.size() < expectedCount)
                sleepForNetworkPoll();
        }

        return result;
    }

    void expectMixedGameplaySequences(const std::vector<MixedTrafficPayload>& payloads, std::uint16_t sequenceCount)
    {
        ASSERT_EQ(payloads.size(), mixedTrafficSpecs.size() * sequenceCount);

        std::vector<std::vector<std::uint16_t>> sequencesByChannel(CHANNEL_WORLDSTATE + 1);
        std::vector<PacketId> messageIdsByChannel(CHANNEL_WORLDSTATE + 1, 0);
        for (const MixedTrafficPayload& payload : payloads)
        {
            ASSERT_LE(payload.channel, static_cast<unsigned char>(CHANNEL_WORLDSTATE));
            sequencesByChannel[payload.channel].push_back(payload.sequence);
            messageIdsByChannel[payload.channel] = payload.messageId;
        }

        std::vector<std::uint16_t> expectedSequences;
        for (std::uint16_t sequence = 0; sequence < sequenceCount; ++sequence)
            expectedSequences.push_back(sequence);

        for (const TrafficSpec& spec : mixedTrafficSpecs)
        {
            EXPECT_EQ(messageIdsByChannel[spec.channel], spec.messageId)
                << "channel " << static_cast<int>(spec.channel);
            EXPECT_EQ(sequencesByChannel[spec.channel], expectedSequences)
                << "channel " << static_cast<int>(spec.channel);
        }
    }

    TEST(GnsTransportTest, acceptsLocalClientConnection)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, acceptsLocalhostHostnameConnection)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client, "localhost");

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, acceptsIpv6LoopbackConnectionWhenAvailable)
    {
        auto server = startLocalServer(1, "::1");
        if (server == nullptr)
            GTEST_SKIP() << "IPv6 loopback is not available for GameNetworkingSockets on this host";

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client, "::1");

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, reportsIpv6BannedAddressRejectionWhenAvailable)
    {
        auto server = startLocalServer(1, "::1");
        if (server == nullptr)
            GTEST_SKIP() << "IPv6 loopback is not available for GameNetworkingSockets on this host";

        server->banAddress("::1");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        connectToServer(*server, client, "::1");

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, reportsServerFullRejection)
    {
        auto server = startLocalServer(0);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        connectToServer(*server, client);

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_NO_FREE_INCOMING_CONNECTIONS);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, reportsBannedAddressRejection)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);
        server->banAddress("127.0.0.1");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        connectToServer(*server, client);

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, reportsHostnameBannedAddressRejection)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);
        server->banAddress("localhost");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        connectToServer(*server, client);

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, acceptsConnectionAfterAddressIsUnbanned)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);
        server->banAddress("127.0.0.1");
        server->unbanAddress("127.0.0.1");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, acceptsConnectionAfterHostnameAddressIsUnbanned)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);
        server->banAddress("localhost");
        server->unbanAddress("localhost");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, acceptsConnectionAfterIpv6AddressIsUnbannedWhenAvailable)
    {
        auto server = startLocalServer(1, "::1");
        if (server == nullptr)
            GTEST_SKIP() << "IPv6 loopback is not available for GameNetworkingSockets on this host";

        server->banAddress("::1");
        server->unbanAddress("::1");

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client, "::1");

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, disconnectsActiveConnectionWhenAddressIsBanned)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        server->banAddress("127.0.0.1");

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, disconnectsActiveConnectionWhenHostnameAddressIsBanned)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        server->banAddress("localhost");

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, disconnectsActiveConnectionWhenIpv6AddressIsBannedWhenAvailable)
    {
        auto server = startLocalServer(1, "::1");
        if (server == nullptr)
            GTEST_SKIP() << "IPv6 loopback is not available for GameNetworkingSockets on this host";

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client, "::1");

        server->banAddress("::1");

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_CONNECTION_BANNED);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, closesConnectionByGuidForServerKick)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketGuid clientGuid = client.getMyGuid();
        const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(clientAddress, unassignedPacketAddress());
        EXPECT_EQ(server->getPacketAddress(clientGuid), clientAddress);

        server->closeConnection(mwmp::PacketDestination(clientGuid), true);

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(clientPackets, ID_CONNECTION_LOST);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_EQ(server->getPacketAddress(clientGuid), unassignedPacketAddress());
        EXPECT_FALSE(client.isConnected());
    }

    TEST(GnsTransportTest, reportsLegacyAdminConnectionMetrics)
    {
        auto server = startLocalServer(2);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketGuid clientGuid = client.getMyGuid();
        const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(clientAddress, unassignedPacketAddress());

        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_EQ(server->maxConnections(), 2);
        EXPECT_EQ(server->getPacketAddress(clientGuid), clientAddress);
        EXPECT_GE(server->averagePing(mwmp::PacketDestination(clientGuid)), 0);
        EXPECT_GE(server->averagePing(clientAddress), 0);

        server->closeConnection(mwmp::PacketDestination(clientGuid), true);
        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(clientPackets, ID_CONNECTION_LOST);
        }));

        EXPECT_EQ(server->averagePing(mwmp::PacketDestination(clientGuid)), 0);
        EXPECT_EQ(server->getPacketAddress(clientGuid), unassignedPacketAddress());
    }

    TEST(GnsTransportTest, exchangesPreInitPacketWithEmbeddedGuid)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { "Morrowind.esm", { 0x7B6AF5B9 } },
            { "Tribunal.esm", { 0xF481F334 } },
        };
        PacketStream clientStream;
        writePreInit(clientStream, sentChecksums, client.getMyGuid());

        ASSERT_EQ(client.send(clientStream.data(), clientStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_SYSTEM, unassignedPacketAddress(), false),
            1);

        ReceivedPacket* receivedServerPacket = waitForPacket(*server, ID_GAME_PREINIT);
        ASSERT_NE(receivedServerPacket, nullptr);
        EXPECT_EQ(receivedServerPacket->id(), ID_GAME_PREINIT);
        EXPECT_EQ(receivedServerPacket->guid(), client.getMyGuid());

        PacketStream serverRead(receivedServerPacket->data(), receivedServerPacket->length());
        unsigned char packetId = 0;
        ASSERT_TRUE(serverRead.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        serverRead.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums;
        mwmp::PacketPreInit serverPreInit;
        serverPreInit.setChecksums(&receivedChecksums);
        serverPreInit.Packet(&serverRead, false);

        ASSERT_TRUE(serverPreInit.isPacketValid());
        EXPECT_EQ(serverPreInit.getVersion(), "0.1.0");
        EXPECT_EQ(serverPreInit.getProtocolVersion(), 10);
        EXPECT_EQ(serverPreInit.getCommitHash(), "abcdef1234567890");
        ASSERT_EQ(receivedChecksums.size(), 2);
        EXPECT_EQ(receivedChecksums[0].first, "Morrowind.esm");
        ASSERT_EQ(receivedChecksums[0].second.size(), 1);
        EXPECT_EQ(receivedChecksums[0].second[0], 0x7B6AF5B9);
        EXPECT_EQ(receivedChecksums[1].first, "Tribunal.esm");
        ASSERT_EQ(receivedChecksums[1].second.size(), 1);
        EXPECT_EQ(receivedChecksums[1].second[0], 0xF481F334);

        const PacketAddress clientAddress = receivedServerPacket->address();
        server->deallocatePacket(receivedServerPacket);

        mwmp::PacketPreInit::PluginContainer acceptedChecksums;
        PacketStream serverStream;
        writePreInit(serverStream, acceptedChecksums, server->getMyGuid());
        ASSERT_EQ(server->send(serverStream.data(), serverStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_SYSTEM, clientAddress, false),
            1);

        ReceivedPacket* receivedClientPacket = nullptr;
        const auto responseStart = Clock::now();
        while (receivedClientPacket == nullptr && Clock::now() - responseStart < std::chrono::seconds(5))
        {
            receivedClientPacket = client.receive();
            if (receivedClientPacket == nullptr)
                sleepForNetworkPoll();
        }

        ASSERT_NE(receivedClientPacket, nullptr);
        EXPECT_EQ(receivedClientPacket->id(), ID_GAME_PREINIT);

        PacketStream clientRead(receivedClientPacket->data(), receivedClientPacket->length());
        ASSERT_TRUE(clientRead.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        clientRead.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer responseChecksums{ { "stale.esm", { 1 } } };
        mwmp::PacketPreInit clientPreInit;
        clientPreInit.setChecksums(&responseChecksums);
        clientPreInit.Packet(&clientRead, false);

        EXPECT_TRUE(clientPreInit.isPacketValid());
        EXPECT_TRUE(responseChecksums.empty());
        client.deallocatePacket(receivedClientPacket);
    }

    TEST(GnsTransportTest, exchangesSystemHandshakeAfterPreInit)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(clientAddress, unassignedPacketAddress());

        PacketStream requestStream;
        requestStream.Write(static_cast<unsigned char>(ID_SYSTEM_HANDSHAKE));
        writePacketGuid(requestStream, client.getMyGuid());

        ASSERT_EQ(server->send(requestStream.data(), requestStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrdered, CHANNEL_SYSTEM, clientAddress, false),
            1);

        ReceivedPacket* receivedRequestPacket = waitForPacket(client, ID_SYSTEM_HANDSHAKE);
        ASSERT_NE(receivedRequestPacket, nullptr);
        PacketStream clientRequestRead(receivedRequestPacket->data(), receivedRequestPacket->length());
        unsigned char requestPacketId = 0;
        PacketGuid requestedGuid;
        ASSERT_TRUE(clientRequestRead.Read(requestPacketId));
        ASSERT_TRUE(readPacketGuid(clientRequestRead, requestedGuid));
        EXPECT_EQ(requestPacketId, ID_SYSTEM_HANDSHAKE);
        EXPECT_EQ(requestedGuid, client.getMyGuid());
        client.deallocatePacket(receivedRequestPacket);

        mwmp::BaseSystem sentSystem(client.getMyGuid());
        sentSystem.playerName = "alex";
        sentSystem.serverPassword = "secret";
        sentSystem.accountPasswordHash = "client-password-hash";

        PacketStream responseStream;
        mwmp::PacketSystemHandshake response;
        response.setSystem(&sentSystem);
        response.Packet(&responseStream, true);

        ASSERT_EQ(client.send(responseStream.data(), responseStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_SYSTEM, unassignedPacketAddress(), false),
            1);

        ReceivedPacket* receivedResponsePacket = waitForPacket(*server, ID_SYSTEM_HANDSHAKE);
        ASSERT_NE(receivedResponsePacket, nullptr);
        EXPECT_EQ(receivedResponsePacket->guid(), client.getMyGuid());

        PacketStream serverRead(receivedResponsePacket->data(), receivedResponsePacket->length());
        unsigned char packetId = 0;
        ASSERT_TRUE(serverRead.Read(packetId));
        EXPECT_EQ(packetId, ID_SYSTEM_HANDSHAKE);
        serverRead.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseSystem receivedSystem(client.getMyGuid());
        mwmp::PacketSystemHandshake received;
        received.setSystem(&receivedSystem);
        received.Packet(&serverRead, false);

        EXPECT_TRUE(received.isPacketValid());
        EXPECT_EQ(receivedSystem.playerName, "alex");
        EXPECT_EQ(receivedSystem.serverPassword, "secret");
        EXPECT_EQ(receivedSystem.accountPasswordHash, "client-password-hash");
        server->deallocatePacket(receivedResponsePacket);
    }

    TEST(GnsTransportTest, wrongFirstGameplayPacketCanBeClosedBeforePreInit)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        PacketStream stalePacketStream;
        writePacketIdAndGuid(stalePacketStream, ID_PLAYER_BASEINFO, client.getMyGuid());

        ASSERT_EQ(client.send(stalePacketStream.data(), stalePacketStream.size(), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_PLAYER, unassignedPacketAddress(), false),
            1);

        ReceivedPacket* receivedStalePacket = waitForPacket(*server, ID_PLAYER_BASEINFO);
        ASSERT_NE(receivedStalePacket, nullptr);
        EXPECT_EQ(receivedStalePacket->guid(), client.getMyGuid());

        server->closeConnection(PacketDestination(receivedStalePacket->guid()), true);
        server->deallocatePacket(receivedStalePacket);

        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto& serverPackets, const auto& clientPackets) {
            return containsPacket(serverPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(serverPackets, ID_CONNECTION_LOST)
                || containsPacket(clientPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(clientPackets, ID_CONNECTION_LOST);
        }));

        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());
        EXPECT_EQ(server->getPacketAddress(client.getMyGuid()), unassignedPacketAddress());
    }

    TEST(GnsTransportTest, deliversAllOrderingChannels)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(clientAddress, unassignedPacketAddress());

        sendChannelPayloads(client, unassignedPacketAddress());
        expectChannelSequences(receiveChannelPayloads(*server, (CHANNEL_WORLDSTATE + 1) * 2));

        sendChannelPayloads(*server, clientAddress);
        expectChannelSequences(receiveChannelPayloads(client, (CHANNEL_WORLDSTATE + 1) * 2));
    }

    TEST(GnsTransportTest, sustainsMixedHighLoadTrafficAcrossOrderingLanes)
    {
        constexpr std::uint16_t sequenceCount = 48;
        constexpr std::size_t expectedPayloadCount = mixedTrafficSpecs.size() * sequenceCount;

        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(clientAddress, unassignedPacketAddress());

        sendMixedGameplayPayloads(client, unassignedPacketAddress(), sequenceCount);
        expectMixedGameplaySequences(receiveMixedGameplayPayloads(*server, expectedPayloadCount), sequenceCount);

        sendMixedGameplayPayloads(*server, clientAddress, sequenceCount);
        expectMixedGameplaySequences(receiveMixedGameplayPayloads(client, expectedPayloadCount), sequenceCount);
    }

    TEST(GnsTransportTest, routesMultipleClientsByGuidAddressAndBroadcast)
    {
        auto server = startLocalServer(3);
        ASSERT_NE(server, nullptr);

        std::vector<std::unique_ptr<mwmp::GnsTransport>> clients;
        std::vector<PacketGuid> clientGuids;
        std::vector<PacketAddress> clientAddresses;

        for (int index = 0; index < 3; ++index)
        {
            clients.push_back(std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client));
            establishConnection(*server, *clients.back());

            const PacketGuid clientGuid = clients.back()->getMyGuid();
            const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, *clients.back());

            ASSERT_NE(clientAddress, unassignedPacketAddress());
            EXPECT_EQ(server->getPacketAddress(clientGuid), clientAddress);
            clientGuids.push_back(clientGuid);
            clientAddresses.push_back(clientAddress);
        }

        ASSERT_EQ(server->numberOfConnections(), 3);
        for (const auto& client : clients)
            ASSERT_TRUE(client->isConnected());

        const unsigned char directGuidPayload[] = { channelPayloadMessageId, CHANNEL_PLAYER, 10 };
        ASSERT_EQ(server->send(directGuidPayload, sizeof(directGuidPayload), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_PLAYER, mwmp::PacketDestination(clientGuids[0]), false),
            1);

        const std::vector<ChannelPayload> directGuidPayloads = receiveChannelPayloads(*clients[0], 1);
        ASSERT_EQ(directGuidPayloads.size(), 1);
        EXPECT_EQ(directGuidPayloads[0].channel, CHANNEL_PLAYER);
        EXPECT_EQ(directGuidPayloads[0].sequence, 10);
        EXPECT_TRUE(receiveChannelPayloads(*clients[1], 1, std::chrono::milliseconds(250)).empty());
        EXPECT_TRUE(receiveChannelPayloads(*clients[2], 1, std::chrono::milliseconds(250)).empty());

        const unsigned char directAddressPayload[] = { channelPayloadMessageId, CHANNEL_OBJECT, 20 };
        ASSERT_EQ(server->send(directAddressPayload, sizeof(directAddressPayload), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_OBJECT, clientAddresses[2], false),
            1);

        const std::vector<ChannelPayload> directAddressPayloads = receiveChannelPayloads(*clients[2], 1);
        ASSERT_EQ(directAddressPayloads.size(), 1);
        EXPECT_EQ(directAddressPayloads[0].channel, CHANNEL_OBJECT);
        EXPECT_EQ(directAddressPayloads[0].sequence, 20);

        const unsigned char broadcastPayload[] = { channelPayloadMessageId, CHANNEL_MASTER, 30 };
        ASSERT_EQ(server->send(broadcastPayload, sizeof(broadcastPayload), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, mwmp::PacketDestination(clientGuids[1]), true),
            2);

        const std::vector<ChannelPayload> clientZeroBroadcastPayloads = receiveChannelPayloads(*clients[0], 1);
        ASSERT_EQ(clientZeroBroadcastPayloads.size(), 1);
        EXPECT_EQ(clientZeroBroadcastPayloads[0].channel, CHANNEL_MASTER);
        EXPECT_EQ(clientZeroBroadcastPayloads[0].sequence, 30);

        const std::vector<ChannelPayload> clientTwoBroadcastPayloads = receiveChannelPayloads(*clients[2], 1);
        ASSERT_EQ(clientTwoBroadcastPayloads.size(), 1);
        EXPECT_EQ(clientTwoBroadcastPayloads[0].channel, CHANNEL_MASTER);
        EXPECT_EQ(clientTwoBroadcastPayloads[0].sequence, 30);

        EXPECT_TRUE(receiveChannelPayloads(*clients[1], 1, std::chrono::milliseconds(250)).empty());
    }

    TEST(GnsTransportTest, handlesRapidClientConnectDisconnectChurnAndReplacementRouting)
    {
        constexpr int waveCount = 4;
        constexpr int clientCount = 4;

        auto server = startLocalServer(clientCount);
        ASSERT_NE(server, nullptr);

        for (int wave = 0; wave < waveCount; ++wave)
        {
            std::vector<std::unique_ptr<mwmp::GnsTransport>> clients;
            std::vector<PacketGuid> clientGuids;
            std::vector<PacketAddress> clientAddresses;

            for (int index = 0; index < clientCount; ++index)
            {
                clients.push_back(std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client));
                establishConnection(*server, *clients.back());

                const PacketGuid clientGuid = clients.back()->getMyGuid();
                const PacketAddress clientAddress = sendClientPreInitAndGetAddress(*server, *clients.back());

                ASSERT_NE(clientAddress, unassignedPacketAddress());
                EXPECT_EQ(server->getPacketAddress(clientGuid), clientAddress);
                clientGuids.push_back(clientGuid);
                clientAddresses.push_back(clientAddress);
            }

            ASSERT_EQ(server->numberOfConnections(), clientCount);
            for (int index = 0; index < clientCount; ++index)
            {
                ASSERT_TRUE(clients[index]->isConnected());

                const unsigned char payload[] = { channelPayloadMessageId, CHANNEL_PLAYER,
                    static_cast<unsigned char>(wave * clientCount + index) };
                ASSERT_EQ(clients[index]->send(payload, sizeof(payload), PacketPriority::High,
                              PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_PLAYER,
                              unassignedPacketAddress(), false),
                    1);
            }

            ASSERT_EQ(receiveChannelPayloads(*server, clientCount).size(), static_cast<std::size_t>(clientCount));

            server->closeConnection(clientAddresses[1], true);
            server->closeConnection(clientAddresses[3], true);

            ASSERT_TRUE(waitForMultiClientTransportState(*server, clients, [&](const auto&, const auto& clientPackets) {
                return !clients[1]->isConnected() && !clients[3]->isConnected()
                    && (containsPacket(clientPackets[1], ID_DISCONNECTION_NOTIFICATION)
                        || containsPacket(clientPackets[1], ID_CONNECTION_LOST))
                    && (containsPacket(clientPackets[3], ID_DISCONNECTION_NOTIFICATION)
                        || containsPacket(clientPackets[3], ID_CONNECTION_LOST));
            }));

            EXPECT_EQ(server->numberOfConnections(), 2);
            EXPECT_EQ(server->getPacketAddress(clientGuids[1]), unassignedPacketAddress());
            EXPECT_EQ(server->getPacketAddress(clientGuids[3]), unassignedPacketAddress());
            EXPECT_EQ(server->getPacketAddress(clientGuids[0]), clientAddresses[0]);
            EXPECT_EQ(server->getPacketAddress(clientGuids[2]), clientAddresses[2]);

            for (int replacementIndex : { 1, 3 })
            {
                clients[replacementIndex] = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client);
                establishConnection(*server, *clients[replacementIndex]);

                clientGuids[replacementIndex] = clients[replacementIndex]->getMyGuid();
                clientAddresses[replacementIndex] = sendClientPreInitAndGetAddress(*server, *clients[replacementIndex]);

                ASSERT_NE(clientAddresses[replacementIndex], unassignedPacketAddress());
                EXPECT_EQ(server->getPacketAddress(clientGuids[replacementIndex]), clientAddresses[replacementIndex]);
            }

            ASSERT_EQ(server->numberOfConnections(), clientCount);

            const unsigned char guidPayload[] = { channelPayloadMessageId, CHANNEL_OBJECT,
                static_cast<unsigned char>(80 + wave) };
            ASSERT_EQ(server->send(guidPayload, sizeof(guidPayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_OBJECT,
                          mwmp::PacketDestination(clientGuids[1]), false),
                1);
            const std::vector<ChannelPayload> guidPayloads = receiveChannelPayloads(*clients[1], 1);
            ASSERT_EQ(guidPayloads.size(), 1);
            EXPECT_EQ(guidPayloads[0].sequence, static_cast<unsigned char>(80 + wave));

            const unsigned char addressPayload[] = { channelPayloadMessageId, CHANNEL_WORLDSTATE,
                static_cast<unsigned char>(90 + wave) };
            ASSERT_EQ(server->send(addressPayload, sizeof(addressPayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_WORLDSTATE, clientAddresses[2], false),
                1);
            const std::vector<ChannelPayload> addressPayloads = receiveChannelPayloads(*clients[2], 1);
            ASSERT_EQ(addressPayloads.size(), 1);
            EXPECT_EQ(addressPayloads[0].sequence, static_cast<unsigned char>(90 + wave));

            for (int index = 0; index < clientCount; ++index)
                server->closeConnection(clientAddresses[index], true);

            ASSERT_TRUE(waitForMultiClientTransportState(*server, clients, [&](const auto&, const auto&) {
                return std::all_of(clients.begin(), clients.end(),
                    [](const auto& client) { return !client->isConnected(); });
            }));
            EXPECT_EQ(server->numberOfConnections(), 0);
        }
    }

    TEST(GnsTransportTest, sustainsGameplayChurnWhileMasterBrowserRefreshes)
    {
        constexpr int gameplayClientCount = 3;
        constexpr int browserClientCount = 2;
        constexpr int refreshCount = 4;

        auto gameplayServer = startLocalServer(gameplayClientCount);
        ASSERT_NE(gameplayServer, nullptr);
        auto masterServer = startLocalServer(browserClientCount);
        ASSERT_NE(masterServer, nullptr);

        std::vector<std::unique_ptr<mwmp::GnsTransport>> browserClients;
        for (int index = 0; index < browserClientCount; ++index)
        {
            browserClients.push_back(std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client));
            establishConnection(*masterServer, *browserClients.back());
        }
        ASSERT_EQ(masterServer->numberOfConnections(), browserClientCount);

        std::vector<std::unique_ptr<mwmp::GnsTransport>> gameplayClients;
        std::vector<PacketGuid> gameplayGuids;
        std::vector<PacketAddress> gameplayAddresses;

        for (int index = 0; index < gameplayClientCount; ++index)
        {
            gameplayClients.push_back(std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client));
            establishConnection(*gameplayServer, *gameplayClients.back());

            gameplayGuids.push_back(gameplayClients.back()->getMyGuid());
            gameplayAddresses.push_back(sendClientPreInitAndGetAddress(*gameplayServer, *gameplayClients.back()));

            ASSERT_NE(gameplayAddresses.back(), unassignedPacketAddress());
            EXPECT_EQ(gameplayServer->getPacketAddress(gameplayGuids.back()), gameplayAddresses.back());
        }
        ASSERT_EQ(gameplayServer->numberOfConnections(), gameplayClientCount);

        for (int refresh = 0; refresh < refreshCount; ++refresh)
        {
            for (int index = 0; index < gameplayClientCount; ++index)
            {
                const unsigned char gameplayPayload[] = { channelPayloadMessageId, CHANNEL_PLAYER,
                    static_cast<unsigned char>(refresh * gameplayClientCount + index) };
                ASSERT_EQ(gameplayClients[index]->send(gameplayPayload, sizeof(gameplayPayload), PacketPriority::High,
                              PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_PLAYER, unassignedPacketAddress(), false),
                    1);
            }
            ASSERT_EQ(receiveChannelPayloads(*gameplayServer, gameplayClientCount).size(),
                static_cast<std::size_t>(gameplayClientCount));

            const unsigned char masterQueryPayload[] = { ID_MASTER_QUERY, CHANNEL_MASTER,
                static_cast<unsigned char>(refresh) };
            const unsigned char masterUpdatePayload[] = { ID_MASTER_UPDATE, CHANNEL_MASTER,
                static_cast<unsigned char>(refresh) };

            ASSERT_EQ(browserClients[0]->send(masterQueryPayload, sizeof(masterQueryPayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, unassignedPacketAddress(), false),
                1);
            ASSERT_EQ(browserClients[1]->send(masterUpdatePayload, sizeof(masterUpdatePayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, unassignedPacketAddress(), false),
                1);

            ASSERT_TRUE(waitForPacketIds(*masterServer, { ID_MASTER_QUERY, ID_MASTER_UPDATE }));

            ASSERT_EQ(masterServer->send(masterQueryPayload, sizeof(masterQueryPayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, PacketDestination(), true),
                static_cast<uint32_t>(browserClientCount));
            ASSERT_EQ(masterServer->send(masterUpdatePayload, sizeof(masterUpdatePayload), PacketPriority::High,
                          PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, PacketDestination(), true),
                static_cast<uint32_t>(browserClientCount));

            for (const auto& browserClient : browserClients)
                ASSERT_TRUE(waitForPacketIds(*browserClient, { ID_MASTER_QUERY, ID_MASTER_UPDATE }));

            const int replacementIndex = refresh % gameplayClientCount;
            const PacketGuid replacedGuid = gameplayGuids[replacementIndex];
            gameplayServer->closeConnection(gameplayAddresses[replacementIndex], true);

            ASSERT_TRUE(waitForMultiClientTransportState(*gameplayServer, gameplayClients,
                [&](const auto&, const auto& clientPackets) {
                    return !gameplayClients[replacementIndex]->isConnected()
                        && (containsPacket(clientPackets[replacementIndex], ID_DISCONNECTION_NOTIFICATION)
                            || containsPacket(clientPackets[replacementIndex], ID_CONNECTION_LOST));
                }));

            EXPECT_EQ(gameplayServer->getPacketAddress(replacedGuid), unassignedPacketAddress());
            EXPECT_EQ(gameplayServer->numberOfConnections(), gameplayClientCount - 1);

            gameplayClients[replacementIndex] = std::make_unique<mwmp::GnsTransport>(mwmp::GnsMode::Client);
            establishConnection(*gameplayServer, *gameplayClients[replacementIndex]);
            gameplayGuids[replacementIndex] = gameplayClients[replacementIndex]->getMyGuid();
            gameplayAddresses[replacementIndex]
                = sendClientPreInitAndGetAddress(*gameplayServer, *gameplayClients[replacementIndex]);

            ASSERT_NE(gameplayAddresses[replacementIndex], unassignedPacketAddress());
            EXPECT_EQ(gameplayServer->getPacketAddress(gameplayGuids[replacementIndex]),
                gameplayAddresses[replacementIndex]);
            EXPECT_EQ(gameplayServer->numberOfConnections(), gameplayClientCount);
            EXPECT_EQ(masterServer->numberOfConnections(), browserClientCount);
        }
    }

    TEST(GnsTransportTest, routesCallbacksForConcurrentTransports)
    {
        auto serverA = startLocalServer(1);
        ASSERT_NE(serverA, nullptr);
        auto serverB = startLocalServer(1);
        ASSERT_NE(serverB, nullptr);

        mwmp::GnsTransport clientA(mwmp::GnsMode::Client);
        mwmp::GnsTransport clientB(mwmp::GnsMode::Client);

        connectToServer(*serverA, clientA);
        connectToServer(*serverB, clientB);

        std::vector<PacketId> serverAPackets;
        std::vector<PacketId> clientAPackets;
        std::vector<PacketId> serverBPackets;
        std::vector<PacketId> clientBPackets;

        const auto start = Clock::now();
        while (Clock::now() - start < std::chrono::seconds(5))
        {
            const auto newServerAPackets = receivePacketIds(*serverA);
            serverAPackets.insert(serverAPackets.end(), newServerAPackets.begin(), newServerAPackets.end());
            const auto newClientAPackets = receivePacketIds(clientA);
            clientAPackets.insert(clientAPackets.end(), newClientAPackets.begin(), newClientAPackets.end());
            const auto newServerBPackets = receivePacketIds(*serverB);
            serverBPackets.insert(serverBPackets.end(), newServerBPackets.begin(), newServerBPackets.end());
            const auto newClientBPackets = receivePacketIds(clientB);
            clientBPackets.insert(clientBPackets.end(), newClientBPackets.begin(), newClientBPackets.end());

            if (containsPacket(serverAPackets, ID_NEW_INCOMING_CONNECTION)
                && containsPacket(clientAPackets, ID_CONNECTION_REQUEST_ACCEPTED)
                && containsPacket(serverBPackets, ID_NEW_INCOMING_CONNECTION)
                && containsPacket(clientBPackets, ID_CONNECTION_REQUEST_ACCEPTED))
                break;

            sleepForNetworkPoll();
        }

        EXPECT_TRUE(containsPacket(serverAPackets, ID_NEW_INCOMING_CONNECTION));
        EXPECT_TRUE(containsPacket(clientAPackets, ID_CONNECTION_REQUEST_ACCEPTED));
        EXPECT_TRUE(containsPacket(serverBPackets, ID_NEW_INCOMING_CONNECTION));
        EXPECT_TRUE(containsPacket(clientBPackets, ID_CONNECTION_REQUEST_ACCEPTED));
        EXPECT_EQ(serverA->numberOfConnections(), 1);
        EXPECT_EQ(serverB->numberOfConnections(), 1);
        EXPECT_TRUE(clientA.isConnected());
        EXPECT_TRUE(clientB.isConnected());

        const unsigned char payloadA[] = { channelPayloadMessageId, CHANNEL_MASTER, 42 };
        ASSERT_EQ(clientA.send(payloadA, sizeof(payloadA), PacketPriority::High, PacketReliability::ReliableOrderedWithAckReceipt,
                      CHANNEL_MASTER, unassignedPacketAddress(), false),
            1);

        const unsigned char payloadB[] = { channelPayloadMessageId, CHANNEL_WORLDSTATE, 84 };
        ASSERT_EQ(clientB.send(payloadB, sizeof(payloadB), PacketPriority::High, PacketReliability::ReliableOrderedWithAckReceipt,
                      CHANNEL_WORLDSTATE, unassignedPacketAddress(), false),
            1);

        const std::vector<ChannelPayload> payloadsA = receiveChannelPayloads(*serverA, 1);
        ASSERT_EQ(payloadsA.size(), 1);
        EXPECT_EQ(payloadsA[0].channel, CHANNEL_MASTER);
        EXPECT_EQ(payloadsA[0].sequence, 42);

        const std::vector<ChannelPayload> payloadsB = receiveChannelPayloads(*serverB, 1);
        ASSERT_EQ(payloadsB.size(), 1);
        EXPECT_EQ(payloadsB[0].channel, CHANNEL_WORLDSTATE);
        EXPECT_EQ(payloadsB[0].sequence, 84);
    }

    TEST(GnsTransportTest, reconnectsAfterClientInitiatedClose)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketGuid clientGuid = client.getMyGuid();
        const PacketAddress firstAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(firstAddress, unassignedPacketAddress());
        EXPECT_EQ(server->getPacketAddress(clientGuid), firstAddress);

        client.closeConnection(unassignedPacketAddress(), true);
        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto& serverPackets, const auto& clientPackets) {
            return containsPacket(serverPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(serverPackets, ID_CONNECTION_LOST)
                || containsPacket(clientPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(clientPackets, ID_CONNECTION_LOST);
        }));
        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());

        establishConnection(*server, client);

        const PacketAddress secondAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(secondAddress, unassignedPacketAddress());
        EXPECT_EQ(client.getMyGuid(), clientGuid);
        EXPECT_EQ(server->getPacketAddress(clientGuid), secondAddress);
        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, preservesClientGuidAcrossReconnect)
    {
        auto server = startLocalServer(1);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport client(mwmp::GnsMode::Client);
        establishConnection(*server, client);

        const PacketGuid clientGuid = client.getMyGuid();
        const PacketAddress firstAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(firstAddress, unassignedPacketAddress());
        EXPECT_EQ(server->getPacketAddress(clientGuid), firstAddress);
        EXPECT_EQ(server->numberOfConnections(), 1);

        server->closeConnection(firstAddress, true);
        ASSERT_TRUE(waitForTransportState(*server, client, [](const auto&, const auto& clientPackets) {
            return containsPacket(clientPackets, ID_DISCONNECTION_NOTIFICATION)
                || containsPacket(clientPackets, ID_CONNECTION_LOST);
        }));
        EXPECT_EQ(server->numberOfConnections(), 0);
        EXPECT_FALSE(client.isConnected());

        establishConnection(*server, client);

        const PacketAddress secondAddress = sendClientPreInitAndGetAddress(*server, client);
        ASSERT_NE(secondAddress, unassignedPacketAddress());
        EXPECT_EQ(client.getMyGuid(), clientGuid);
        EXPECT_EQ(server->getPacketAddress(clientGuid), secondAddress);
        EXPECT_EQ(server->numberOfConnections(), 1);
        EXPECT_TRUE(client.isConnected());
    }

    TEST(GnsTransportTest, duplicateEmbeddedClientGuidReplacesOldConnection)
    {
        auto server = startLocalServer(2);
        ASSERT_NE(server, nullptr);

        mwmp::GnsTransport clientA(mwmp::GnsMode::Client);
        mwmp::GnsTransport clientB(mwmp::GnsMode::Client);
        establishConnection(*server, clientA);
        establishConnection(*server, clientB);
        EXPECT_EQ(server->numberOfConnections(), 2);

        const PacketGuid sharedGuid = clientA.getMyGuid();
        const PacketAddress firstAddress = sendClientPreInitAndGetAddress(*server, clientA, sharedGuid);
        ASSERT_NE(firstAddress, unassignedPacketAddress());
        EXPECT_EQ(server->getPacketAddress(sharedGuid), firstAddress);

        const PacketAddress secondAddress = sendClientPreInitAndGetAddress(*server, clientB, sharedGuid);
        ASSERT_NE(secondAddress, unassignedPacketAddress());
        EXPECT_NE(secondAddress, firstAddress);
        EXPECT_EQ(server->getPacketAddress(sharedGuid), secondAddress);
        EXPECT_EQ(server->numberOfConnections(), 1);

        const unsigned char broadcastPayload[] = { channelPayloadMessageId, CHANNEL_MASTER, 99 };
        EXPECT_EQ(server->send(broadcastPayload, sizeof(broadcastPayload), PacketPriority::High,
                      PacketReliability::ReliableOrderedWithAckReceipt, CHANNEL_MASTER, PacketDestination(), true),
            1);

        const std::vector<ChannelPayload> payloads = receiveChannelPayloads(clientB, 1);
        ASSERT_EQ(payloads.size(), 1);
        EXPECT_EQ(payloads[0].channel, CHANNEL_MASTER);
        EXPECT_EQ(payloads[0].sequence, 99);
    }
}
