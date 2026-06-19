#include <components/esm/refid.hpp>
#include <components/openmw-mp/Base/ActorStatsAuthority.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Master/PacketMasterAnnounce.hpp>
#include <components/openmw-mp/Master/PacketMasterQuery.hpp>
#include <components/openmw-mp/Master/PacketMasterUpdate.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAI.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimFlags.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimPlay.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttack.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCast.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCellChange.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorDeath.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorEquipment.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPosition.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorSpellsActive.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorStatsDynamic.hpp>
#include <components/openmw-mp/Packets/Object/PacketConsoleCommand.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectActivate.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectHit.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectSound.hpp>
#include <components/openmw-mp/Packets/Object/PacketClientScriptLocal.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Packets/Player/PacketGameSettings.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAlly.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBaseInfo.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBook.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAttribute.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimFlags.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimPlay.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAttack.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCast.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerClass.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCellChange.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCellState.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCooldowns.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerDeath.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerEquipment.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerFaction.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventory.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerJournal.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerLuaEvent.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerMiscellaneous.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerPosition.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerQuickKeys.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerResurrect.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerShapeshift.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSkill.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellbook.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellsActive.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerStatsDynamic.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerTopic.hpp>
#include <components/openmw-mp/Packets/System/PacketSystemHandshake.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketCellReset.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketClientScriptGlobal.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketClientScriptSettings.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketRecordDynamic.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldCollisionOverride.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldDestinationOverride.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldKillCount.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldMap.hpp>
#include <components/openmw-mp/ServerPassword.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketTransport.hpp>
#include <components/openmw-mp/Version.hpp>

#include <components/openmw-mp/Transport/PacketStream.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace
{
    using namespace mwmp;

    TEST(MpPacketStreamTest, roundTripsBytesAcrossUnalignedBitBoundaries)
    {
        PacketStream stream;
        const std::uint16_t sentShort = 0x1234;
        const std::array<char, 3> sentBytes = { 'A', 'B', 'C' };

        stream.Write(true);
        stream.Write(sentShort);
        stream.Write(false);
        stream.Write(sentBytes.data(), static_cast<unsigned int>(sentBytes.size()));

        EXPECT_EQ(stream.size(), 6u);

        bool firstBool = false;
        std::uint16_t receivedShort = 0;
        bool secondBool = true;
        std::array<char, 3> receivedBytes = {};

        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(firstBool));
        ASSERT_TRUE(stream.Read(receivedShort));
        ASSERT_TRUE(stream.Read(secondBool));
        ASSERT_TRUE(stream.Read(receivedBytes.data(), static_cast<unsigned int>(receivedBytes.size())));

        EXPECT_TRUE(firstBool);
        EXPECT_EQ(receivedShort, sentShort);
        EXPECT_FALSE(secondBool);
        EXPECT_EQ(receivedBytes, sentBytes);
    }

    TEST(MpPacketStreamTest, roundTripsCompressedValuesAcrossUnalignedBitBoundaries)
    {
        PacketStream stream;
        const std::uint32_t sentSparse = 0x12;
        const float sentFloat = 0.25f;
        const double sentDouble = -0.5;

        stream.Write(true);
        stream.WriteCompressed(sentSparse);
        stream.WriteCompressed(sentFloat);
        stream.WriteCompressed(sentDouble);

        bool prefix = false;
        std::uint32_t receivedSparse = 0;
        float receivedFloat = 0.f;
        double receivedDouble = 0.0;

        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(prefix));
        ASSERT_TRUE(stream.ReadCompressed(receivedSparse));
        ASSERT_TRUE(stream.ReadCompressed(receivedFloat));
        ASSERT_TRUE(stream.ReadCompressed(receivedDouble));

        EXPECT_TRUE(prefix);
        EXPECT_EQ(receivedSparse, sentSparse);
        EXPECT_NEAR(receivedFloat, sentFloat, 0.0001f);
        EXPECT_NEAR(receivedDouble, sentDouble, 0.000000001);
    }

    TEST(MpPacketStreamTest, readPastEndFailsWithoutMutatingValue)
    {
        PacketStream stream;
        const std::uint16_t sentValue = 0xBEEF;
        stream.Write(sentValue);

        std::uint16_t receivedValue = 0;
        std::uint8_t extraValue = 0xAA;

        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(receivedValue));
        EXPECT_EQ(receivedValue, sentValue);
        EXPECT_FALSE(stream.Read(extraValue));
        EXPECT_EQ(extraValue, 0xAA);
    }

    TEST(MpPacketStreamTest, resetPointersAllowSafeStreamReuse)
    {
        PacketStream stream;
        const std::uint16_t firstValue = 0x1234;
        const std::uint8_t replacementValue = 0x5A;

        stream.Write(firstValue);
        stream.ResetReadPointer();

        std::uint16_t firstRead = 0;
        ASSERT_TRUE(stream.Read(firstRead));
        EXPECT_EQ(firstRead, firstValue);

        stream.ResetReadPointer();
        firstRead = 0;
        ASSERT_TRUE(stream.Read(firstRead));
        EXPECT_EQ(firstRead, firstValue);

        stream.ResetWritePointer();
        stream.Write(replacementValue);
        EXPECT_EQ(stream.size(), 1u);

        stream.ResetReadPointer();
        std::uint8_t replacementRead = 0;
        ASSERT_TRUE(stream.Read(replacementRead));
        EXPECT_EQ(replacementRead, replacementValue);

        std::uint8_t staleRead = 0xCC;
        EXPECT_FALSE(stream.Read(staleRead));
        EXPECT_EQ(staleRead, 0xCC);

        stream.Reset();
        EXPECT_EQ(stream.size(), 0u);
        staleRead = 0xDD;
        EXPECT_FALSE(stream.Read(staleRead));
        EXPECT_EQ(staleRead, 0xDD);
    }

    TEST(MpPacketStreamTest, zeroByteReadSucceedsAfterUnalignedBitRead)
    {
        PacketStream stream;
        const std::array<char, 1> sentBytes = { 'Z' };

        stream.Write(true);
        stream.Write(sentBytes.data(), static_cast<unsigned int>(sentBytes.size()));

        bool prefix = false;
        char unchanged = 'x';

        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(prefix));
        EXPECT_TRUE(prefix);
        EXPECT_TRUE(stream.Read(&unchanged, 0));
        EXPECT_EQ(unchanged, 'x');

        std::array<char, 1> receivedBytes = {};
        ASSERT_TRUE(stream.Read(receivedBytes.data(), static_cast<unsigned int>(receivedBytes.size())));
        EXPECT_EQ(receivedBytes, sentBytes);
    }

    TEST(MpPacketStreamTest, emptyInputAndZeroLengthOperationsAreSafeNoops)
    {
        PacketStream empty(nullptr, 0);
        EXPECT_EQ(empty.size(), 0u);

        char unchanged = 'x';
        EXPECT_TRUE(empty.Read(&unchanged, 0));
        EXPECT_EQ(unchanged, 'x');
        EXPECT_FALSE(empty.Read(unchanged));
        EXPECT_EQ(unchanged, 'x');

        PacketStream stream;
        const std::uint16_t value = 0xBEEF;
        stream.Write(value, 0);
        EXPECT_EQ(stream.size(), 0u);

        std::uint16_t received = 0x1234;
        EXPECT_TRUE(stream.Read(received, 0));
        EXPECT_EQ(received, 0x1234);
        EXPECT_FALSE(stream.Read(received));
        EXPECT_EQ(received, 0x1234);
    }

    class TestPacket final : public mwmp::BasePacket
    {
    public:
        TestPacket()
            : BasePacket()
        {
        }

        void beginRead(PacketStream& stream)
        {
            Packet(&stream, false);
        }

        bool rw(PacketStream& stream, ESM::RefId& id, bool write, bool compress)
        {
            bs = &stream;
            return RW(id, write, compress);
        }

        bool rw(PacketStream& stream, std::string& value, bool write, bool compress, std::string::size_type maxSize)
        {
            bs = &stream;
            return RW(value, write, compress, maxSize);
        }

        bool rw(PacketStream& stream, std::variant<ESM::RefId, ESM::RefNum>& value, bool write)
        {
            bs = &stream;
            return RW(value, write);
        }
    };

    class TestActorSpellsActivePacket final : public mwmp::PacketActorSpellsActive
    {
    public:
        void readActorPayload(PacketStream& stream, mwmp::BaseActor& actor)
        {
            mwmp::BasePacket::Packet(&stream, false);
            Actor(actor, false);
        }
    };

    class TestClientScriptLocalPacket final : public mwmp::PacketClientScriptLocal
    {
    public:
        void readObjectPayload(PacketStream& stream, mwmp::BaseObject& object)
        {
            mwmp::BasePacket::Packet(&stream, false);
            Object(object, false);
        }
    };

    class TestRecordDynamicPacket final : public mwmp::PacketRecordDynamic
    {
    public:
        void readEffects(PacketStream& stream, ESM::EffectList& effectList)
        {
            mwmp::BasePacket::Packet(&stream, false);
            ProcessEffects(effectList, false);
        }

        void readBodyParts(PacketStream& stream, ESM::PartReferenceList& partList)
        {
            mwmp::BasePacket::Packet(&stream, false);
            ProcessBodyParts(partList, false);
        }

        void readInventory(PacketStream& stream, std::vector<mwmp::Item>& inventory, ESM::InventoryList& inventoryList)
        {
            mwmp::BasePacket::Packet(&stream, false);
            ProcessInventoryList(inventory, inventoryList, false);
        }
    };

    TEST(MpBasePacketTest, refIdReadFallsBackToPlainStringIdsUnknownToStaticStore)
    {
        for (const bool compress : { false, true })
        {
            SCOPED_TRACE(compress ? "compressed" : "uncompressed");

            PacketStream stream;
            TestPacket packet;

            ESM::RefId sent = ESM::RefId::stringRefId("dark elf");
            ASSERT_TRUE(packet.rw(stream, sent, true, compress));

            ESM::RefId received;
            ASSERT_TRUE(packet.rw(stream, received, false, compress));

            EXPECT_FALSE(received.empty());
            EXPECT_EQ(received, "dark elf");
            EXPECT_EQ(received.serializeText(), "dark elf");
        }
    }

    TEST(MpBasePacketTest, stringsUseTes3mpLengthPrefixedSerializationAndClampToMaxSize)
    {
        for (const bool compress : { false, true })
        {
            SCOPED_TRACE(compress ? "compressed compatibility flag" : "uncompressed");

            PacketStream stream;
            TestPacket packet;

            std::string sent = "abcdef";
            ASSERT_TRUE(packet.rw(stream, sent, true, compress, 4));

            uint32_t serializedSize = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(serializedSize));
            EXPECT_EQ(serializedSize, 4u);

            stream.ResetReadPointer();
            std::string received;
            ASSERT_TRUE(packet.rw(stream, received, false, compress, 3));
            EXPECT_EQ(received, "abc");
        }
    }

    PacketGuid testGuid()
    {
        return makePacketGuid(0x1020304050607080);
    }

    void setInteriorCell(ESM::Cell& cell, const std::string& cellName)
    {
        cell.mData.mFlags |= ESM::Cell::Interior;
        cell.mName = cellName;
        cell.updateId();
    }

    constexpr float testMovementSampleInterval = 1.f / 144.f;
    constexpr float testMovementLatency = 0.061f;

    void setMovementTiming(mwmp::BasePlayer& player)
    {
        player.movementSampleIntervalSeconds = testMovementSampleInterval;
        player.movementLatencySeconds = testMovementLatency;
    }

    void setMovementTiming(mwmp::BaseActor& actor)
    {
        actor.movementSampleIntervalSeconds = testMovementSampleInterval;
        actor.movementLatencySeconds = testMovementLatency;
    }

    void expectMovementTiming(const mwmp::BasePlayer& player)
    {
        EXPECT_FLOAT_EQ(
            player.movementSampleIntervalSeconds, mwmp::sanitizeMovementSampleIntervalSeconds(testMovementSampleInterval));
        EXPECT_FLOAT_EQ(player.movementLatencySeconds, testMovementLatency);
    }

    void expectMovementTiming(const mwmp::BaseActor& actor)
    {
        EXPECT_FLOAT_EQ(
            actor.movementSampleIntervalSeconds, mwmp::sanitizeMovementSampleIntervalSeconds(testMovementSampleInterval));
        EXPECT_FLOAT_EQ(actor.movementLatencySeconds, testMovementLatency);
    }

    TEST(MpBasePacketTest, failedPrimitiveAndStringReadsMarkPacketInvalid)
    {
        {
            PacketStream stream;
            TestPacket packet;
            packet.beginRead(stream);

            std::string received = "unchanged";
            EXPECT_FALSE(packet.rw(stream, received, false, false, 16));
            EXPECT_FALSE(packet.isPacketValid());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 4 });
            stream.Write("ab", 2);

            TestPacket packet;
            packet.beginRead(stream);

            std::string received;
            EXPECT_FALSE(packet.rw(stream, received, false, false, 16));
            EXPECT_FALSE(packet.isPacketValid());
            EXPECT_TRUE(received.empty());
        }
    }

    TEST(MpBasePacketTest, invalidVariantTagMarksPacketInvalid)
    {
        PacketStream stream;
        stream.Write(std::uint8_t{ 2 });

        TestPacket packet;
        packet.beginRead(stream);

        std::variant<ESM::RefId, ESM::RefNum> received;
        EXPECT_FALSE(packet.rw(stream, received, false));
        EXPECT_FALSE(packet.isPacketValid());
    }

    TEST(MpBasePacketTest, truncatedPlayerPacketPayloadMarksPacketInvalid)
    {
        PacketStream stream;

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerPosition reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpPacketIdentityTest, packetAddressHelpersFormatAndRoundTripWithoutRakNetTypes)
    {
        PacketAddress loopback = makePacketAddress("[::1]", 25565);
        EXPECT_TRUE(isPacketAddressAssigned(loopback));
        EXPECT_TRUE(isPacketAddressNumericHost(loopback.host));
        EXPECT_EQ(packetAddressToString(loopback, false), "::1");
        EXPECT_EQ(packetAddressToString(loopback, true), "[::1]:25565");
        EXPECT_EQ(packetAddressToString(loopback, true, '|'), "::1|25565");

        PacketAddress hostname = makePacketAddress("example.invalid", 1234);
        EXPECT_TRUE(isPacketAddressAssigned(hostname));
        EXPECT_FALSE(isPacketAddressNumericHost(hostname.host));
        setPacketAddressPortHostOrder(hostname, 25566);
        EXPECT_EQ(packetAddressPort(hostname), 25566);
        EXPECT_EQ(packetAddressToString(hostname, true), "example.invalid:25566");

        PacketStream stream;
        ASSERT_TRUE(writePacketAddress(stream, loopback));

        PacketAddress received;
        ASSERT_TRUE(readPacketAddress(stream, received));
        EXPECT_EQ(received, loopback);

        PacketAddress tooLong{ std::string(65536, 'a'), 1 };
        PacketStream oversizedStream;
        EXPECT_FALSE(writePacketAddress(oversizedStream, tooLong));
        EXPECT_EQ(oversizedStream.size(), 0u);

        PacketStream truncatedStream;
        const std::uint16_t hostSize = 4;
        truncatedStream.Write(hostSize);
        truncatedStream.Write("ab", 2);

        PacketAddress unchanged = hostname;
        EXPECT_FALSE(readPacketAddress(truncatedStream, unchanged));
        EXPECT_EQ(unchanged, hostname);
    }

    class CapturingTransport final : public mwmp::PacketTransport
    {
    public:
        uint32_t send(const unsigned char* data, std::size_t length, PacketPriority priority,
            PacketReliability reliability, int8_t orderChannel, const PacketDestination& destination, bool broadcast) override
        {
            sentData.assign(data, data + length);
            sentPriority = priority;
            sentReliability = reliability;
            sentOrderChannel = orderChannel;
            sentDestination = destination;
            sentBroadcast = broadcast;
            return 1;
        }

        ReceivedPacket* receive() override { return nullptr; }
        void deallocatePacket(ReceivedPacket*) override {}
        void closeConnection(const PacketDestination&, bool) override {}
        void banAddress(const char*) override {}
        void unbanAddress(const char*) override {}
        PacketAddress getPacketAddress(PacketGuid) const override
        {
            return unassignedPacketAddress();
        }
        PacketGuid getMyGuid() const override { return testGuid(); }
        unsigned short numberOfConnections() const override { return 0; }
        unsigned int maxConnections() const override { return 0; }
        int averagePing(const PacketDestination&) const override { return 0; }
        unsigned short port() const override { return 0; }

        std::vector<unsigned char> sentData;
        PacketPriority sentPriority = PacketPriority::Low;
        PacketReliability sentReliability = PacketReliability::Unreliable;
        int8_t sentOrderChannel = -1;
        PacketDestination sentDestination;
        bool sentBroadcast = true;
    };

    class ScopedPacketTransport
    {
    public:
        explicit ScopedPacketTransport(mwmp::PacketTransport* transport)
        {
            mwmp::BasePacket::SetPacketTransport(transport);
        }

        ~ScopedPacketTransport()
        {
            mwmp::BasePacket::SetPacketTransport(nullptr);
        }
    };

    TEST(MpBasePacketTest, sendUsesPacketTransportWithoutRakPeerFallback)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BaseSystem system(testGuid());
        system.playerName = "alex";
        system.serverPassword = "secret";
        system.accountPasswordHash = "client-password-hash";

        mwmp::PacketSystemHandshake packet;
        packet.SetSendStream(&sendStream);
        packet.setGUID(testGuid());
        packet.setSystem(&system);

        const mwmp::PacketDestination destination(testGuid());

        EXPECT_EQ(packet.Send(destination), 1u);

        ASSERT_GE(transport.sentData.size(), mwmp::BasePacket::headerSize());
        EXPECT_EQ(transport.sentData[0], ID_SYSTEM_HANDSHAKE);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_SYSTEM);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    template <class Packet>
    void writePlayerPacketToPayload(Packet& packet, mwmp::BasePlayer& player, PacketStream& stream)
    {
        packet.setPlayer(&player);
        packet.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, packet.GetPacketID());
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));
    }

    template <class Packet>
    void writeActorPacketToPayload(Packet& packet, mwmp::BaseActorList& actorList, PacketStream& stream)
    {
        packet.setActorList(&actorList);
        packet.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, packet.GetPacketID());
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));
    }

    mwmp::ActiveSpell makeFiniteActiveSpell()
    {
        mwmp::ActiveSpell spell;
        spell.id = "firebite";
        spell.timestampDay = 7;
        spell.timestampHour = 13.5;
        spell.params.mDisplayName = "Firebite";
        spell.caster.isPlayer = true;
        spell.caster.guid = testGuid();

        ESM::ActiveEffect effect{};
        effect.mEffectId = ESM::RefId::stringRefId("fire damage");
        effect.mMagnitude = 5.f;
        effect.mMinMagnitude = 5.f;
        effect.mMaxMagnitude = 5.f;
        effect.mDuration = 10.f;
        effect.mTimeLeft = 8.f;
        spell.params.mEffects.push_back(effect);

        return spell;
    }

    template <class Packet>
    void expectTruncatedPlayerPacketInvalid(mwmp::BasePlayer& sent, unsigned char expectedPacketId)
    {
        PacketStream fullStream;
        Packet writer;
        writer.setPlayer(&sent);
        writer.Packet(&fullStream, true);

        ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
        PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, expectedPacketId);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BasePlayer received(testGuid());
        Packet reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    template <class Packet>
    void expectTruncatedActorPacketInvalid(mwmp::BaseActorList& sent, unsigned char expectedPacketId)
    {
        PacketStream fullStream;
        Packet writer;
        writer.setActorList(&sent);
        writer.Packet(&fullStream, true);

        ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
        PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, expectedPacketId);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseActorList received;
        received.isValid = true;
        Packet reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
    }

    template <class Packet>
    void expectTruncatedObjectPacketInvalid(mwmp::BaseObjectList& sent, unsigned char expectedPacketId)
    {
        PacketStream fullStream;
        Packet writer;
        writer.setObjectList(&sent);
        writer.Packet(&fullStream, true);

        ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
        PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, expectedPacketId);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        Packet reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseObjects.empty());
    }

    TEST(MpBasePacketTest, playerPositionRoundTripsSequenceAndMovement)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.positionSequence = 0x80000001u;
        sent.position.pos[0] = 12.5f;
        sent.position.pos[1] = -34.25f;
        sent.position.pos[2] = 56.f;
        sent.position.rot[0] = 0.125f;
        sent.position.rot[2] = 1.25f;
        sent.direction.pos[0] = 1.f;
        sent.direction.pos[1] = -1.f;
        sent.direction.rot[2] = 0.5f;
        setMovementTiming(sent);

        PacketStream stream;
        mwmp::PacketPlayerPosition writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerPosition reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.positionSequence, 0x80000001u);
        EXPECT_FLOAT_EQ(received.position.pos[0], 12.5f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -34.25f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 56.f);
        EXPECT_FLOAT_EQ(received.position.rot[0], 0.125f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 1.25f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], -1.f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.5f);
        expectMovementTiming(received);
    }

    TEST(MpBasePacketTest, playerPositionUsesUnreliableSequencedMovementDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());
        mwmp::PacketPlayerPosition packet;
        packet.SetSendStream(&sendStream);
        packet.setPlayer(&player);

        EXPECT_EQ(packet.SendWithReliability(mwmp::PacketDestination(testGuid()), PacketReliability::ReliableOrdered), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::UnreliableSequenced);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerCellChangeRoundTripsCellAndPreviousPosition)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.cell.mData.mFlags = ESM::Cell::Interior;
        sent.cell.mData.mX = 2;
        sent.cell.mData.mY = -3;
        sent.cell.mName = "Balmora, Guild of Mages";
        sent.cell.mRegion = ESM::RefId::stringRefId("west gash");
        sent.positionSequence = 0x80000010u;
        sent.position.pos[0] = 128.f;
        sent.position.pos[1] = -256.f;
        sent.position.pos[2] = 64.f;
        sent.position.rot[2] = 1.25f;
        sent.direction.pos[1] = 1.f;
        sent.direction.rot[2] = 0.25f;
        setMovementTiming(sent);
        sent.cellChangeReason = mwmp::CELL_CHANGE_REASON_MAGIC_RECALL;
        sent.previousCellPosition.pos[0] = 12.5f;
        sent.previousCellPosition.pos[1] = -34.25f;
        sent.previousCellPosition.pos[2] = 56.f;
        sent.isChangingRegion = true;

        PacketStream stream;
        mwmp::PacketPlayerCellChange writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerCellChange reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.cell.mData.mFlags, ESM::Cell::Interior);
        EXPECT_EQ(received.cell.mData.mX, 2);
        EXPECT_EQ(received.cell.mData.mY, -3);
        EXPECT_EQ(received.cell.mName, "Balmora, Guild of Mages");
        EXPECT_EQ(received.cell.mRegion, ESM::RefId::stringRefId("west gash"));
        EXPECT_EQ(received.positionSequence, 0x80000010u);
        EXPECT_FLOAT_EQ(received.position.pos[0], 128.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -256.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 64.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 1.25f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], 1.f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.25f);
        expectMovementTiming(received);
        EXPECT_EQ(received.cellChangeReason, mwmp::CELL_CHANGE_REASON_MAGIC_RECALL);
        EXPECT_FLOAT_EQ(received.previousCellPosition.pos[0], 12.5f);
        EXPECT_FLOAT_EQ(received.previousCellPosition.pos[1], -34.25f);
        EXPECT_FLOAT_EQ(received.previousCellPosition.pos[2], 56.f);
        EXPECT_TRUE(received.isChangingRegion);
    }

    TEST(MpBasePacketTest, playerCellChangeRejectsInvalidReason)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.cell.mData.mFlags = ESM::Cell::Interior;
        sent.cell.mName = "Balmora, Guild of Mages";
        sent.positionSequence = 0x80000012u;
        sent.position.pos[0] = 128.f;
        sent.position.pos[1] = -256.f;
        sent.position.pos[2] = 64.f;
        sent.cellChangeReason = 999u;

        PacketStream stream;
        mwmp::PacketPlayerCellChange writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerCellChange reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, playerCellChangeRejectsNonFiniteMovementSnapshot)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.cell.mData.mFlags = ESM::Cell::Interior;
        sent.cell.mName = "Balmora, Guild of Mages";
        sent.positionSequence = 0x80000011u;
        sent.position.pos[0] = std::numeric_limits<float>::infinity();
        sent.direction.rot[2] = std::numeric_limits<float>::quiet_NaN();

        PacketStream stream;
        mwmp::PacketPlayerCellChange writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerCellChange reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, playerCellChangeUsesImmediateReliablePlayerDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());
        mwmp::PacketPlayerCellChange packet;
        packet.SetSendStream(&sendStream);
        packet.setPlayer(&player);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::Immediate);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_PLAYER);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerPositionSequenceRejectsAndRestoresStaleDecodedPackets)
    {
        mwmp::BasePlayer player(testGuid());
        player.positionSequence = 10;
        player.position.pos[0] = 100.f;
        player.direction.pos[0] = 1.f;

        EXPECT_TRUE(player.acceptPositionPacket());

        player.positionSequence = 9;
        player.position.pos[0] = -50.f;
        player.direction.pos[0] = -1.f;

        EXPECT_FALSE(player.acceptPositionPacket());
        EXPECT_EQ(player.positionSequence, 10u);
        EXPECT_FLOAT_EQ(player.position.pos[0], 100.f);
        EXPECT_FLOAT_EQ(player.direction.pos[0], 1.f);

        player.positionSequence = 11;
        player.position.pos[0] = 125.f;
        player.direction.pos[0] = 0.f;

        EXPECT_TRUE(player.acceptPositionPacket());
        EXPECT_EQ(player.positionSequence, 11u);
        EXPECT_FLOAT_EQ(player.position.pos[0], 125.f);
        EXPECT_FLOAT_EQ(player.direction.pos[0], 0.f);
    }

    TEST(MpBasePacketTest, playerAcceptedCharacterStateResetAllowsNewCharacterSequences)
    {
        mwmp::BasePlayer player(testGuid());
        player.positionSequence = 40;
        player.position.pos[0] = 100.f;
        ASSERT_TRUE(player.acceptPositionPacket());

        player.animFlagsSequence = 41;
        player.movementFlags = 7;
        player.drawState = 2;
        ASSERT_TRUE(player.acceptAnimFlagsPacket());

        player.inventorySequence = 42;
        player.inventoryChanges.action = mwmp::InventoryChanges::ADD;
        player.inventoryChanges.items.push_back({ "gold_001", 10, -1, -1.f, "" });
        ASSERT_TRUE(player.acceptInventoryPacket());

        ESM::Spell acceptedSpell;
        acceptedSpell.mId = ESM::RefId::stringRefId("firebite");
        player.spellbookChanges.action = mwmp::SpellbookChanges::SET;
        player.spellbookChanges.spells = { acceptedSpell };
        player.acceptCurrentSpellbookPacket();

        player.equipmentSequence = 43;
        player.equipmentItems[0].refId = "iron dagger";
        player.equipmentItems[0].count = 1;
        ASSERT_TRUE(player.acceptEquipmentPacket());

        player.statsDynamicSequence = 44;
        for (int statIndex = 0; statIndex < 3; ++statIndex)
        {
            player.creatureStats.mDynamic[statIndex].mBase = 100.f;
            player.creatureStats.mDynamic[statIndex].mCurrent = 0.f;
            player.creatureStats.mDynamic[statIndex].mMod = 0.f;
        }
        player.creatureStats.mDead = true;
        ASSERT_TRUE(player.acceptStatsDynamicPacket(true));

        player.combatSequence = 45;
        ASSERT_TRUE(player.acceptCombatPacket());

        player.clearAcceptedCharacterState();

        EXPECT_FALSE(player.hasAcceptedPositionPacket);
        EXPECT_FALSE(player.hasAcceptedAnimFlagsPacket);
        EXPECT_FALSE(player.hasAcceptedInventoryPacket);
        EXPECT_FALSE(player.hasAcceptedEquipmentPacket);
        EXPECT_FALSE(player.hasAcceptedStatsDynamicPacket);
        EXPECT_FALSE(player.hasAcceptedCombatPacket);
        EXPECT_EQ(player.positionSequence, 0u);
        EXPECT_EQ(player.inventorySequence, 0u);
        EXPECT_EQ(player.equipmentSequence, 0u);
        EXPECT_EQ(player.statsDynamicSequence, 0u);
        EXPECT_EQ(player.combatSequence, 0u);
        EXPECT_FALSE(player.acceptedStatsDynamicDead);
        EXPECT_TRUE(player.statsDynamicIndexChanges.empty());
        EXPECT_TRUE(player.equipmentIndexChanges.empty());
        EXPECT_EQ(player.inventoryChanges.action, mwmp::InventoryChanges::SET);
        EXPECT_TRUE(player.inventoryChanges.items.empty());
        EXPECT_TRUE(player.acceptedInventoryItems.empty());
        EXPECT_FALSE(player.hasAcceptedSpellbookPacket);
        EXPECT_TRUE(player.acceptedSpellbookSpells.empty());
        EXPECT_TRUE(player.equipmentItems[0].refId.empty());
        EXPECT_FLOAT_EQ(player.position.pos[0], 0.f);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 0.f);
        EXPECT_FALSE(player.creatureStats.mDead);

        player.positionSequence = 1;
        player.position.pos[0] = -10.f;
        EXPECT_TRUE(player.acceptPositionPacket());

        player.statsDynamicSequence = 1;
        for (int statIndex = 0; statIndex < 3; ++statIndex)
        {
            player.creatureStats.mDynamic[statIndex].mBase = 100.f;
            player.creatureStats.mDynamic[statIndex].mCurrent = 100.f;
            player.creatureStats.mDynamic[statIndex].mMod = 0.f;
        }
        player.creatureStats.mDead = false;
        EXPECT_TRUE(player.acceptStatsDynamicPacket(true));

        player.equipmentSequence = 1;
        player.equipmentItems[0].refId = "chitin dagger";
        player.equipmentItems[0].count = 1;
        EXPECT_TRUE(player.acceptEquipmentPacket());

        player.inventorySequence = 1;
        player.inventoryChanges.action = mwmp::InventoryChanges::SET;
        player.inventoryChanges.items = { { "gold_001", 5, -1, -1.f, "" } };
        EXPECT_TRUE(player.acceptInventoryPacket());

        player.combatSequence = 1;
        EXPECT_TRUE(player.acceptCombatPacket());
    }

    TEST(MpBasePacketTest, playerPositionRejectsAndRestoresNonFiniteDecodedPackets)
    {
        mwmp::BasePlayer player(testGuid());
        player.positionSequence = 10;
        player.position.pos[0] = 100.f;
        player.position.pos[1] = 200.f;
        player.direction.pos[0] = 1.f;
        player.direction.rot[2] = 0.25f;

        EXPECT_TRUE(player.acceptPositionPacket());

        player.positionSequence = 11;
        player.position.pos[0] = -50.f;
        player.position.pos[1] = std::numeric_limits<float>::infinity();
        player.direction.pos[0] = -1.f;
        player.direction.rot[2] = std::numeric_limits<float>::quiet_NaN();

        EXPECT_FALSE(player.acceptPositionPacket());
        EXPECT_EQ(player.positionSequence, 10u);
        EXPECT_FLOAT_EQ(player.position.pos[0], 100.f);
        EXPECT_FLOAT_EQ(player.position.pos[1], 200.f);
        EXPECT_FLOAT_EQ(player.direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(player.direction.rot[2], 0.25f);
    }

    TEST(MpBasePacketTest, playerAnimPlayRoundTripsCombatTransformAndAnimation)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.combatSequence = 41;
        sent.positionSequence = 0x80000009u;
        sent.position.pos[0] = 16.f;
        sent.position.pos[1] = -32.f;
        sent.position.pos[2] = 64.f;
        sent.position.rot[2] = 0.875f;
        sent.direction.pos[0] = 0.5f;
        sent.direction.pos[1] = -0.5f;
        sent.direction.rot[2] = 0.125f;
        setMovementTiming(sent);
        sent.animation.groupname = "attack1";
        sent.animation.mode = 2;
        sent.animation.count = 3;
        sent.animation.persist = true;

        PacketStream stream;
        mwmp::PacketPlayerAnimPlay writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerAnimPlay reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.combatSequence, 41u);
        EXPECT_EQ(received.positionSequence, 0x80000009u);
        EXPECT_FLOAT_EQ(received.position.pos[0], 16.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -32.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 64.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 0.875f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], 0.5f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], -0.5f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.125f);
        expectMovementTiming(received);
        EXPECT_EQ(received.animation.groupname, "attack1");
        EXPECT_EQ(received.animation.mode, 2);
        EXPECT_EQ(received.animation.count, 3);
        EXPECT_TRUE(received.animation.persist);
    }

    TEST(MpBasePacketTest, playerMovementAnimationPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000021u;
            sent.position.pos[0] = 12.f;
            sent.position.pos[1] = -24.f;
            sent.position.pos[2] = 36.f;
            sent.position.rot[2] = 0.75f;
            sent.direction.pos[0] = 1.f;
            sent.direction.rot[2] = 0.25f;

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerPosition>(sent, ID_PLAYER_POSITION);
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000022u;
            sent.position.pos[0] = 32.f;
            sent.position.pos[1] = -64.f;
            sent.position.pos[2] = 96.f;
            sent.direction.pos[1] = -1.f;
            sent.animFlagsSequence = 0x80000003u;
            sent.movementFlags = 0x13;
            sent.drawState = 2;
            sent.isJumping = true;
            sent.isFlying = true;
            sent.hasTcl = true;

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerAnimFlags>(sent, ID_PLAYER_ANIM_FLAGS);
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000023u;
            sent.position.pos[0] = -16.f;
            sent.position.pos[1] = 48.f;
            sent.position.pos[2] = 80.f;
            sent.direction.pos[0] = 0.5f;
            sent.direction.rot[2] = -0.125f;
            sent.animation.groupname = "attack1";
            sent.animation.mode = 2;
            sent.animation.count = 3;
            sent.animation.persist = true;

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerAnimPlay>(sent, ID_PLAYER_ANIM_PLAY);
        }
    }

    TEST(MpBasePacketTest, playerAttackRoundTripsCombatTransformAndHitState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.combatSequence = 42;
        sent.positionSequence = 0x80000007u;
        sent.position.pos[0] = 12.f;
        sent.position.pos[1] = -24.f;
        sent.position.pos[2] = 36.f;
        sent.position.rot[2] = 1.125f;
        sent.direction.pos[0] = 1.f;
        sent.direction.pos[1] = -1.f;
        sent.direction.rot[2] = 0.375f;
        setMovementTiming(sent);
        sent.attack.target.isPlayer = false;
        sent.attack.target.refId = "rat";
        sent.attack.target.refNum = 777;
        sent.attack.target.mpNum = 2;
        sent.attack.type = mwmp::Attack::RANGED;
        sent.attack.pressed = true;
        sent.attack.success = true;
        sent.attack.isHit = true;
        sent.attack.attackStrength = 0.75f;
        sent.attack.rangedWeaponId = "iron bow";
        sent.attack.rangedAmmoId = "iron arrow";
        sent.attack.projectileOrigin.origin[0] = 1.f;
        sent.attack.projectileOrigin.origin[1] = 2.f;
        sent.attack.projectileOrigin.origin[2] = 3.f;
        sent.attack.projectileOrigin.orientation[3] = 1.f;
        sent.attack.damage = 12.f;
        sent.attack.knockdown = true;
        sent.attack.applyWeaponEnchantment = true;
        sent.attack.applyAmmoEnchantment = true;
        sent.attack.hitPosition.pos[0] = 4.f;
        sent.attack.hitPosition.pos[1] = 5.f;
        sent.attack.hitPosition.pos[2] = 6.f;

        PacketStream stream;
        mwmp::PacketPlayerAttack writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerAttack reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.combatSequence, 42u);
        EXPECT_EQ(received.positionSequence, 0x80000007u);
        EXPECT_FLOAT_EQ(received.position.pos[0], 12.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -24.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 36.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 1.125f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], -1.f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.375f);
        expectMovementTiming(received);
        EXPECT_EQ(received.attack.target.refId, "rat");
        EXPECT_EQ(received.attack.target.refNum, 777u);
        EXPECT_EQ(received.attack.target.mpNum, 2u);
        EXPECT_EQ(received.attack.type, mwmp::Attack::RANGED);
        EXPECT_TRUE(received.attack.pressed);
        EXPECT_TRUE(received.attack.success);
        EXPECT_TRUE(received.attack.isHit);
        EXPECT_FLOAT_EQ(received.attack.attackStrength, 0.75f);
        EXPECT_EQ(received.attack.rangedWeaponId, "iron bow");
        EXPECT_EQ(received.attack.rangedAmmoId, "iron arrow");
        EXPECT_FLOAT_EQ(received.attack.projectileOrigin.origin[0], 1.f);
        EXPECT_FLOAT_EQ(received.attack.projectileOrigin.origin[1], 2.f);
        EXPECT_FLOAT_EQ(received.attack.projectileOrigin.origin[2], 3.f);
        EXPECT_FLOAT_EQ(received.attack.projectileOrigin.orientation[3], 1.f);
        EXPECT_FLOAT_EQ(received.attack.damage, 12.f);
        EXPECT_TRUE(received.attack.knockdown);
        EXPECT_TRUE(received.attack.applyWeaponEnchantment);
        EXPECT_TRUE(received.attack.applyAmmoEnchantment);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[0], 4.f);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[1], 5.f);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[2], 6.f);
    }

    TEST(MpBasePacketTest, playerMeleeAttackRoundTripsWeaponContext)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.attack.target.isPlayer = false;
        sent.attack.target.refId = "rat";
        sent.attack.target.refNum = 777;
        sent.attack.target.mpNum = 2;
        sent.attack.type = mwmp::Attack::MELEE;
        sent.attack.pressed = false;
        sent.attack.success = true;
        sent.attack.isHit = true;
        sent.attack.attackAnimation = "slash";
        sent.attack.attackStrength = 0.625f;
        sent.attack.rangedWeaponId = "iron longsword";
        sent.attack.damage = 8.f;
        sent.attack.applyWeaponEnchantment = true;
        sent.attack.hitPosition.pos[0] = 4.f;
        sent.attack.hitPosition.pos[1] = 5.f;
        sent.attack.hitPosition.pos[2] = 6.f;

        PacketStream stream;
        mwmp::PacketPlayerAttack writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerAttack reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.attack.target.refId, "rat");
        EXPECT_EQ(received.attack.target.refNum, 777u);
        EXPECT_EQ(received.attack.target.mpNum, 2u);
        EXPECT_EQ(received.attack.type, mwmp::Attack::MELEE);
        EXPECT_FALSE(received.attack.pressed);
        EXPECT_TRUE(received.attack.success);
        EXPECT_TRUE(received.attack.isHit);
        EXPECT_EQ(received.attack.attackAnimation, "slash");
        EXPECT_FLOAT_EQ(received.attack.attackStrength, 0.625f);
        EXPECT_EQ(received.attack.rangedWeaponId, "iron longsword");
        EXPECT_FLOAT_EQ(received.attack.damage, 8.f);
        EXPECT_TRUE(received.attack.applyWeaponEnchantment);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[0], 4.f);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[1], 5.f);
        EXPECT_FLOAT_EQ(received.attack.hitPosition.pos[2], 6.f);
    }

    TEST(MpBasePacketTest, playerCastRoundTripsCombatTransformAndProjectileState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.combatSequence = 43;
        sent.positionSequence = 0x80000008u;
        sent.position.pos[0] = -12.f;
        sent.position.pos[1] = 24.f;
        sent.position.pos[2] = 48.f;
        sent.position.rot[2] = -1.125f;
        sent.direction.pos[0] = -1.f;
        sent.direction.pos[1] = 1.f;
        sent.direction.rot[2] = -0.375f;
        setMovementTiming(sent);
        sent.cast.target.isPlayer = false;
        sent.cast.target.refId = "skeleton";
        sent.cast.target.refNum = 321;
        sent.cast.target.mpNum = 6;
        sent.cast.type = mwmp::Cast::REGULAR;
        sent.cast.pressed = true;
        sent.cast.success = true;
        sent.cast.instant = true;
        sent.cast.spellId = "firebite";
        sent.cast.hasProjectile = true;
        sent.cast.projectileOrigin.origin[0] = 7.f;
        sent.cast.projectileOrigin.origin[1] = 8.f;
        sent.cast.projectileOrigin.origin[2] = 9.f;
        sent.cast.projectileOrigin.orientation[3] = 1.f;

        PacketStream stream;
        mwmp::PacketPlayerCast writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerCast reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.combatSequence, 43u);
        EXPECT_EQ(received.positionSequence, 0x80000008u);
        EXPECT_FLOAT_EQ(received.position.pos[0], -12.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], 24.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 48.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], -1.125f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], -1.f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], 1.f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], -0.375f);
        expectMovementTiming(received);
        EXPECT_EQ(received.cast.target.refId, "skeleton");
        EXPECT_EQ(received.cast.target.refNum, 321u);
        EXPECT_EQ(received.cast.target.mpNum, 6u);
        EXPECT_EQ(received.cast.type, mwmp::Cast::REGULAR);
        EXPECT_TRUE(received.cast.pressed);
        EXPECT_TRUE(received.cast.success);
        EXPECT_TRUE(received.cast.instant);
        EXPECT_EQ(received.cast.spellId, "firebite");
        EXPECT_TRUE(received.cast.hasProjectile);
        EXPECT_FLOAT_EQ(received.cast.projectileOrigin.origin[0], 7.f);
        EXPECT_FLOAT_EQ(received.cast.projectileOrigin.origin[1], 8.f);
        EXPECT_FLOAT_EQ(received.cast.projectileOrigin.origin[2], 9.f);
        EXPECT_FLOAT_EQ(received.cast.projectileOrigin.orientation[3], 1.f);
    }

    TEST(MpBasePacketTest, playerDeathRoundTripsCombatTransformAndKillerState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.combatSequence = 44;
        sent.positionSequence = 0x8000000Au;
        sent.position.pos[0] = 18.f;
        sent.position.pos[1] = -36.f;
        sent.position.pos[2] = 72.f;
        sent.position.rot[2] = 1.75f;
        sent.direction.pos[0] = 0.75f;
        sent.direction.pos[1] = -0.25f;
        sent.direction.rot[2] = 0.5f;
        setMovementTiming(sent);
        sent.deathState = 3;
        sent.killer.isPlayer = false;
        sent.killer.refId = "skeleton";
        sent.killer.refNum = 321;
        sent.killer.mpNum = 6;
        sent.killer.name = "Bonewalker";

        PacketStream stream;
        mwmp::PacketPlayerDeath writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerDeath reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.combatSequence, 44u);
        EXPECT_EQ(received.positionSequence, 0x8000000Au);
        EXPECT_FLOAT_EQ(received.position.pos[0], 18.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -36.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 72.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 1.75f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], 0.75f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], -0.25f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.5f);
        expectMovementTiming(received);
        EXPECT_EQ(received.deathState, 3);
        EXPECT_FALSE(received.killer.isPlayer);
        EXPECT_EQ(received.killer.refId, "skeleton");
        EXPECT_EQ(received.killer.refNum, 321u);
        EXPECT_EQ(received.killer.mpNum, 6u);
        EXPECT_EQ(received.killer.name, "Bonewalker");
    }

    TEST(MpBasePacketTest, playerCombatSequenceRejectsStaleEvents)
    {
        mwmp::BasePlayer player(testGuid());
        player.combatSequence = 100;

        ASSERT_TRUE(player.acceptCombatPacket());
        EXPECT_EQ(player.acceptedCombatSequence, 100u);
        EXPECT_TRUE(player.hasAcceptedCombatPacket);

        player.combatSequence = 99;
        EXPECT_FALSE(player.acceptCombatPacket());
        EXPECT_EQ(player.combatSequence, 100u);
        EXPECT_EQ(player.acceptedCombatSequence, 100u);

        player.combatSequence = 101;
        EXPECT_TRUE(player.acceptCombatPacket());
        EXPECT_EQ(player.acceptedCombatSequence, 101u);
    }

    TEST(MpBasePacketTest, playerCombatEventPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000011u;
            sent.attack.target.isPlayer = false;
            sent.attack.target.refId = "rat";
            sent.attack.target.refNum = 777;
            sent.attack.target.mpNum = 2;
            sent.attack.type = mwmp::Attack::RANGED;
            sent.attack.pressed = false;
            sent.attack.success = true;
            sent.attack.isHit = true;
            sent.attack.attackStrength = 0.75f;
            sent.attack.rangedWeaponId = "iron bow";
            sent.attack.rangedAmmoId = "iron arrow";
            sent.attack.projectileOrigin.orientation[3] = 1.f;
            sent.attack.damage = 12.f;
            sent.attack.applyAmmoEnchantment = true;
            sent.attack.hitPosition.pos[2] = 6.f;

            PacketStream fullStream;
            mwmp::PacketPlayerAttack writer;
            writer.setPlayer(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_PLAYER_ATTACK);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAttack reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000012u;
            sent.cast.target.isPlayer = false;
            sent.cast.target.refId = "skeleton";
            sent.cast.target.refNum = 321;
            sent.cast.target.mpNum = 6;
            sent.cast.type = mwmp::Cast::REGULAR;
            sent.cast.pressed = true;
            sent.cast.success = true;
            sent.cast.instant = true;
            sent.cast.spellId = "firebite";
            sent.cast.hasProjectile = true;
            sent.cast.projectileOrigin.orientation[3] = 1.f;

            PacketStream fullStream;
            mwmp::PacketPlayerCast writer;
            writer.setPlayer(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_PLAYER_CAST);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerCast reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.positionSequence = 0x80000013u;
            sent.deathState = 3;
            sent.killer.isPlayer = false;
            sent.killer.refId = "skeleton";
            sent.killer.refNum = 321;
            sent.killer.mpNum = 6;
            sent.killer.name = "Bonewalker";

            PacketStream fullStream;
            mwmp::PacketPlayerDeath writer;
            writer.setPlayer(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_PLAYER_DEATH);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerDeath reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }
    }

    TEST(MpBasePacketTest, playerCombatEventPacketsRejectInvalidValues)
    {
        {
            mwmp::BasePlayer sent(testGuid());
            sent.attack.target.isPlayer = false;
            sent.attack.target.refId = "rat";
            sent.attack.target.refNum = 777;
            sent.attack.target.mpNum = 2;
            sent.attack.type = 99;
            sent.attack.isHit = true;
            sent.attack.damage = 12.f;
            sent.attack.hitPosition.pos[0] = 4.f;
            sent.attack.hitPosition.pos[1] = 5.f;
            sent.attack.hitPosition.pos[2] = 6.f;

            PacketStream stream;
            mwmp::PacketPlayerAttack writer;
            writePlayerPacketToPayload(writer, sent, stream);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAttack reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.attack.target.isPlayer = false;
            sent.attack.target.refId = "rat";
            sent.attack.target.refNum = 777;
            sent.attack.target.mpNum = 2;
            sent.attack.type = mwmp::Attack::RANGED;
            sent.attack.isHit = true;
            sent.attack.attackStrength = 0.75f;
            sent.attack.rangedWeaponId = "iron bow";
            sent.attack.rangedAmmoId = "iron arrow";
            sent.attack.projectileOrigin.origin[0] = std::numeric_limits<float>::quiet_NaN();
            sent.attack.projectileOrigin.orientation[3] = 1.f;
            sent.attack.damage = 12.f;
            sent.attack.hitPosition.pos[0] = 4.f;
            sent.attack.hitPosition.pos[1] = 5.f;
            sent.attack.hitPosition.pos[2] = 6.f;

            PacketStream stream;
            mwmp::PacketPlayerAttack writer;
            writePlayerPacketToPayload(writer, sent, stream);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAttack reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.cast.target.isPlayer = false;
            sent.cast.target.refId = "skeleton";
            sent.cast.target.refNum = 321;
            sent.cast.target.mpNum = 6;
            sent.cast.type = mwmp::Cast::REGULAR;
            sent.cast.pressed = false;
            sent.cast.success = true;
            sent.cast.instant = false;
            sent.cast.spellId = "";

            PacketStream stream;
            mwmp::PacketPlayerCast writer;
            writePlayerPacketToPayload(writer, sent, stream);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerCast reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        }
    }

    TEST(MpBasePacketTest, playerDeathUsesCombatDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());

        mwmp::PacketPlayerDeath packet;
        packet.SetSendStream(&sendStream);
        packet.setPlayer(&player);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_COMBAT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerStatsDynamicUsesMovementDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());
        player.exchangeFullInfo = false;
        player.statsDynamicIndexChanges.push_back(0);

        mwmp::PacketPlayerStatsDynamic packet;
        packet.SetSendStream(&sendStream);
        packet.setPlayer(&player);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerStatsDynamicRoundTripsDeathState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.statsDynamicSequence = 42;
        sent.exchangeFullInfo = false;
        sent.creatureStats.mDead = true;
        sent.creatureStats.mDynamic[0].mBase = 100.f;
        sent.creatureStats.mDynamic[0].mCurrent = 0.f;
        sent.statsDynamicIndexChanges.push_back(0);

        PacketStream stream;
        mwmp::PacketPlayerStatsDynamic writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerStatsDynamic reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_TRUE(received.creatureStats.mDead);
        EXPECT_EQ(received.statsDynamicSequence, 42u);
        ASSERT_EQ(received.statsDynamicIndexChanges.size(), 1);
        EXPECT_EQ(received.statsDynamicIndexChanges[0], 0);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[0].mBase, 100.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[0].mCurrent, 0.f);
    }

    TEST(MpBasePacketTest, playerStatsDynamicCompactRoundTripPreservesOmittedDynamicState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.statsDynamicSequence = 77;
        sent.exchangeFullInfo = false;
        sent.creatureStats.mDynamic[0].mBase = 120.f;
        sent.creatureStats.mDynamic[0].mCurrent = 65.f;
        sent.statsDynamicIndexChanges.push_back(0);

        PacketStream stream;
        mwmp::PacketPlayerStatsDynamic writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        received.creatureStats.mDynamic[1].mBase = 50.f;
        received.creatureStats.mDynamic[1].mCurrent = 35.f;
        received.creatureStats.mDynamic[2].mBase = 80.f;
        received.creatureStats.mDynamic[2].mCurrent = 70.f;

        mwmp::PacketPlayerStatsDynamic reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.statsDynamicSequence, 77u);
        ASSERT_EQ(received.statsDynamicIndexChanges.size(), 1);
        EXPECT_EQ(received.statsDynamicIndexChanges[0], 0);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[0].mBase, 120.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[0].mCurrent, 65.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[1].mBase, 50.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[1].mCurrent, 35.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[2].mBase, 80.f);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[2].mCurrent, 70.f);
    }

    TEST(MpBasePacketTest, playerStatsDynamicRejectsStaleSequencesAndRestoresAcceptedSnapshot)
    {
        mwmp::BasePlayer player(testGuid());
        player.statsDynamicSequence = 10;
        player.creatureStats.mDynamic[0].mCurrent = 80.f;
        ASSERT_TRUE(player.acceptStatsDynamicPacket());

        player.statsDynamicSequence = 9;
        player.creatureStats.mDynamic[0].mCurrent = 100.f;
        player.statsDynamicIndexChanges.push_back(0);

        EXPECT_FALSE(player.acceptStatsDynamicPacket());
        EXPECT_EQ(player.statsDynamicSequence, 10u);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 80.f);
        EXPECT_TRUE(player.statsDynamicIndexChanges.empty());

        player.statsDynamicSequence = 11;
        player.creatureStats.mDynamic[0].mCurrent = 70.f;

        EXPECT_TRUE(player.acceptStatsDynamicPacket());
        EXPECT_EQ(player.statsDynamicSequence, 11u);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 70.f);
    }

    TEST(MpBasePacketTest, playerStatsDynamicRejectsNonFiniteValuesAndRestoresAcceptedSnapshot)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.statsDynamicSequence = 8;
        sent.exchangeFullInfo = false;
        sent.creatureStats.mDynamic[0].mBase = 100.f;
        sent.creatureStats.mDynamic[0].mCurrent = std::numeric_limits<float>::quiet_NaN();
        sent.statsDynamicIndexChanges.push_back(0);

        PacketStream stream;
        mwmp::PacketPlayerStatsDynamic writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        received.statsDynamicSequence = 7;
        received.creatureStats.mDynamic[0].mBase = 100.f;
        received.creatureStats.mDynamic[0].mCurrent = 80.f;
        received.creatureStats.mDynamic[1].mBase = 40.f;
        received.creatureStats.mDynamic[1].mCurrent = 30.f;
        received.creatureStats.mDynamic[2].mBase = 60.f;
        received.creatureStats.mDynamic[2].mCurrent = 50.f;
        for (int i = 0; i < 3; ++i)
            received.creatureStats.mDynamic[i].mMod = 0.f;
        ASSERT_TRUE(received.acceptStatsDynamicPacket());

        mwmp::PacketPlayerStatsDynamic reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_EQ(received.statsDynamicSequence, 7u);
        EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[0].mCurrent, 80.f);
        EXPECT_TRUE(received.statsDynamicIndexChanges.empty());

        PacketStream unacceptedStream;
        mwmp::PacketPlayerStatsDynamic unacceptedWriter;
        writePlayerPacketToPayload(unacceptedWriter, sent, unacceptedStream);

        mwmp::BasePlayer unaccepted(testGuid());
        mwmp::PacketPlayerStatsDynamic unacceptedReader;
        unacceptedReader.setPlayer(&unaccepted);
        unacceptedReader.Packet(&unacceptedStream, false);

        EXPECT_FALSE(unacceptedReader.isPacketValid());
        EXPECT_TRUE(unaccepted.hasFiniteDynamicStats());
        EXPECT_EQ(unaccepted.statsDynamicSequence, 0u);
        EXPECT_FLOAT_EQ(unaccepted.creatureStats.mDynamic[0].mCurrent, 0.f);
        EXPECT_TRUE(unaccepted.statsDynamicIndexChanges.empty());
    }

    TEST(MpBasePacketTest, playerStatsDynamicDefaultReceiverAllowsServerHealthIncreaseAndRevive)
    {
        mwmp::BasePlayer player(testGuid());
        player.statsDynamicSequence = 1;
        player.creatureStats.mDynamic[0].mBase = 100.f;
        player.creatureStats.mDynamic[0].mCurrent = 35.f;
        player.creatureStats.mDynamic[1].mBase = 40.f;
        player.creatureStats.mDynamic[1].mCurrent = 30.f;
        player.creatureStats.mDynamic[2].mBase = 60.f;
        player.creatureStats.mDynamic[2].mCurrent = 50.f;
        for (int i = 0; i < 3; ++i)
            player.creatureStats.mDynamic[i].mMod = 0.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket());

        player.statsDynamicSequence = 2;
        player.creatureStats.mDynamic[0].mCurrent = 80.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket());
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 80.f);

        player.statsDynamicSequence = 3;
        player.creatureStats.mDead = true;
        player.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket());

        player.statsDynamicSequence = 4;
        player.creatureStats.mDead = false;
        player.creatureStats.mDynamic[0].mCurrent = 60.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket());
        EXPECT_FALSE(player.creatureStats.mDead);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 60.f);
    }

    TEST(MpBasePacketTest, clientPlayerStatsDynamicAuthorityRejectsHealthIncreaseAndRevive)
    {
        mwmp::BasePlayer player(testGuid());
        player.statsDynamicSequence = 1;
        player.creatureStats.mDynamic[0].mBase = 100.f;
        player.creatureStats.mDynamic[0].mCurrent = 50.f;
        player.creatureStats.mDynamic[1].mBase = 40.f;
        player.creatureStats.mDynamic[1].mCurrent = 30.f;
        player.creatureStats.mDynamic[2].mBase = 60.f;
        player.creatureStats.mDynamic[2].mCurrent = 50.f;
        for (int i = 0; i < 3; ++i)
            player.creatureStats.mDynamic[i].mMod = 0.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket(true));

        player.statsDynamicSequence = 2;
        player.creatureStats.mDynamic[0].mCurrent = 35.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket(true));
        EXPECT_EQ(player.statsDynamicSequence, 2u);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 35.f);

        player.statsDynamicSequence = 3;
        player.creatureStats.mDynamic[0].mCurrent = 45.f;
        player.statsDynamicIndexChanges.push_back(0);
        EXPECT_FALSE(player.acceptStatsDynamicPacket(true));
        EXPECT_EQ(player.statsDynamicSequence, 2u);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 35.f);
        EXPECT_TRUE(player.statsDynamicIndexChanges.empty());

        player.statsDynamicSequence = 3;
        player.creatureStats.mDead = true;
        player.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_TRUE(player.acceptStatsDynamicPacket(true));
        EXPECT_TRUE(player.creatureStats.mDead);

        player.statsDynamicSequence = 4;
        player.creatureStats.mDead = false;
        player.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_FALSE(player.acceptStatsDynamicPacket(true));
        EXPECT_EQ(player.statsDynamicSequence, 3u);
        EXPECT_TRUE(player.creatureStats.mDead);
        EXPECT_FLOAT_EQ(player.creatureStats.mDynamic[0].mCurrent, 0.f);
    }

    TEST(MpBasePacketTest, clientPlayerDeathAuthorityRequiresAcceptedDeadStats)
    {
        mwmp::BasePlayer player(testGuid());
        player.deathState = 1;
        player.creatureStats.mDynamic[0].mBase = 100.f;
        player.creatureStats.mDynamic[0].mCurrent = 40.f;
        player.creatureStats.mDynamic[1].mBase = 30.f;
        player.creatureStats.mDynamic[1].mCurrent = 12.f;
        player.creatureStats.mDynamic[2].mBase = 50.f;
        player.creatureStats.mDynamic[2].mCurrent = 25.f;
        for (int i = 0; i < 3; ++i)
            player.creatureStats.mDynamic[i].mMod = 0.f;

        EXPECT_FALSE(player.isClientDeathPacketAllowed());
        ASSERT_TRUE(player.acceptStatsDynamicPacket(true));
        EXPECT_FALSE(player.isClientDeathPacketAllowed());

        player.statsDynamicSequence = 2;
        player.creatureStats.mDynamic[0].mCurrent = 0.f;
        ASSERT_TRUE(player.acceptStatsDynamicPacket(true));
        EXPECT_TRUE(player.isClientDeathPacketAllowed());

        player.deathState = 0;
        EXPECT_FALSE(player.isClientDeathPacketAllowed());
        player.deathState = 1;

        player.statsDynamicSequence = 3;
        player.creatureStats.mDead = true;
        player.creatureStats.mDynamic[0].mCurrent = 0.f;
        ASSERT_TRUE(player.acceptStatsDynamicPacket(true));
        EXPECT_TRUE(player.isClientDeathPacketAllowed());
    }

    TEST(MpBasePacketTest, playerResurrectCarriesAuthoritativeDynamicStats)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.resurrectType = mwmp::RESURRECT_TYPE::IMPERIAL_SHRINE;
        sent.statsDynamicSequence = 42;
        sent.creatureStats.mDead = false;
        sent.creatureStats.mDynamic[0].mBase = 100.f;
        sent.creatureStats.mDynamic[0].mCurrent = 100.f;
        sent.creatureStats.mDynamic[0].mMod = 0.f;
        sent.creatureStats.mDynamic[1].mBase = 60.f;
        sent.creatureStats.mDynamic[1].mCurrent = 38.f;
        sent.creatureStats.mDynamic[1].mMod = 4.f;
        sent.creatureStats.mDynamic[2].mBase = 80.f;
        sent.creatureStats.mDynamic[2].mCurrent = 80.f;
        sent.creatureStats.mDynamic[2].mMod = 0.f;

        PacketStream stream;
        mwmp::PacketPlayerResurrect writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerResurrect reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.resurrectType, sent.resurrectType);
        EXPECT_EQ(received.statsDynamicSequence, 42u);
        EXPECT_FALSE(received.creatureStats.mDead);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[i].mBase, sent.creatureStats.mDynamic[i].mBase);
            EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[i].mCurrent, sent.creatureStats.mDynamic[i].mCurrent);
            EXPECT_FLOAT_EQ(received.creatureStats.mDynamic[i].mMod, sent.creatureStats.mDynamic[i].mMod);
        }
    }

    TEST(MpBasePacketTest, playerCellAndStatsPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BasePlayer sent(testGuid());
            sent.cell.mData.mFlags = ESM::Cell::Interior;
            sent.cell.mName = "Balmora, Guild of Mages";
            sent.cell.mRegion = ESM::RefId::stringRefId("west gash");
            sent.previousCellPosition.pos[0] = 12.5f;
            sent.previousCellPosition.pos[1] = -34.25f;
            sent.previousCellPosition.pos[2] = 56.f;
            sent.isChangingRegion = true;

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerCellChange>(sent, ID_PLAYER_CELL_CHANGE);
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.exchangeFullInfo = false;
            sent.creatureStats.mDead = true;
            sent.creatureStats.mDynamic[0].mBase = 100.f;
            sent.creatureStats.mDynamic[0].mCurrent = 0.f;
            sent.statsDynamicIndexChanges.push_back(0);

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerStatsDynamic>(sent, ID_PLAYER_STATS_DYNAMIC);
        }

        {
            mwmp::BasePlayer sent(testGuid());
            sent.resurrectType = mwmp::RESURRECT_TYPE::REGULAR;
            sent.statsDynamicSequence = 5;
            sent.creatureStats.mDynamic[0].mBase = 100.f;
            sent.creatureStats.mDynamic[0].mCurrent = 100.f;
            sent.creatureStats.mDynamic[1].mBase = 50.f;
            sent.creatureStats.mDynamic[1].mCurrent = 50.f;
            sent.creatureStats.mDynamic[2].mBase = 75.f;
            sent.creatureStats.mDynamic[2].mCurrent = 75.f;

            expectTruncatedPlayerPacketInvalid<mwmp::PacketPlayerResurrect>(sent, ID_PLAYER_RESURRECT);
        }
    }

    TEST(MpBasePacketTest, playerCompactStatPacketsRejectOversizedCountsBeforeResize)
    {
        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(false);
            stream.Write(false);
            stream.Write(std::uint32_t{ 4 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerStatsDynamic reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.statsDynamicIndexChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(false);
            stream.Write(std::uint32_t{ 9 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAttribute reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.attributeIndexChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(false);
            stream.Write(std::uint32_t{ 28 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerSkill reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.skillIndexChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(false);
            stream.Write(std::uint32_t{ 20 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerEquipment reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.equipmentIndexChanges.empty());
        }
    }

    TEST(MpBasePacketTest, playerCompactPacketsRejectTruncatedCountsAndIndexes)
    {
        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(false);
            stream.Write(false);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerStatsDynamic reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.statsDynamicIndexChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(false);
            stream.Write(std::uint32_t{ 1 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerEquipment reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.equipmentIndexChanges.empty());
        }
    }

    TEST(MpBasePacketTest, playerEquipmentRejectsInvalidCompactSlotBeforeItemReplay)
    {
        PacketStream stream;
        stream.Write(std::uint32_t{ 1 });
        stream.Write(false);
        stream.Write(std::uint32_t{ 1 });
        stream.Write(19);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerEquipment reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(received.equipmentIndexChanges.empty());
    }

    TEST(MpBasePacketTest, playerEquipmentRoundTripsSequenceAndCompactChanges)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.equipmentSequence = 7;
        sent.exchangeFullInfo = false;
        sent.equipmentIndexChanges = { 0, 13 };
        sent.equipmentItems[0].refId = "iron dagger";
        sent.equipmentItems[0].count = 1;
        sent.equipmentItems[0].charge = 42;
        sent.equipmentItems[0].enchantmentCharge = -1.f;
        sent.equipmentItems[13].refId = "";
        sent.equipmentItems[13].count = 0;
        sent.equipmentItems[13].charge = -1;
        sent.equipmentItems[13].enchantmentCharge = -1.f;

        PacketStream stream;
        mwmp::PacketPlayerEquipment writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerEquipment reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.equipmentSequence, 7u);
        ASSERT_EQ(received.equipmentIndexChanges.size(), 2u);
        EXPECT_EQ(received.equipmentIndexChanges[0], 0);
        EXPECT_EQ(received.equipmentIndexChanges[1], 13);
        EXPECT_EQ(received.equipmentItems[0].refId, "iron dagger");
        EXPECT_EQ(received.equipmentItems[0].count, 1);
        EXPECT_EQ(received.equipmentItems[0].charge, 42);
        EXPECT_TRUE(received.equipmentItems[13].refId.empty());
        EXPECT_EQ(received.equipmentItems[13].count, 0);
    }

    TEST(MpBasePacketTest, playerEquipmentCompactChangesPreserveUnmentionedSlots)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.equipmentSequence = 8;
        sent.exchangeFullInfo = false;
        sent.equipmentIndexChanges = { 0 };
        sent.equipmentItems[0].refId = "iron dagger";
        sent.equipmentItems[0].count = 1;
        sent.equipmentItems[0].charge = 42;
        sent.equipmentItems[0].enchantmentCharge = -1.f;

        PacketStream stream;
        mwmp::PacketPlayerEquipment writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        received.equipmentSequence = 7;
        received.exchangeFullInfo = false;
        received.equipmentItems[5].refId = "common_shirt_01";
        received.equipmentItems[5].count = 1;
        received.equipmentItems[5].charge = -1;
        received.equipmentItems[5].enchantmentCharge = -1.f;

        mwmp::PacketPlayerEquipment reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.equipmentSequence, 8u);
        ASSERT_EQ(received.equipmentIndexChanges.size(), 1u);
        EXPECT_EQ(received.equipmentIndexChanges[0], 0);
        EXPECT_EQ(received.equipmentItems[0].refId, "iron dagger");
        EXPECT_EQ(received.equipmentItems[5].refId, "common_shirt_01");
        EXPECT_EQ(received.equipmentItems[5].count, 1);
    }

    TEST(MpBasePacketTest, playerEquipmentFullSnapshotMarksEverySlotChanged)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.equipmentSequence = 9;
        sent.exchangeFullInfo = true;
        sent.equipmentItems[7].refId = "common_shoes_01";
        sent.equipmentItems[7].count = 1;
        sent.equipmentItems[7].charge = -1;
        sent.equipmentItems[7].enchantmentCharge = -1.f;
        sent.equipmentItems[8].refId = "common_shirt_01";
        sent.equipmentItems[8].count = 1;
        sent.equipmentItems[8].charge = -1;
        sent.equipmentItems[8].enchantmentCharge = -1.f;

        PacketStream stream;
        mwmp::PacketPlayerEquipment writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerEquipment reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.equipmentSequence, 9u);
        EXPECT_TRUE(received.exchangeFullInfo);
        ASSERT_EQ(received.equipmentIndexChanges.size(), mwmp::equipmentSlotCount);
        for (std::size_t slot = 0; slot < mwmp::equipmentSlotCount; ++slot)
            EXPECT_EQ(received.equipmentIndexChanges[slot], static_cast<int>(slot));
        EXPECT_EQ(received.equipmentItems[7].refId, "common_shoes_01");
        EXPECT_EQ(received.equipmentItems[8].refId, "common_shirt_01");
    }

    TEST(MpBasePacketTest, playerEquipmentRejectsInvalidItemPayloadsWithoutSlotReplay)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.equipmentSequence = 8;
        sent.exchangeFullInfo = false;
        sent.equipmentIndexChanges = { 0, 1 };
        sent.equipmentItems[0].refId = "iron dagger";
        sent.equipmentItems[0].count = 1;
        sent.equipmentItems[1].refId = "$dynamic_bad_equipment";
        sent.equipmentItems[1].count = 1;

        PacketStream stream;
        mwmp::PacketPlayerEquipment writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerEquipment reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_EQ(received.equipmentSequence, 0u);
        EXPECT_TRUE(received.equipmentIndexChanges.empty());
        EXPECT_TRUE(received.equipmentItems[0].refId.empty());
        EXPECT_TRUE(received.equipmentItems[1].refId.empty());
    }

    TEST(MpBasePacketTest, playerEquipmentSequenceRejectsStaleAndRestoresAcceptedSnapshot)
    {
        mwmp::BasePlayer player(testGuid());
        player.equipmentSequence = 10;
        player.equipmentIndexChanges = { 0 };
        player.equipmentItems[0].refId = "iron dagger";
        player.equipmentItems[0].count = 1;
        ASSERT_TRUE(player.acceptEquipmentPacket());

        player.equipmentSequence = 9;
        player.equipmentIndexChanges = { 0 };
        player.equipmentItems[0].refId = "daedric longsword";
        player.equipmentItems[0].count = 1;
        EXPECT_FALSE(player.acceptEquipmentPacket());
        EXPECT_EQ(player.equipmentSequence, 10u);
        EXPECT_EQ(player.equipmentItems[0].refId, "iron dagger");
        EXPECT_TRUE(player.equipmentIndexChanges.empty());

        player.equipmentSequence = 11;
        player.equipmentIndexChanges = { 0 };
        player.equipmentItems[0].refId = "steel dagger";
        player.equipmentItems[0].count = 1;
        EXPECT_TRUE(player.acceptEquipmentPacket());
        EXPECT_EQ(player.equipmentSequence, 11u);
        EXPECT_EQ(player.equipmentItems[0].refId, "steel dagger");
    }

    TEST(MpBasePacketTest, playerLuaEventRoundTripsBoundedPayload)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.luaEvent.schemaVersion = mwmp::clientLuaEventSchemaVersion;
        sent.luaEvent.sequence = 42;
        sent.luaEvent.namespaceName = "communitymp.server";
        sent.luaEvent.eventName = "ready";
        sent.luaEvent.payload = "{\"schema\":1,\"kind\":\"ready\"}";

        PacketStream stream;
        mwmp::PacketPlayerLuaEvent writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerLuaEvent reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.luaEvent.schemaVersion, mwmp::clientLuaEventSchemaVersion);
        EXPECT_EQ(received.luaEvent.sequence, 42u);
        EXPECT_EQ(received.luaEvent.namespaceName, "communitymp.server");
        EXPECT_EQ(received.luaEvent.eventName, "ready");
        EXPECT_EQ(received.luaEvent.payload, "{\"schema\":1,\"kind\":\"ready\"}");
    }

    TEST(MpBasePacketTest, playerLuaEventRejectsOversizedStringsWithoutTruncation)
    {
        PacketStream stream;
        stream.Write(mwmp::clientLuaEventSchemaVersion);
        stream.Write(std::uint32_t{ 1 });
        const std::string oversizedNamespace(mwmp::clientLuaEventMaxNamespaceLength + 1, 'a');
        stream.Write(static_cast<std::uint32_t>(oversizedNamespace.size()));
        stream.Write(oversizedNamespace.data(), static_cast<unsigned int>(oversizedNamespace.size()));

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerLuaEvent reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(received.luaEvent.namespaceName.empty());

        PacketStream payloadStream;
        payloadStream.Write(mwmp::clientLuaEventSchemaVersion);
        payloadStream.Write(std::uint32_t{ 1 });
        payloadStream.Write(std::uint32_t{ 18 });
        payloadStream.Write("communitymp.server", 18);
        payloadStream.Write(std::uint32_t{ 5 });
        payloadStream.Write("ready", 5);
        const std::string oversizedPayload(mwmp::clientLuaEventMaxPayloadLength + 1, 'x');
        payloadStream.Write(static_cast<std::uint32_t>(oversizedPayload.size()));
        payloadStream.Write(oversizedPayload.data(), static_cast<unsigned int>(oversizedPayload.size()));

        mwmp::BasePlayer receivedPayload(testGuid());
        mwmp::PacketPlayerLuaEvent payloadReader;
        payloadReader.setPlayer(&receivedPayload);
        payloadReader.Packet(&payloadStream, false);

        EXPECT_FALSE(payloadReader.isPacketValid());
        EXPECT_TRUE(receivedPayload.luaEvent.payload.empty());
    }

    TEST(MpBasePacketTest, playerListPacketsRejectOversizedCountsBeforeResize)
    {
        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAlly reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.alliedPlayers.empty());
        }

        {
            PacketStream stream;
            stream.Write(true);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerBook reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.bookChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerCellState reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.cellStateChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerCooldowns reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.cooldownChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerFaction reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.factionChanges.factions.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(0);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerInventory reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.inventoryChanges.items.empty());
        }

        {
            PacketStream stream;
            stream.Write(false);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerJournal reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.journalChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 11 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerQuickKeys reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.quickKeyChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerSpellbook reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.spellbookChanges.spells.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerSpellsActive reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.spellsActiveChanges.activeSpells.empty());
        }

        {
            PacketStream stream;
            stream.Write(false);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerTopic reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.topicChanges.empty());
        }
    }

    TEST(MpBasePacketTest, playerListPacketsRejectTruncatedCountsBeforeResize)
    {
        {
            PacketStream stream;

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerAlly reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.alliedPlayers.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });
            stream.Write(0);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerInventory reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.inventoryChanges.items.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerSpellsActive reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.spellsActiveChanges.activeSpells.empty());
        }
    }

    TEST(MpBasePacketTest, playerInventoryRejectsInvalidActions)
    {
        PacketStream stream;
        stream.Write(std::uint32_t{ 1 });
        stream.Write(99);
        stream.Write(std::uint32_t{ 0 });

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerInventory reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(received.inventoryChanges.items.empty());
    }

    TEST(MpBasePacketTest, playerInventorySkipsInvalidItemMutations)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.inventorySequence = 12;
        sent.inventoryChanges.action = mwmp::InventoryChanges::ADD;

        mwmp::Item validItem;
        validItem.refId = "gold_001";
        validItem.count = 7;
        validItem.charge = -1;
        validItem.enchantmentCharge = -1.f;
        sent.inventoryChanges.items.push_back(validItem);

        mwmp::Item emptyRefId = validItem;
        emptyRefId.refId.clear();
        sent.inventoryChanges.items.push_back(emptyRefId);

        mwmp::Item dynamicRefId = validItem;
        dynamicRefId.refId = "$dynamic_bad_item";
        sent.inventoryChanges.items.push_back(dynamicRefId);

        mwmp::Item zeroCount = validItem;
        zeroCount.count = 0;
        sent.inventoryChanges.items.push_back(zeroCount);

        mwmp::Item impossibleCount = validItem;
        impossibleCount.count = 1000001;
        sent.inventoryChanges.items.push_back(impossibleCount);

        mwmp::Item nonFiniteEnchantment = validItem;
        nonFiniteEnchantment.enchantmentCharge = std::numeric_limits<float>::quiet_NaN();
        sent.inventoryChanges.items.push_back(nonFiniteEnchantment);

        PacketStream stream;
        mwmp::PacketPlayerInventory writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerInventory reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.inventorySequence, 12u);
        ASSERT_EQ(received.inventoryChanges.items.size(), 1);
        EXPECT_EQ(received.inventoryChanges.action, mwmp::InventoryChanges::ADD);
        EXPECT_EQ(received.inventoryChanges.items[0].refId, "gold_001");
        EXPECT_EQ(received.inventoryChanges.items[0].count, 7);
        EXPECT_EQ(received.inventoryChanges.items[0].charge, -1);
        EXPECT_FLOAT_EQ(received.inventoryChanges.items[0].enchantmentCharge, -1.f);
    }

    TEST(MpBasePacketTest, playerInventorySequenceRejectsStaleAndRestoresAcceptedChanges)
    {
        mwmp::BasePlayer player(testGuid());
        player.inventorySequence = 10;
        player.inventoryChanges.action = mwmp::InventoryChanges::ADD;
        player.inventoryChanges.items.push_back({ "gold_001", 10, -1, -1.f, "" });
        ASSERT_TRUE(player.acceptInventoryPacket());

        player.inventorySequence = 9;
        player.inventoryChanges.action = mwmp::InventoryChanges::REMOVE;
        player.inventoryChanges.items = { { "daedric longsword", 1, -1, -1.f, "" } };
        EXPECT_FALSE(player.acceptInventoryPacket());
        EXPECT_EQ(player.inventorySequence, 10u);
        EXPECT_EQ(player.inventoryChanges.action, mwmp::InventoryChanges::ADD);
        ASSERT_EQ(player.inventoryChanges.items.size(), 1u);
        EXPECT_EQ(player.inventoryChanges.items[0].refId, "gold_001");
        EXPECT_EQ(player.inventoryChanges.items[0].count, 10);

        player.inventorySequence = 11;
        player.inventoryChanges.action = mwmp::InventoryChanges::REMOVE;
        player.inventoryChanges.items = { { "gold_001", 3, -1, -1.f, "" } };
        EXPECT_TRUE(player.acceptInventoryPacket());
        EXPECT_EQ(player.inventorySequence, 11u);
        EXPECT_EQ(player.inventoryChanges.action, mwmp::InventoryChanges::REMOVE);
        ASSERT_EQ(player.inventoryChanges.items.size(), 1u);
        EXPECT_EQ(player.inventoryChanges.items[0].count, 3);
    }

    TEST(MpBasePacketTest, playerInventoryAcceptMaintainsFullSnapshot)
    {
        mwmp::BasePlayer player(testGuid());

        player.inventorySequence = 1;
        player.inventoryChanges.action = mwmp::InventoryChanges::SET;
        player.inventoryChanges.items = {
            { "gold_001", 10, -1, -1.f, "" },
            { "iron dagger", 1, 80, -1.f, "" },
        };
        ASSERT_TRUE(player.acceptInventoryPacket());
        ASSERT_EQ(player.acceptedInventoryItems.size(), 2u);
        EXPECT_EQ(player.acceptedInventoryItems[0].refId, "gold_001");
        EXPECT_EQ(player.acceptedInventoryItems[0].count, 10);

        player.inventorySequence = 2;
        player.inventoryChanges.action = mwmp::InventoryChanges::ADD;
        player.inventoryChanges.items = {
            { "gold_001", 5, -1, -1.f, "" },
            { "iron dagger", 1, 50, -1.f, "" },
        };
        ASSERT_TRUE(player.acceptInventoryPacket());
        ASSERT_EQ(player.acceptedInventoryItems.size(), 3u);
        EXPECT_EQ(player.acceptedInventoryItems[0].refId, "gold_001");
        EXPECT_EQ(player.acceptedInventoryItems[0].count, 15);
        EXPECT_EQ(player.acceptedInventoryItems[2].refId, "iron dagger");
        EXPECT_EQ(player.acceptedInventoryItems[2].charge, 50);

        player.inventorySequence = 1;
        player.inventoryChanges.action = mwmp::InventoryChanges::REMOVE;
        player.inventoryChanges.items = { { "gold_001", 100, -1, -1.f, "" } };
        EXPECT_FALSE(player.acceptInventoryPacket());
        ASSERT_EQ(player.acceptedInventoryItems.size(), 3u);
        EXPECT_EQ(player.acceptedInventoryItems[0].count, 15);

        player.inventorySequence = 3;
        player.inventoryChanges.action = mwmp::InventoryChanges::REMOVE;
        player.inventoryChanges.items = {
            { "gold_001", 12, -1, -1.f, "" },
            { "iron dagger", 1, 80, -1.f, "" },
        };
        ASSERT_TRUE(player.acceptInventoryPacket());
        ASSERT_EQ(player.acceptedInventoryItems.size(), 2u);
        EXPECT_EQ(player.acceptedInventoryItems[0].refId, "gold_001");
        EXPECT_EQ(player.acceptedInventoryItems[0].count, 3);
        EXPECT_EQ(player.acceptedInventoryItems[1].refId, "iron dagger");
        EXPECT_EQ(player.acceptedInventoryItems[1].charge, 50);
    }

    TEST(MpBasePacketTest, playerSpellbookAcceptMaintainsFullSnapshot)
    {
        mwmp::BasePlayer player(testGuid());

        ESM::Spell firebite;
        firebite.mId = ESM::RefId::stringRefId("firebite");
        ESM::Spell hearthHeal;
        hearthHeal.mId = ESM::RefId::stringRefId("hearth heal");
        ESM::Spell frostbite;
        frostbite.mId = ESM::RefId::stringRefId("frostbite");

        player.spellbookChanges.action = mwmp::SpellbookChanges::SET;
        player.spellbookChanges.spells = { firebite, firebite };
        player.acceptCurrentSpellbookPacket();
        ASSERT_EQ(player.acceptedSpellbookSpells.size(), 1u);
        EXPECT_EQ(player.acceptedSpellbookSpells[0].mId, firebite.mId);

        player.spellbookChanges.action = mwmp::SpellbookChanges::ADD;
        player.spellbookChanges.spells = { hearthHeal, frostbite };
        player.acceptCurrentSpellbookPacket();
        ASSERT_EQ(player.acceptedSpellbookSpells.size(), 3u);
        EXPECT_EQ(player.acceptedSpellbookSpells[1].mId, hearthHeal.mId);
        EXPECT_EQ(player.acceptedSpellbookSpells[2].mId, frostbite.mId);

        player.spellbookChanges.action = mwmp::SpellbookChanges::REMOVE;
        player.spellbookChanges.spells = { hearthHeal };
        player.acceptCurrentSpellbookPacket();
        ASSERT_EQ(player.acceptedSpellbookSpells.size(), 2u);
        EXPECT_EQ(player.acceptedSpellbookSpells[0].mId, firebite.mId);
        EXPECT_EQ(player.acceptedSpellbookSpells[1].mId, frostbite.mId);
    }

    TEST(MpBasePacketTest, actorSpellsActiveRejectsOversizedCountBeforeResize)
    {
        PacketStream stream;
        stream.Write(0);
        stream.Write(std::uint32_t{ 3001 });

        mwmp::BaseActor actor;
        TestActorSpellsActivePacket reader;
        reader.readActorPayload(stream, actor);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(actor.spellsActiveChanges.activeSpells.empty());
    }

    TEST(MpBasePacketTest, playerSpellsActiveRoundTripsNativeSpellIdentity)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
        mwmp::ActiveSpell spell = makeFiniteActiveSpell();
        spell.id = "runtime stack key";
        spell.isStackingSpell = true;
        spell.params.mActiveSpellId = ESM::RefId::stringRefId("openmw generated active id");
        spell.params.mSourceSpellId = ESM::RefId::stringRefId("potion_restore_health_01");
        spell.caster.isPlayer = false;
        spell.caster.refId = "fargoth";
        spell.caster.refNum = 330;
        spell.caster.mpNum = 7;
        sent.spellsActiveChanges.activeSpells.push_back(spell);

        PacketStream stream;
        mwmp::PacketPlayerSpellsActive writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerSpellsActive reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.spellsActiveChanges.action, mwmp::SpellsActiveChanges::ADD);
        ASSERT_EQ(received.spellsActiveChanges.activeSpells.size(), 1);
        const mwmp::ActiveSpell& receivedSpell = received.spellsActiveChanges.activeSpells[0];
        EXPECT_EQ(receivedSpell.id, "runtime stack key");
        EXPECT_TRUE(receivedSpell.isStackingSpell);
        EXPECT_EQ(receivedSpell.params.mActiveSpellId, ESM::RefId::stringRefId("openmw generated active id"));
        EXPECT_EQ(receivedSpell.params.mSourceSpellId, ESM::RefId::stringRefId("potion_restore_health_01"));
        EXPECT_EQ(receivedSpell.params.mDisplayName, "Firebite");
        EXPECT_FALSE(receivedSpell.caster.isPlayer);
        EXPECT_EQ(receivedSpell.caster.refId, "fargoth");
        EXPECT_EQ(receivedSpell.caster.refNum, 330u);
        EXPECT_EQ(receivedSpell.caster.mpNum, 7u);
        ASSERT_EQ(receivedSpell.params.mEffects.size(), 1);
        EXPECT_EQ(receivedSpell.params.mEffects[0].mEffectId, ESM::RefId::stringRefId("fire damage"));
        EXPECT_FLOAT_EQ(receivedSpell.params.mEffects[0].mMagnitude, 5.f);
        EXPECT_FLOAT_EQ(receivedSpell.params.mEffects[0].mTimeLeft, 8.f);
    }

    TEST(MpBasePacketTest, actorSpellsActiveRoundTripsNativeSpellIdentity)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;
        mwmp::ActiveSpell spell = makeFiniteActiveSpell();
        spell.id = "runtime nonstack key";
        spell.params.mActiveSpellId = ESM::RefId::stringRefId("openmw active id");
        spell.params.mSourceSpellId = ESM::RefId::stringRefId("hearth heal");
        actor.spellsActiveChanges.activeSpells.push_back(spell);
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorSpellsActive writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorSpellsActive reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_EQ(receivedActor.spellsActiveChanges.action, mwmp::SpellsActiveChanges::REMOVE);
        ASSERT_EQ(receivedActor.spellsActiveChanges.activeSpells.size(), 1);
        const mwmp::ActiveSpell& receivedSpell = receivedActor.spellsActiveChanges.activeSpells[0];
        EXPECT_EQ(receivedSpell.id, "runtime nonstack key");
        EXPECT_FALSE(receivedSpell.isStackingSpell);
        EXPECT_EQ(receivedSpell.params.mActiveSpellId, ESM::RefId::stringRefId("openmw active id"));
        EXPECT_EQ(receivedSpell.params.mSourceSpellId, ESM::RefId::stringRefId("hearth heal"));
        EXPECT_EQ(receivedSpell.params.mDisplayName, "Firebite");
        EXPECT_TRUE(receivedSpell.caster.isPlayer);
        EXPECT_EQ(receivedSpell.caster.guid, testGuid());
        ASSERT_EQ(receivedSpell.params.mEffects.size(), 1);
        EXPECT_EQ(receivedSpell.params.mEffects[0].mEffectId, ESM::RefId::stringRefId("fire damage"));
    }

    TEST(MpBasePacketTest, playerSpellsActiveRejectsNonFiniteEffectValues)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
        mwmp::ActiveSpell spell = makeFiniteActiveSpell();
        spell.params.mEffects[0].mMagnitude = std::numeric_limits<float>::quiet_NaN();
        sent.spellsActiveChanges.activeSpells.push_back(spell);

        PacketStream stream;
        mwmp::PacketPlayerSpellsActive writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerSpellsActive reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(received.spellsActiveChanges.activeSpells.empty());
    }

    TEST(MpBasePacketTest, actorSpellsActiveRejectsNonFiniteEffectValues)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
        actor.spellsActiveChanges.activeSpells.push_back(makeFiniteActiveSpell());
        actor.spellsActiveChanges.activeSpells[0].timestampHour = std::numeric_limits<double>::infinity();
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorSpellsActive writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorSpellsActive reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseActors.empty());
    }

    TEST(MpBasePacketTest, actorPacketRejectsTruncatedHeaderCountBeforeLoop)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");
        sent.isValid = true;

        PacketStream fullStream;
        mwmp::PacketActorPosition writer;
        writeActorPacketToPayload(writer, sent, fullStream);

        ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
        PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_ACTOR_POSITION);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseActorList received;
        received.guid = testGuid();
        received.isValid = true;
        mwmp::PacketActorPosition reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseActors.empty());
    }

    TEST(MpBasePacketTest, actorPacketRejectsNamedExteriorCellHeader)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        sent.cell.mData.mX = -2;
        sent.cell.mData.mY = -9;
        sent.cell.mName = "Seyda Neen";
        sent.baseActors.emplace_back();

        PacketStream stream;
        mwmp::PacketActorPosition writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorPosition reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseActors.empty());
    }

    TEST(MpBasePacketTest, objectClientScriptLocalRejectsOversizedCountBeforeResize)
    {
        PacketStream stream;
        std::string emptyRefId;
        TestPacket stringWriter;
        ASSERT_TRUE(stringWriter.rw(stream, emptyRefId, true, true, 1024));
        stream.Write(0u);
        stream.Write(0u);
        stream.Write(std::uint32_t{ 3001 });

        mwmp::BaseObject object;
        TestClientScriptLocalPacket reader;
        reader.readObjectPayload(stream, object);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_TRUE(object.clientLocals.empty());
    }

    TEST(MpBasePacketTest, worldstateListPacketsRejectOversizedCountsBeforeResize)
    {
        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketClientScriptGlobal reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.clientGlobals.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketClientScriptSettings reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.synchronizedClientScriptIds.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 0 });
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketClientScriptSettings reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.synchronizedClientGlobalIds.empty());
        }

        {
            PacketStream stream;
            stream.Write(false);
            stream.Write(false);
            stream.Write(false);
            stream.Write(false);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldCollisionOverride reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.enforcedCollisionRefIds.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldKillCount reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.killChanges.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldMap reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.mapTiles.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketCellReset reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.cellsToReset.empty());
        }
    }

    TEST(MpBasePacketTest, worldstateMapPacketsRejectTruncatedCountsAndEntries)
    {
        {
            PacketStream stream;

            mwmp::BaseWorldstate received;
            mwmp::PacketClientScriptGlobal reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.clientGlobals.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 0 });

            mwmp::BaseWorldstate received;
            mwmp::PacketClientScriptSettings reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.synchronizedClientGlobalIds.empty());
        }

        {
            PacketStream stream;

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldMap reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.mapTiles.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1 });

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldDestinationOverride reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.destinationOverrides.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(false);
            stream.Write(true);
            stream.Write(true);
            stream.Write(true);
            stream.Write(0);
            stream.Write(60.f);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketGameSettings reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.gameSettings.empty());
        }
    }

    TEST(MpBasePacketTest, worldMapRejectsOversizedImageData)
    {
        PacketStream stream;
        stream.Write(std::uint32_t{ 1 });
        stream.Write(0);
        stream.Write(0);
        stream.Write(static_cast<std::uint32_t>(mwmp::maxImageDataSize + 1));

        mwmp::BaseWorldstate received;
        mwmp::PacketWorldMap reader;
        reader.setWorldstate(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, settingsMapPacketsRejectOversizedCountsBeforeLoop)
    {
        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(false);
            stream.Write(true);
            stream.Write(true);
            stream.Write(true);
            stream.Write(0);
            stream.Write(60.f);
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketGameSettings reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.gameSettings.empty());
        }

        {
            PacketStream stream;
            stream.Write(0);
            stream.Write(false);
            stream.Write(true);
            stream.Write(true);
            stream.Write(true);
            stream.Write(0);
            stream.Write(60.f);
            stream.Write(std::uint32_t{ 0 });
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketGameSettings reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.vrSettings.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 3001 });

            mwmp::BaseWorldstate received;
            mwmp::PacketWorldDestinationOverride reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.destinationOverrides.empty());
        }
    }

    TEST(MpBasePacketTest, recordDynamicRejectsOversizedCountsBeforeResize)
    {
        {
            PacketStream stream;
            stream.Write(static_cast<unsigned short>(mwmp::RECORD_TYPE::SPELL));
            stream.Write(3001u);

            mwmp::BaseWorldstate received;
            mwmp::PacketRecordDynamic reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.spellRecords.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 101 });

            ESM::EffectList effectList;
            TestRecordDynamicPacket reader;
            reader.readEffects(stream, effectList);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(effectList.mList.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 8 });

            ESM::PartReferenceList partList;
            TestRecordDynamicPacket reader;
            reader.readBodyParts(stream, partList);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(partList.mParts.empty());
        }

        {
            PacketStream stream;
            stream.Write(std::uint32_t{ 1001 });

            std::vector<mwmp::Item> inventory;
            ESM::InventoryList inventoryList;
            TestRecordDynamicPacket reader;
            reader.readInventory(stream, inventory, inventoryList);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(inventory.empty());
            EXPECT_TRUE(inventoryList.mList.empty());
        }
    }

    TEST(MpBasePacketTest, recordDynamicRejectsTruncatedCountsBeforeResize)
    {
        {
            PacketStream stream;
            stream.Write(static_cast<unsigned short>(mwmp::RECORD_TYPE::SPELL));

            mwmp::BaseWorldstate received;
            mwmp::PacketRecordDynamic reader;
            reader.setWorldstate(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(received.spellRecords.empty());
        }

        {
            PacketStream stream;

            ESM::EffectList effectList;
            TestRecordDynamicPacket reader;
            reader.readEffects(stream, effectList);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_TRUE(effectList.mList.empty());
        }
    }

    TEST(MpBasePacketTest, movementSequenceHelpersHandleWrapAroundBoundaries)
    {
        EXPECT_TRUE(mwmp::isNewerPlayerPositionSequence(0u, 0xFFFFFFFFu));
        EXPECT_TRUE(mwmp::isPlayerPositionSequenceAtLeast(0u, 0xFFFFFFFFu));
        EXPECT_TRUE(mwmp::isPlayerPositionSequenceAtLeast(0x80000010u, 0x80000010u));
        EXPECT_TRUE(mwmp::isNewerPlayerAnimFlagsSequence(7u, 3u));
        EXPECT_TRUE(mwmp::isNewerPositionSequence(0u, 0xFFFFFFFFu));
        EXPECT_TRUE(mwmp::isNewerActorAnimFlagsSequence(42u, 41u));

        EXPECT_FALSE(mwmp::isNewerPlayerPositionSequence(0xFFFFFFFFu, 0u));
        EXPECT_FALSE(mwmp::isPlayerPositionSequenceAtLeast(0xFFFFFFFFu, 0u));
        EXPECT_FALSE(mwmp::isPlayerPositionSequenceAtLeast(0x80000000u, 0u));
        EXPECT_FALSE(mwmp::isNewerPlayerAnimFlagsSequence(3u, 7u));
        EXPECT_FALSE(mwmp::isNewerPositionSequence(0xFFFFFFFFu, 0u));
        EXPECT_FALSE(mwmp::isNewerActorAnimFlagsSequence(41u, 42u));

        EXPECT_FALSE(mwmp::isNewerPlayerPositionSequence(0u, 0u));
        EXPECT_FALSE(mwmp::isNewerPlayerAnimFlagsSequence(0x80000000u, 0u));
        EXPECT_FALSE(mwmp::isNewerPositionSequence(0x80000000u, 0u));
        EXPECT_FALSE(mwmp::isNewerActorAnimFlagsSequence(0x80000000u, 0u));
    }

    TEST(MpBasePacketTest, playerAnimFlagsRoundTripsSequenceAndState)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.positionSequence = 0x80000011u;
        sent.position.pos[0] = 32.f;
        sent.position.pos[1] = -64.f;
        sent.position.pos[2] = 96.f;
        sent.position.rot[2] = 1.25f;
        sent.direction.pos[0] = 1.f;
        sent.direction.pos[1] = -0.5f;
        sent.direction.rot[2] = 0.25f;
        setMovementTiming(sent);
        sent.animFlagsSequence = 0x80000002u;
        sent.movementFlags = 0x13;
        sent.drawState = 2;
        sent.isJumping = true;
        sent.isFlying = true;
        sent.hasTcl = true;

        PacketStream stream;
        mwmp::PacketPlayerAnimFlags writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerAnimFlags reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.positionSequence, 0x80000011u);
        EXPECT_FLOAT_EQ(received.position.pos[0], 32.f);
        EXPECT_FLOAT_EQ(received.position.pos[1], -64.f);
        EXPECT_FLOAT_EQ(received.position.pos[2], 96.f);
        EXPECT_FLOAT_EQ(received.position.rot[2], 1.25f);
        EXPECT_FLOAT_EQ(received.direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(received.direction.pos[1], -0.5f);
        EXPECT_FLOAT_EQ(received.direction.rot[2], 0.25f);
        expectMovementTiming(received);
        EXPECT_EQ(received.animFlagsSequence, 0x80000002u);
        EXPECT_EQ(received.movementFlags, 0x13u);
        EXPECT_EQ(received.drawState, 2);
        EXPECT_TRUE(received.isJumping);
        EXPECT_TRUE(received.isFlying);
        EXPECT_TRUE(received.hasTcl);
    }

    TEST(MpBasePacketTest, playerAnimFlagsUseUnreliableSequencedMovementDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());
        mwmp::PacketPlayerAnimFlags packet;
        packet.SetSendStream(&sendStream);
        packet.setPlayer(&player);

        EXPECT_EQ(packet.SendWithReliability(mwmp::PacketDestination(testGuid()), PacketReliability::ReliableOrdered), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::UnreliableSequenced);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerAnimFlagsSequenceRejectsAndRestoresStaleDecodedPackets)
    {
        mwmp::BasePlayer player(testGuid());
        player.animFlagsSequence = 10;
        player.movementFlags = 0x11;
        player.drawState = 2;
        player.isJumping = true;
        player.isFlying = false;
        player.hasTcl = true;

        EXPECT_TRUE(player.acceptAnimFlagsPacket());

        player.animFlagsSequence = 9;
        player.movementFlags = 0;
        player.drawState = 0;
        player.isJumping = false;
        player.isFlying = true;
        player.hasTcl = false;

        EXPECT_FALSE(player.acceptAnimFlagsPacket());
        EXPECT_EQ(player.animFlagsSequence, 10u);
        EXPECT_EQ(player.movementFlags, 0x11u);
        EXPECT_EQ(player.drawState, 2);
        EXPECT_TRUE(player.isJumping);
        EXPECT_FALSE(player.isFlying);
        EXPECT_TRUE(player.hasTcl);

        player.animFlagsSequence = 11;
        player.movementFlags = 0x02;
        player.drawState = 1;
        player.isJumping = false;
        player.isFlying = false;
        player.hasTcl = false;

        EXPECT_TRUE(player.acceptAnimFlagsPacket());
        EXPECT_EQ(player.animFlagsSequence, 11u);
        EXPECT_EQ(player.movementFlags, 0x02u);
        EXPECT_EQ(player.drawState, 1);
        EXPECT_FALSE(player.isJumping);
        EXPECT_FALSE(player.isFlying);
        EXPECT_FALSE(player.hasTcl);
    }

    TEST(MpBasePacketTest, actorPositionRoundTripsSequenceMovementAndMarksData)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.positionSequence = 0x80000004u;
        actor.position.pos[0] = 12.5f;
        actor.position.pos[1] = -34.25f;
        actor.position.pos[2] = 56.f;
        actor.position.rot[0] = 0.125f;
        actor.position.rot[2] = 1.25f;
        actor.direction.pos[0] = 1.f;
        actor.direction.pos[1] = -1.f;
        actor.direction.rot[2] = 0.5f;
        setMovementTiming(actor);
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorPosition writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorPosition reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        EXPECT_EQ(received.baseActors[0].refNum, 128u);
        EXPECT_EQ(received.baseActors[0].mpNum, 4u);
        EXPECT_EQ(received.baseActors[0].positionSequence, 0x80000004u);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[0], 12.5f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[1], -34.25f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[2], 56.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.rot[0], 0.125f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.rot[2], 1.25f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.pos[1], -1.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.rot[2], 0.5f);
        expectMovementTiming(received.baseActors[0]);
        EXPECT_TRUE(received.baseActors[0].hasPositionData);
    }

    TEST(MpBasePacketTest, actorPositionUsesUnreliableSequencedActorDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BaseActorList actorList;
        actorList.guid = testGuid();
        actorList.baseActors.emplace_back();

        mwmp::PacketActorPosition packet;
        packet.SetSendStream(&sendStream);
        packet.setActorList(&actorList);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::UnreliableSequenced);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_ACTOR);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, actorEquipmentRoundTripsSequenceAndItems)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.equipmentSequence = 42;
        actor.equipmentItems[0].refId = "iron dagger";
        actor.equipmentItems[0].count = 1;
        actor.equipmentItems[0].charge = 25;
        actor.equipmentItems[0].enchantmentCharge = -1.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorEquipment writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorEquipment reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        EXPECT_EQ(received.baseActors[0].refNum, 128u);
        EXPECT_EQ(received.baseActors[0].mpNum, 4u);
        EXPECT_EQ(received.baseActors[0].equipmentSequence, 42u);
        EXPECT_TRUE(received.baseActors[0].hasEquipmentData);
        EXPECT_EQ(received.baseActors[0].equipmentItems[0].refId, "iron dagger");
        EXPECT_EQ(received.baseActors[0].equipmentItems[0].count, 1);
        EXPECT_EQ(received.baseActors[0].equipmentItems[0].charge, 25);
        EXPECT_FLOAT_EQ(received.baseActors[0].equipmentItems[0].enchantmentCharge, -1.f);
    }

    TEST(MpBasePacketTest, actorEquipmentRejectsInvalidItemPayloads)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.equipmentItems[0].refId = "$dynamic_bad_equipment";
        actor.equipmentItems[0].count = 1;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorEquipment writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorEquipment reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseActors.empty());
    }

    TEST(MpBasePacketTest, actorStatsDynamicRoundTripsDeathState)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.statsDynamicSequence = 91;
        actor.creatureStats.mDead = true;
        actor.creatureStats.mDeathAnimationFinished = true;
        actor.creatureStats.mDynamic[0].mBase = 80.f;
        actor.creatureStats.mDynamic[0].mCurrent = 0.f;
        actor.creatureStats.mDynamic[1].mBase = 30.f;
        actor.creatureStats.mDynamic[1].mCurrent = 12.f;
        actor.creatureStats.mDynamic[2].mBase = 50.f;
        actor.creatureStats.mDynamic[2].mCurrent = 25.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorStatsDynamic writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorStatsDynamic reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        EXPECT_EQ(received.baseActors[0].refNum, 128u);
        EXPECT_EQ(received.baseActors[0].mpNum, 4u);
        EXPECT_TRUE(received.baseActors[0].hasStatsDynamicData);
        EXPECT_EQ(received.baseActors[0].statsDynamicSequence, 91u);
        EXPECT_TRUE(received.baseActors[0].creatureStats.mDead);
        EXPECT_TRUE(received.baseActors[0].creatureStats.mDeathAnimationFinished);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[0].mBase, 80.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[0].mCurrent, 0.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[1].mBase, 30.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[1].mCurrent, 12.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[2].mBase, 50.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].creatureStats.mDynamic[2].mCurrent, 25.f);
    }

    TEST(MpBasePacketTest, actorStatsDynamicRejectsNonFiniteValues)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.statsDynamicSequence = 92;
        actor.creatureStats.mDynamic[0].mBase = 80.f;
        actor.creatureStats.mDynamic[0].mCurrent = std::numeric_limits<float>::quiet_NaN();
        actor.creatureStats.mDynamic[1].mBase = 30.f;
        actor.creatureStats.mDynamic[1].mCurrent = 12.f;
        actor.creatureStats.mDynamic[2].mBase = 50.f;
        actor.creatureStats.mDynamic[2].mCurrent = 25.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorStatsDynamic writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorStatsDynamic reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseActors.empty());
    }

    TEST(MpBasePacketTest, clientActorStatsDynamicAuthorityAllowsFirstSnapshotAndDamageOnlyHealth)
    {
        mwmp::BaseActor stored;
        stored.refNum = 128;
        stored.mpNum = 4;
        stored.hasStatsDynamicData = true;
        stored.statsDynamicSequence = 8;
        stored.creatureStats.mDynamic[0].mBase = 100.f;
        stored.creatureStats.mDynamic[0].mCurrent = 50.f;
        stored.creatureStats.mDynamic[0].mMod = 4.f;
        stored.creatureStats.mDynamic[1].mBase = 40.f;
        stored.creatureStats.mDynamic[1].mCurrent = 20.f;
        stored.creatureStats.mDynamic[1].mMod = 2.f;
        stored.creatureStats.mDynamic[2].mBase = 70.f;
        stored.creatureStats.mDynamic[2].mCurrent = 35.f;
        stored.creatureStats.mDynamic[2].mMod = 3.f;

        mwmp::BaseActor firstSnapshot = stored;
        firstSnapshot.hasStatsDynamicData = false;
        firstSnapshot.statsDynamicSequence = 1;
        EXPECT_TRUE(mwmp::isClientActorStatsDynamicUpdateAllowed(nullptr, firstSnapshot));

        mwmp::BaseActor firstSnapshotDeadWithHealth = firstSnapshot;
        firstSnapshotDeadWithHealth.creatureStats.mDead = true;
        EXPECT_FALSE(mwmp::isClientActorStatsDynamicUpdateAllowed(nullptr, firstSnapshotDeadWithHealth));

        mwmp::BaseActor damaged = stored;
        damaged.statsDynamicSequence = 9;
        damaged.creatureStats.mDynamic[0].mBase = 999.f;
        damaged.creatureStats.mDynamic[0].mCurrent = 35.f;
        damaged.creatureStats.mDynamic[0].mMod = 999.f;
        damaged.creatureStats.mDynamic[1].mCurrent = 999.f;
        damaged.creatureStats.mDynamic[2].mCurrent = 888.f;
        EXPECT_TRUE(mwmp::isClientActorStatsDynamicUpdateAllowed(&stored, damaged));
        mwmp::normalizeClientActorStatsDynamicUpdate(&stored, damaged);
        EXPECT_FLOAT_EQ(damaged.creatureStats.mDynamic[0].mBase, 100.f);
        EXPECT_FLOAT_EQ(damaged.creatureStats.mDynamic[0].mCurrent, 35.f);
        EXPECT_FLOAT_EQ(damaged.creatureStats.mDynamic[0].mMod, 4.f);
        EXPECT_FLOAT_EQ(damaged.creatureStats.mDynamic[1].mCurrent, 20.f);
        EXPECT_FLOAT_EQ(damaged.creatureStats.mDynamic[2].mCurrent, 35.f);

        mwmp::BaseActor stale = damaged;
        stale.statsDynamicSequence = 8;
        stale.creatureStats.mDynamic[0].mCurrent = 25.f;
        EXPECT_FALSE(mwmp::isClientActorStatsDynamicUpdateAllowed(&stored, stale));

        mwmp::BaseActor healed = stored;
        healed.statsDynamicSequence = 10;
        healed.creatureStats.mDynamic[0].mCurrent = 60.f;
        EXPECT_FALSE(mwmp::isClientActorStatsDynamicUpdateAllowed(&stored, healed));

        mwmp::BaseActor positiveHealthDeadFlag = stored;
        positiveHealthDeadFlag.statsDynamicSequence = 11;
        positiveHealthDeadFlag.creatureStats.mDead = true;
        positiveHealthDeadFlag.creatureStats.mDynamic[0].mCurrent = 35.f;
        EXPECT_FALSE(mwmp::isClientActorStatsDynamicUpdateAllowed(&stored, positiveHealthDeadFlag));

        mwmp::BaseActor deadStored = stored;
        deadStored.creatureStats.mDead = true;
        deadStored.creatureStats.mDynamic[0].mCurrent = 0.f;
        mwmp::BaseActor revived = deadStored;
        revived.statsDynamicSequence = 12;
        revived.creatureStats.mDead = false;
        EXPECT_FALSE(mwmp::isClientActorStatsDynamicUpdateAllowed(&deadStored, revived));
    }

    TEST(MpBasePacketTest, clientActorDeathAuthorityRequiresAcceptedDeadStats)
    {
        mwmp::BaseActor stored;
        stored.refNum = 128;
        stored.mpNum = 4;
        stored.hasStatsDynamicData = true;
        stored.statsDynamicSequence = 100;
        stored.creatureStats.mDynamic[0].mCurrent = 40.f;
        stored.creatureStats.mDynamic[1].mCurrent = 12.f;
        stored.creatureStats.mDynamic[2].mCurrent = 25.f;

        mwmp::BaseActor death;
        death.refNum = stored.refNum;
        death.mpNum = stored.mpNum;
        death.statsDynamicSequence = stored.statsDynamicSequence;
        death.deathState = 29;

        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(nullptr, death));
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&stored, death));

        mwmp::BaseActor deadFlagWithoutZeroHealth = stored;
        deadFlagWithoutZeroHealth.creatureStats.mDead = true;
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&deadFlagWithoutZeroHealth, death));

        mwmp::BaseActor noStats = stored;
        noStats.hasStatsDynamicData = false;
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&noStats, death));

        mwmp::BaseActor zeroHealth = stored;
        zeroHealth.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&zeroHealth, death));

        mwmp::BaseActor dead = stored;
        dead.creatureStats.mDead = true;
        dead.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_TRUE(mwmp::isClientActorDeathUpdateAllowed(&dead, death));

        mwmp::BaseActor staleDeath = death;
        staleDeath.statsDynamicSequence = stored.statsDynamicSequence - 1;
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&dead, staleDeath));

        mwmp::BaseActor noDeathState = death;
        noDeathState.deathState = 0;
        EXPECT_FALSE(mwmp::isClientActorDeathUpdateAllowed(&dead, noDeathState));
    }

    TEST(MpBasePacketTest, clientActorControlAuthorityRejectsServerDeadActors)
    {
        EXPECT_FALSE(mwmp::isClientActorControlUpdateAllowed(nullptr));

        mwmp::BaseActor actor;
        actor.hasStatsDynamicData = false;
        actor.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_TRUE(mwmp::isClientActorControlUpdateAllowed(&actor));

        actor.hasStatsDynamicData = true;
        actor.creatureStats.mDynamic[0].mCurrent = 30.f;
        EXPECT_TRUE(mwmp::isClientActorControlUpdateAllowed(&actor));

        actor.creatureStats.mDynamic[0].mCurrent = 0.f;
        EXPECT_FALSE(mwmp::isClientActorControlUpdateAllowed(&actor));

        actor.creatureStats.mDynamic[0].mCurrent = 30.f;
        actor.creatureStats.mDead = true;
        EXPECT_FALSE(mwmp::isClientActorControlUpdateAllowed(&actor));
    }

    TEST(MpBasePacketTest, actorCellChangeRoundTripsSequencedMovementSnapshot)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        setInteriorCell(actor.cell, "Balmora");
        actor.cell.mData.mX = -3;
        actor.cell.mData.mY = 1;
        actor.positionSequence = 0x80000021u;
        actor.position.pos[0] = 128.f;
        actor.position.pos[1] = -256.f;
        actor.position.pos[2] = 64.f;
        actor.position.rot[2] = 1.875f;
        actor.direction.pos[0] = 0.5f;
        actor.direction.pos[1] = -0.25f;
        actor.direction.rot[2] = 0.125f;
        setMovementTiming(actor);
        actor.isFollowerCellChange = true;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorCellChange writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorCellChange reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_EQ(receivedActor.cell.mName, "Balmora");
        EXPECT_EQ(receivedActor.cell.mData.mX, -3);
        EXPECT_EQ(receivedActor.cell.mData.mY, 1);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000021u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], 128.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], -256.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 64.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], 1.875f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], 0.5f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], -0.25f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], 0.125f);
        expectMovementTiming(receivedActor);
        EXPECT_TRUE(receivedActor.isFollowerCellChange);
        EXPECT_TRUE(receivedActor.hasPositionData);
    }

    TEST(MpBasePacketTest, actorAnimPlayRoundTripsCombatTransformAndAnimation)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.combatSequence = 51;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000007u;
        actor.position.pos[0] = 14.f;
        actor.position.pos[1] = -28.f;
        actor.position.pos[2] = 42.f;
        actor.position.rot[2] = 0.625f;
        actor.direction.pos[0] = 0.25f;
        actor.direction.pos[1] = -0.25f;
        actor.direction.rot[2] = 0.75f;
        setMovementTiming(actor);
        actor.animation.groupname = "attack2";
        actor.animation.mode = 1;
        actor.animation.count = 2;
        actor.animation.persist = true;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAnimPlay writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAnimPlay reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_TRUE(receivedActor.hasCombatData);
        EXPECT_EQ(receivedActor.combatSequence, 51u);
        EXPECT_TRUE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000007u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], 14.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], -28.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 42.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], 0.625f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], 0.25f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], -0.25f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], 0.75f);
        expectMovementTiming(receivedActor);
        EXPECT_EQ(receivedActor.animation.groupname, "attack2");
        EXPECT_EQ(receivedActor.animation.mode, 1);
        EXPECT_EQ(receivedActor.animation.count, 2);
        EXPECT_TRUE(receivedActor.animation.persist);
    }

    TEST(MpBasePacketTest, actorMovementAnimationPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.positionSequence = 0x80000031u;
            actor.position.pos[0] = 14.f;
            actor.position.pos[1] = -28.f;
            actor.position.pos[2] = 42.f;
            actor.position.rot[2] = 0.625f;
            actor.direction.pos[0] = 0.25f;
            actor.direction.rot[2] = 0.75f;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorPosition>(sent, ID_ACTOR_POSITION);
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000032u;
            actor.position.pos[0] = 48.f;
            actor.position.pos[1] = -24.f;
            actor.position.pos[2] = 12.f;
            actor.direction.pos[1] = 0.25f;
            actor.animFlagsSequence = 0x80000005u;
            actor.movementFlags = 0x15;
            actor.drawState = 2;
            actor.isJumping = true;
            actor.isFlying = true;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorAnimFlags>(sent, ID_ACTOR_ANIM_FLAGS);
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000033u;
            actor.position.pos[0] = 16.f;
            actor.position.pos[1] = 32.f;
            actor.position.pos[2] = -64.f;
            actor.direction.pos[0] = 0.5f;
            actor.animation.groupname = "attack2";
            actor.animation.mode = 1;
            actor.animation.count = 2;
            actor.animation.persist = true;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorAnimPlay>(sent, ID_ACTOR_ANIM_PLAY);
        }
    }

    TEST(MpBasePacketTest, actorAttackRoundTripsCombatTransformAndHitState)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.combatSequence = 52;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000005u;
        actor.position.pos[0] = 10.f;
        actor.position.pos[1] = -20.f;
        actor.position.pos[2] = 30.f;
        actor.position.rot[2] = 1.5f;
        actor.direction.pos[0] = 1.f;
        actor.direction.pos[1] = -1.f;
        actor.direction.rot[2] = 0.25f;
        setMovementTiming(actor);
        actor.attack.target.isPlayer = false;
        actor.attack.target.refId = "rat";
        actor.attack.target.refNum = 777;
        actor.attack.target.mpNum = 2;
        actor.attack.type = mwmp::Attack::RANGED;
        actor.attack.pressed = true;
        actor.attack.success = true;
        actor.attack.isHit = true;
        actor.attack.attackStrength = 0.75f;
        actor.attack.rangedWeaponId = "iron bow";
        actor.attack.rangedAmmoId = "iron arrow";
        actor.attack.projectileOrigin.origin[0] = 1.f;
        actor.attack.projectileOrigin.origin[1] = 2.f;
        actor.attack.projectileOrigin.origin[2] = 3.f;
        actor.attack.projectileOrigin.orientation[3] = 1.f;
        actor.attack.damage = 12.f;
        actor.attack.knockdown = true;
        actor.attack.applyWeaponEnchantment = true;
        actor.attack.applyAmmoEnchantment = true;
        actor.attack.hitPosition.pos[0] = 4.f;
        actor.attack.hitPosition.pos[1] = 5.f;
        actor.attack.hitPosition.pos[2] = 6.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAttack writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAttack reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_TRUE(receivedActor.hasCombatData);
        EXPECT_EQ(receivedActor.combatSequence, 52u);
        EXPECT_TRUE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000005u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], 10.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], -20.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 30.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], 1.5f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], 1.f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], -1.f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], 0.25f);
        expectMovementTiming(receivedActor);
        EXPECT_EQ(receivedActor.attack.target.refId, "rat");
        EXPECT_EQ(receivedActor.attack.target.refNum, 777u);
        EXPECT_EQ(receivedActor.attack.target.mpNum, 2u);
        EXPECT_EQ(receivedActor.attack.type, mwmp::Attack::RANGED);
        EXPECT_TRUE(receivedActor.attack.pressed);
        EXPECT_TRUE(receivedActor.attack.success);
        EXPECT_TRUE(receivedActor.attack.isHit);
        EXPECT_FLOAT_EQ(receivedActor.attack.attackStrength, 0.75f);
        EXPECT_EQ(receivedActor.attack.rangedWeaponId, "iron bow");
        EXPECT_EQ(receivedActor.attack.rangedAmmoId, "iron arrow");
        EXPECT_FLOAT_EQ(receivedActor.attack.projectileOrigin.origin[0], 1.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.projectileOrigin.origin[1], 2.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.projectileOrigin.origin[2], 3.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.projectileOrigin.orientation[3], 1.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.damage, 12.f);
        EXPECT_TRUE(receivedActor.attack.knockdown);
        EXPECT_TRUE(receivedActor.attack.applyWeaponEnchantment);
        EXPECT_TRUE(receivedActor.attack.applyAmmoEnchantment);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[0], 4.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[1], 5.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[2], 6.f);
    }

    TEST(MpBasePacketTest, actorMeleeAttackRoundTripsWeaponContext)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.attack.target.isPlayer = false;
        actor.attack.target.refId = "rat";
        actor.attack.target.refNum = 777;
        actor.attack.target.mpNum = 2;
        actor.attack.type = mwmp::Attack::MELEE;
        actor.attack.pressed = false;
        actor.attack.success = true;
        actor.attack.isHit = true;
        actor.attack.attackAnimation = "slash";
        actor.attack.attackStrength = 0.625f;
        actor.attack.rangedWeaponId = "iron longsword";
        actor.attack.damage = 8.f;
        actor.attack.applyWeaponEnchantment = true;
        actor.attack.hitPosition.pos[0] = 4.f;
        actor.attack.hitPosition.pos[1] = 5.f;
        actor.attack.hitPosition.pos[2] = 6.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAttack writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAttack reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_EQ(receivedActor.attack.target.refId, "rat");
        EXPECT_EQ(receivedActor.attack.target.refNum, 777u);
        EXPECT_EQ(receivedActor.attack.target.mpNum, 2u);
        EXPECT_EQ(receivedActor.attack.type, mwmp::Attack::MELEE);
        EXPECT_FALSE(receivedActor.attack.pressed);
        EXPECT_TRUE(receivedActor.attack.success);
        EXPECT_TRUE(receivedActor.attack.isHit);
        EXPECT_EQ(receivedActor.attack.attackAnimation, "slash");
        EXPECT_FLOAT_EQ(receivedActor.attack.attackStrength, 0.625f);
        EXPECT_EQ(receivedActor.attack.rangedWeaponId, "iron longsword");
        EXPECT_FLOAT_EQ(receivedActor.attack.damage, 8.f);
        EXPECT_TRUE(receivedActor.attack.applyWeaponEnchantment);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[0], 4.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[1], 5.f);
        EXPECT_FLOAT_EQ(receivedActor.attack.hitPosition.pos[2], 6.f);
    }

    TEST(MpBasePacketTest, actorCastRoundTripsCombatTransformAndProjectileState)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 129;
        actor.mpNum = 5;
        actor.combatSequence = 53;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000006u;
        actor.position.pos[0] = -10.f;
        actor.position.pos[1] = 20.f;
        actor.position.pos[2] = 40.f;
        actor.position.rot[2] = -1.25f;
        actor.direction.pos[0] = -1.f;
        actor.direction.pos[1] = 1.f;
        actor.direction.rot[2] = -0.5f;
        setMovementTiming(actor);
        actor.cast.target.isPlayer = false;
        actor.cast.target.refId = "skeleton";
        actor.cast.target.refNum = 321;
        actor.cast.target.mpNum = 6;
        actor.cast.type = mwmp::Cast::REGULAR;
        actor.cast.pressed = true;
        actor.cast.success = true;
        actor.cast.instant = true;
        actor.cast.spellId = "firebite";
        actor.cast.hasProjectile = true;
        actor.cast.projectileOrigin.origin[0] = 7.f;
        actor.cast.projectileOrigin.origin[1] = 8.f;
        actor.cast.projectileOrigin.origin[2] = 9.f;
        actor.cast.projectileOrigin.orientation[3] = 1.f;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorCast writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorCast reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 129u);
        EXPECT_EQ(receivedActor.mpNum, 5u);
        EXPECT_TRUE(receivedActor.hasCombatData);
        EXPECT_EQ(receivedActor.combatSequence, 53u);
        EXPECT_TRUE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000006u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], -10.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], 20.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 40.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], -1.25f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], -1.f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], 1.f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], -0.5f);
        expectMovementTiming(receivedActor);
        EXPECT_EQ(receivedActor.cast.target.refId, "skeleton");
        EXPECT_EQ(receivedActor.cast.target.refNum, 321u);
        EXPECT_EQ(receivedActor.cast.target.mpNum, 6u);
        EXPECT_EQ(receivedActor.cast.type, mwmp::Cast::REGULAR);
        EXPECT_TRUE(receivedActor.cast.pressed);
        EXPECT_TRUE(receivedActor.cast.success);
        EXPECT_TRUE(receivedActor.cast.instant);
        EXPECT_EQ(receivedActor.cast.spellId, "firebite");
        EXPECT_TRUE(receivedActor.cast.hasProjectile);
        EXPECT_FLOAT_EQ(receivedActor.cast.projectileOrigin.origin[0], 7.f);
        EXPECT_FLOAT_EQ(receivedActor.cast.projectileOrigin.origin[1], 8.f);
        EXPECT_FLOAT_EQ(receivedActor.cast.projectileOrigin.origin[2], 9.f);
        EXPECT_FLOAT_EQ(receivedActor.cast.projectileOrigin.orientation[3], 1.f);
    }

    TEST(MpBasePacketTest, actorCombatSequenceRejectsStaleEvents)
    {
        mwmp::BaseActor stored;
        mwmp::BaseActor incoming;
        incoming.combatSequence = 100;

        ASSERT_TRUE(mwmp::isActorCombatSequenceAllowed(stored, incoming));
        mwmp::acceptActorCombatSequence(stored, incoming);
        EXPECT_TRUE(stored.hasCombatData);
        EXPECT_EQ(stored.combatSequence, 100u);

        incoming.combatSequence = 99;
        EXPECT_FALSE(mwmp::isActorCombatSequenceAllowed(stored, incoming));
        EXPECT_EQ(stored.combatSequence, 100u);

        incoming.combatSequence = 101;
        EXPECT_TRUE(mwmp::isActorCombatSequenceAllowed(stored, incoming));
        mwmp::acceptActorCombatSequence(stored, incoming);
        EXPECT_EQ(stored.combatSequence, 101u);
    }

    TEST(MpBasePacketTest, actorDeathRoundTripsCombatTransformAndKillerState)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refId = "rat";
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.statsDynamicSequence = 0x80000009u;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000008u;
        actor.position.pos[0] = 22.f;
        actor.position.pos[1] = -44.f;
        actor.position.pos[2] = 88.f;
        actor.position.rot[2] = 2.25f;
        actor.direction.pos[0] = -0.75f;
        actor.direction.pos[1] = 0.5f;
        actor.direction.rot[2] = -0.125f;
        setMovementTiming(actor);
        actor.deathState = 4;
        actor.isInstantDeath = true;
        actor.killer.isPlayer = false;
        actor.killer.refId = "skeleton";
        actor.killer.refNum = 321;
        actor.killer.mpNum = 6;
        actor.killer.name = "Bonewalker";
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorDeath writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorDeath reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refId, "rat");
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_EQ(receivedActor.statsDynamicSequence, 0x80000009u);
        EXPECT_TRUE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000008u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], 22.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], -44.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 88.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], 2.25f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], -0.75f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], 0.5f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], -0.125f);
        expectMovementTiming(receivedActor);
        EXPECT_EQ(receivedActor.deathState, 4);
        EXPECT_TRUE(receivedActor.isInstantDeath);
        EXPECT_FALSE(receivedActor.killer.isPlayer);
        EXPECT_EQ(receivedActor.killer.refId, "skeleton");
        EXPECT_EQ(receivedActor.killer.refNum, 321u);
        EXPECT_EQ(receivedActor.killer.mpNum, 6u);
        EXPECT_EQ(receivedActor.killer.name, "Bonewalker");
    }

    TEST(MpBasePacketTest, actorCombatEventPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000021u;
            actor.attack.target.isPlayer = false;
            actor.attack.target.refId = "rat";
            actor.attack.target.refNum = 777;
            actor.attack.target.mpNum = 2;
            actor.attack.type = mwmp::Attack::RANGED;
            actor.attack.pressed = false;
            actor.attack.success = true;
            actor.attack.isHit = true;
            actor.attack.attackStrength = 0.75f;
            actor.attack.rangedWeaponId = "iron bow";
            actor.attack.rangedAmmoId = "iron arrow";
            actor.attack.projectileOrigin.orientation[3] = 1.f;
            actor.attack.damage = 12.f;
            actor.attack.applyAmmoEnchantment = true;
            actor.attack.hitPosition.pos[2] = 6.f;
            sent.baseActors.push_back(actor);

            PacketStream fullStream;
            mwmp::PacketActorAttack writer;
            writer.setActorList(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_ACTOR_ATTACK);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorAttack reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 129;
            actor.mpNum = 5;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000022u;
            actor.cast.target.isPlayer = false;
            actor.cast.target.refId = "skeleton";
            actor.cast.target.refNum = 321;
            actor.cast.target.mpNum = 6;
            actor.cast.type = mwmp::Cast::REGULAR;
            actor.cast.pressed = true;
            actor.cast.success = true;
            actor.cast.instant = true;
            actor.cast.spellId = "firebite";
            actor.cast.hasProjectile = true;
            actor.cast.projectileOrigin.orientation[3] = 1.f;
            sent.baseActors.push_back(actor);

            PacketStream fullStream;
            mwmp::PacketActorCast writer;
            writer.setActorList(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_ACTOR_CAST);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorCast reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refId = "rat";
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000023u;
            actor.deathState = 4;
            actor.isInstantDeath = true;
            actor.killer.isPlayer = false;
            actor.killer.refId = "skeleton";
            actor.killer.refNum = 321;
            actor.killer.mpNum = 6;
            actor.killer.name = "Bonewalker";
            sent.baseActors.push_back(actor);

            PacketStream fullStream;
            mwmp::PacketActorDeath writer;
            writer.setActorList(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_ACTOR_DEATH);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorDeath reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }
    }

    TEST(MpBasePacketTest, actorCombatEventPacketsRejectInvalidValues)
    {
        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.attack.target.isPlayer = false;
            actor.attack.target.refId = "rat";
            actor.attack.target.refNum = 777;
            actor.attack.target.mpNum = 2;
            actor.attack.type = 99;
            actor.attack.isHit = true;
            actor.attack.damage = 12.f;
            actor.attack.hitPosition.pos[0] = 4.f;
            actor.attack.hitPosition.pos[1] = 5.f;
            actor.attack.hitPosition.pos[2] = 6.f;
            sent.baseActors.push_back(actor);

            PacketStream stream;
            mwmp::PacketActorAttack writer;
            writeActorPacketToPayload(writer, sent, stream);

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorAttack reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.attack.target.isPlayer = false;
            actor.attack.target.refId = "rat";
            actor.attack.target.refNum = 777;
            actor.attack.target.mpNum = 2;
            actor.attack.type = mwmp::Attack::RANGED;
            actor.attack.isHit = true;
            actor.attack.attackStrength = 0.75f;
            actor.attack.rangedWeaponId = "iron bow";
            actor.attack.rangedAmmoId = "iron arrow";
            actor.attack.projectileOrigin.origin[0] = std::numeric_limits<float>::quiet_NaN();
            actor.attack.projectileOrigin.orientation[3] = 1.f;
            actor.attack.damage = 12.f;
            actor.attack.hitPosition.pos[0] = 4.f;
            actor.attack.hitPosition.pos[1] = 5.f;
            actor.attack.hitPosition.pos[2] = 6.f;
            sent.baseActors.push_back(actor);

            PacketStream stream;
            mwmp::PacketActorAttack writer;
            writeActorPacketToPayload(writer, sent, stream);

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorAttack reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 129;
            actor.mpNum = 5;
            actor.cast.target.isPlayer = false;
            actor.cast.target.refId = "skeleton";
            actor.cast.target.refNum = 321;
            actor.cast.target.mpNum = 6;
            actor.cast.type = mwmp::Cast::REGULAR;
            actor.cast.pressed = false;
            actor.cast.success = true;
            actor.cast.instant = false;
            actor.cast.spellId = "";
            sent.baseActors.push_back(actor);

            PacketStream stream;
            mwmp::PacketActorCast writer;
            writeActorPacketToPayload(writer, sent, stream);

            mwmp::BaseActorList received;
            received.isValid = true;
            mwmp::PacketActorCast reader;
            reader.setActorList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseActors.empty());
        }
    }

    TEST(MpBasePacketTest, actorAiRoundTripsMovementSnapshotAndTargetPackage)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000014u;
        actor.position.pos[0] = -30.f;
        actor.position.pos[1] = 60.f;
        actor.position.pos[2] = 90.f;
        actor.position.rot[2] = -2.75f;
        actor.direction.pos[0] = 0.5f;
        actor.direction.pos[1] = 0.75f;
        actor.direction.rot[2] = 0.125f;
        setMovementTiming(actor);
        actor.aiAction = mwmp::BaseActorList::ESCORT;
        actor.aiDuration = 45;
        actor.aiCoordinates.pos[0] = 100.f;
        actor.aiCoordinates.pos[1] = 200.f;
        actor.aiCoordinates.pos[2] = 300.f;
        actor.hasAiTarget = true;
        actor.aiTarget.isPlayer = false;
        actor.aiTarget.refId = "guard";
        actor.aiTarget.refNum = 321;
        actor.aiTarget.mpNum = 6;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAI writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAI reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_TRUE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.positionSequence, 0x80000014u);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[0], -30.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[1], 60.f);
        EXPECT_FLOAT_EQ(receivedActor.position.pos[2], 90.f);
        EXPECT_FLOAT_EQ(receivedActor.position.rot[2], -2.75f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[0], 0.5f);
        EXPECT_FLOAT_EQ(receivedActor.direction.pos[1], 0.75f);
        EXPECT_FLOAT_EQ(receivedActor.direction.rot[2], 0.125f);
        expectMovementTiming(receivedActor);
        EXPECT_EQ(receivedActor.aiAction, static_cast<unsigned int>(mwmp::BaseActorList::ESCORT));
        EXPECT_EQ(receivedActor.aiDuration, 45u);
        EXPECT_FLOAT_EQ(receivedActor.aiCoordinates.pos[0], 100.f);
        EXPECT_FLOAT_EQ(receivedActor.aiCoordinates.pos[1], 200.f);
        EXPECT_FLOAT_EQ(receivedActor.aiCoordinates.pos[2], 300.f);
        EXPECT_TRUE(receivedActor.hasAiTarget);
        EXPECT_FALSE(receivedActor.aiTarget.isPlayer);
        EXPECT_EQ(receivedActor.aiTarget.refId, "guard");
        EXPECT_EQ(receivedActor.aiTarget.refNum, 321u);
        EXPECT_EQ(receivedActor.aiTarget.mpNum, 6u);
    }

    TEST(MpBasePacketTest, actorAiRoundTripsWithoutMovementSnapshot)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.aiAction = mwmp::BaseActorList::WANDER;
        actor.aiDistance = 512;
        actor.aiShouldRepeat = true;
        actor.aiDuration = 30;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAI writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAI reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        const mwmp::BaseActor& receivedActor = received.baseActors[0];
        EXPECT_EQ(receivedActor.refNum, 128u);
        EXPECT_EQ(receivedActor.mpNum, 4u);
        EXPECT_FALSE(receivedActor.hasPositionData);
        EXPECT_EQ(receivedActor.aiAction, static_cast<unsigned int>(mwmp::BaseActorList::WANDER));
        EXPECT_EQ(receivedActor.aiDistance, 512u);
        EXPECT_TRUE(receivedActor.aiShouldRepeat);
        EXPECT_EQ(receivedActor.aiDuration, 30u);
    }

    TEST(MpBasePacketTest, actorCellAiStatsPacketsRejectTruncatedPayloads)
    {
        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.creatureStats.mDead = true;
            actor.creatureStats.mDeathAnimationFinished = true;
            actor.creatureStats.mDynamic[0].mBase = 80.f;
            actor.creatureStats.mDynamic[0].mCurrent = 0.f;
            actor.creatureStats.mDynamic[1].mBase = 30.f;
            actor.creatureStats.mDynamic[1].mCurrent = 12.f;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorStatsDynamic>(sent, ID_ACTOR_STATS_DYNAMIC);
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            setInteriorCell(actor.cell, "Balmora");
            actor.cell.mData.mX = -3;
            actor.cell.mData.mY = 1;
            actor.positionSequence = 0x80000041u;
            actor.position.pos[0] = 128.f;
            actor.position.pos[1] = -256.f;
            actor.position.pos[2] = 64.f;
            actor.direction.pos[0] = 0.5f;
            actor.isFollowerCellChange = true;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorCellChange>(sent, ID_ACTOR_CELL_CHANGE);
        }

        {
            mwmp::BaseActorList sent;
            sent.guid = testGuid();
            setInteriorCell(sent.cell, "Seyda Neen");

            mwmp::BaseActor actor;
            actor.refNum = 128;
            actor.mpNum = 4;
            actor.hasPositionData = true;
            actor.positionSequence = 0x80000042u;
            actor.position.pos[0] = -30.f;
            actor.position.pos[1] = 60.f;
            actor.position.pos[2] = 90.f;
            actor.direction.pos[0] = 0.5f;
            actor.aiAction = mwmp::BaseActorList::ESCORT;
            actor.aiDuration = 45;
            actor.aiCoordinates.pos[0] = 100.f;
            actor.aiCoordinates.pos[1] = 200.f;
            actor.aiCoordinates.pos[2] = 300.f;
            actor.hasAiTarget = true;
            actor.aiTarget.isPlayer = false;
            actor.aiTarget.refId = "guard";
            actor.aiTarget.refNum = 321;
            actor.aiTarget.mpNum = 6;
            sent.baseActors.push_back(actor);

            expectTruncatedActorPacketInvalid<mwmp::PacketActorAI>(sent, ID_ACTOR_AI);
        }
    }

    TEST(MpBasePacketTest, actorAnimFlagsRoundTripsSequenceAndState)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.hasPositionData = true;
        actor.positionSequence = 0x80000012u;
        actor.position.pos[0] = 48.f;
        actor.position.pos[1] = -24.f;
        actor.position.pos[2] = 12.f;
        actor.position.rot[2] = 2.5f;
        actor.direction.pos[0] = -1.f;
        actor.direction.pos[1] = 0.25f;
        actor.direction.rot[2] = -0.75f;
        setMovementTiming(actor);
        actor.animFlagsSequence = 0x80000003u;
        actor.movementFlags = 0x15;
        actor.drawState = 2;
        actor.isJumping = true;
        actor.isFlying = true;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAnimFlags writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAnimFlags reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        EXPECT_EQ(received.baseActors[0].refNum, 128u);
        EXPECT_EQ(received.baseActors[0].mpNum, 4u);
        EXPECT_TRUE(received.baseActors[0].hasPositionData);
        EXPECT_EQ(received.baseActors[0].positionSequence, 0x80000012u);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[0], 48.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[1], -24.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.pos[2], 12.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].position.rot[2], 2.5f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.pos[0], -1.f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.pos[1], 0.25f);
        EXPECT_FLOAT_EQ(received.baseActors[0].direction.rot[2], -0.75f);
        expectMovementTiming(received.baseActors[0]);
        EXPECT_EQ(received.baseActors[0].animFlagsSequence, 0x80000003u);
        EXPECT_EQ(received.baseActors[0].movementFlags, 0x15u);
        EXPECT_EQ(received.baseActors[0].drawState, 2);
        EXPECT_TRUE(received.baseActors[0].isJumping);
        EXPECT_TRUE(received.baseActors[0].isFlying);
        EXPECT_TRUE(received.baseActors[0].hasAnimFlagsData);
    }

    TEST(MpBasePacketTest, actorAnimFlagsRoundTripsWithoutMovementSnapshot)
    {
        mwmp::BaseActorList sent;
        sent.guid = testGuid();
        setInteriorCell(sent.cell, "Seyda Neen");

        mwmp::BaseActor actor;
        actor.refNum = 128;
        actor.mpNum = 4;
        actor.animFlagsSequence = 0x80000004u;
        actor.movementFlags = 0x09;
        actor.drawState = 1;
        actor.isJumping = true;
        sent.baseActors.push_back(actor);

        PacketStream stream;
        mwmp::PacketActorAnimFlags writer;
        writeActorPacketToPayload(writer, sent, stream);

        mwmp::BaseActorList received;
        received.isValid = true;
        mwmp::PacketActorAnimFlags reader;
        reader.setActorList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseActors.size(), 1);
        EXPECT_EQ(received.baseActors[0].refNum, 128u);
        EXPECT_EQ(received.baseActors[0].mpNum, 4u);
        EXPECT_FALSE(received.baseActors[0].hasPositionData);
        EXPECT_EQ(received.baseActors[0].animFlagsSequence, 0x80000004u);
        EXPECT_EQ(received.baseActors[0].movementFlags, 0x09u);
        EXPECT_EQ(received.baseActors[0].drawState, 1);
        EXPECT_TRUE(received.baseActors[0].isJumping);
        EXPECT_FALSE(received.baseActors[0].isFlying);
        EXPECT_TRUE(received.baseActors[0].hasAnimFlagsData);
    }

    TEST(MpBasePacketTest, actorAnimFlagsMergeKeepsNewestMovementAndFlags)
    {
        mwmp::BaseActor accepted;
        accepted.refNum = 128;
        accepted.mpNum = 4;
        accepted.hasPositionData = true;
        accepted.positionSequence = 10;
        accepted.position.pos[0] = 10.f;
        accepted.hasAnimFlagsData = true;
        accepted.animFlagsSequence = 20;
        accepted.movementFlags = 1;
        accepted.drawState = 1;

        mwmp::BaseActor staleFlagsNewPosition = accepted;
        staleFlagsNewPosition.positionSequence = 11;
        staleFlagsNewPosition.position.pos[0] = 11.f;
        staleFlagsNewPosition.animFlagsSequence = 19;
        staleFlagsNewPosition.movementFlags = 99;
        staleFlagsNewPosition.drawState = 3;

        mwmp::mergeNewestActorAnimFlags(accepted, staleFlagsNewPosition);

        EXPECT_TRUE(accepted.hasPositionData);
        EXPECT_EQ(accepted.positionSequence, 11u);
        EXPECT_FLOAT_EQ(accepted.position.pos[0], 11.f);
        EXPECT_TRUE(accepted.hasAnimFlagsData);
        EXPECT_EQ(accepted.animFlagsSequence, 20u);
        EXPECT_EQ(accepted.movementFlags, 1u);
        EXPECT_EQ(accepted.drawState, 1);

        mwmp::BaseActor newerFlagsOldPosition = accepted;
        newerFlagsOldPosition.positionSequence = 10;
        newerFlagsOldPosition.position.pos[0] = 10.f;
        newerFlagsOldPosition.animFlagsSequence = 21;
        newerFlagsOldPosition.movementFlags = 7;
        newerFlagsOldPosition.drawState = 2;
        newerFlagsOldPosition.isJumping = true;
        newerFlagsOldPosition.isFlying = true;

        mwmp::mergeNewestActorAnimFlags(accepted, newerFlagsOldPosition);

        EXPECT_EQ(accepted.positionSequence, 11u);
        EXPECT_FLOAT_EQ(accepted.position.pos[0], 11.f);
        EXPECT_EQ(accepted.animFlagsSequence, 21u);
        EXPECT_EQ(accepted.movementFlags, 7u);
        EXPECT_EQ(accepted.drawState, 2);
        EXPECT_TRUE(accepted.isJumping);
        EXPECT_TRUE(accepted.isFlying);
    }

    TEST(MpBasePacketTest, baseActorDefaultsAreSafeForPartialPacketConstruction)
    {
        mwmp::BaseActor actor;

        EXPECT_EQ(actor.refNum, 0u);
        EXPECT_EQ(actor.mpNum, 0u);
        EXPECT_EQ(actor.positionSequence, 0u);
        EXPECT_EQ(actor.animFlagsSequence, 0u);
        EXPECT_EQ(actor.movementFlags, 0u);
        EXPECT_EQ(actor.drawState, 0);
        EXPECT_FALSE(actor.isJumping);
        EXPECT_FALSE(actor.isFlying);
        EXPECT_FALSE(actor.isFollowerCellChange);
        EXPECT_FALSE(actor.hasAiTarget);
        EXPECT_EQ(actor.aiAction, static_cast<unsigned int>(mwmp::BaseActorList::CANCEL));
        EXPECT_EQ(actor.aiDistance, 0u);
        EXPECT_EQ(actor.aiDuration, 0u);
        EXPECT_FALSE(actor.aiShouldRepeat);
        EXPECT_FALSE(actor.hasPositionData);
        EXPECT_FALSE(actor.hasAnimFlagsData);
        EXPECT_FALSE(actor.hasStatsDynamicData);
        EXPECT_FALSE(actor.hasEquipmentData);
        EXPECT_FALSE(actor.hasCombatData);
        EXPECT_EQ(actor.combatSequence, 0u);
        EXPECT_EQ(actor.equipmentSequence, 0u);
        EXPECT_FALSE(actor.creatureStats.mDead);
        EXPECT_FALSE(actor.creatureStats.mDeathAnimationFinished);
        EXPECT_EQ(actor.attack.type, mwmp::Attack::MELEE);
        EXPECT_FALSE(actor.attack.target.isPlayer);
        EXPECT_EQ(actor.attack.target.refNum, static_cast<unsigned int>(-1));
        EXPECT_EQ(actor.attack.target.mpNum, static_cast<unsigned int>(-1));
        EXPECT_EQ(actor.cast.type, mwmp::Cast::REGULAR);
        EXPECT_FALSE(actor.cast.shouldSend);
        EXPECT_EQ(actor.spellsActiveChanges.action, mwmp::SpellsActiveChanges::SET);
    }

    TEST(MpBasePacketTest, actorAnimFlagsUseUnreliableSequencedActorDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);

        PacketStream sendStream;
        mwmp::BaseActorList actorList;
        actorList.guid = testGuid();
        actorList.baseActors.emplace_back();

        mwmp::PacketActorAnimFlags packet;
        packet.SetSendStream(&sendStream);
        packet.setActorList(&actorList);

        EXPECT_EQ(packet.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentPriority, PacketPriority::High);
        EXPECT_EQ(transport.sentReliability, PacketReliability::UnreliableSequenced);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_ACTOR);
        EXPECT_EQ(transport.sentDestination.guid(), testGuid());
        EXPECT_FALSE(transport.sentBroadcast);
    }

    TEST(MpBasePacketTest, playerBaseInfoRoundTripsPlainRefIds)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.npc.mName = "alex";
        sent.npc.mModel = "base_anim.nif";
        sent.npc.mRace = ESM::RefId::stringRefId("dark elf");
        sent.npc.mHair = ESM::RefId::stringRefId("b_n_dark elf_m_hair_01");
        sent.npc.mHead = ESM::RefId::stringRefId("b_n_dark elf_m_head_01");
        sent.npc.mFlags = ESM::NPC::Female;
        sent.birthsign = "elfborn";
        sent.resetStats = true;

        PacketStream stream;
        mwmp::PacketPlayerBaseInfo writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerBaseInfo reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.npc.mName, "alex");
        EXPECT_EQ(received.npc.mModel, "base_anim.nif");
        EXPECT_EQ(received.npc.mRace, "dark elf");
        EXPECT_EQ(received.npc.mHair, "b_n_dark elf_m_hair_01");
        EXPECT_EQ(received.npc.mHead, "b_n_dark elf_m_head_01");
        EXPECT_EQ(received.npc.mFlags, ESM::NPC::Female);
        EXPECT_EQ(received.birthsign, "elfborn");
        EXPECT_TRUE(received.resetStats);
    }

    TEST(MpBasePacketTest, playerBootstrapStateUsesExpectedDelivery)
    {
        CapturingTransport transport;
        ScopedPacketTransport scopedTransport(&transport);
        PacketStream sendStream;
        mwmp::BasePlayer player(testGuid());
        player.exchangeFullInfo = true;

        mwmp::PacketPlayerBaseInfo baseInfoPacket;
        baseInfoPacket.SetSendStream(&sendStream);
        baseInfoPacket.setPlayer(&player);
        EXPECT_EQ(baseInfoPacket.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_PLAYER);

        mwmp::PacketPlayerAttribute attributePacket;
        attributePacket.SetSendStream(&sendStream);
        attributePacket.setPlayer(&player);
        EXPECT_EQ(attributePacket.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);

        mwmp::PacketPlayerSkill skillPacket;
        skillPacket.SetSendStream(&sendStream);
        skillPacket.setPlayer(&player);
        EXPECT_EQ(skillPacket.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);

        mwmp::PacketPlayerEquipment equipmentPacket;
        equipmentPacket.SetSendStream(&sendStream);
        equipmentPacket.setPlayer(&player);
        EXPECT_EQ(equipmentPacket.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_PLAYER);

        mwmp::PacketPlayerShapeshift shapeshiftPacket;
        shapeshiftPacket.SetSendStream(&sendStream);
        shapeshiftPacket.setPlayer(&player);
        EXPECT_EQ(shapeshiftPacket.Send(mwmp::PacketDestination(testGuid())), 1u);
        EXPECT_EQ(transport.sentReliability, PacketReliability::ReliableOrdered);
        EXPECT_EQ(transport.sentOrderChannel, CHANNEL_MOVEMENT);
    }

    TEST(MpBasePacketTest, playerDefaultClassRoundTripsPlainRefId)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.charClass.mId = ESM::RefId::stringRefId("acrobat");

        PacketStream stream;
        mwmp::PacketPlayerClass writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerClass reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(received.charClass.mId, "acrobat");
        EXPECT_TRUE(received.charClass.mName.empty());
    }

    TEST(MpBasePacketTest, playerCustomClassRoundTripsCompressedClassData)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.charClass.mId = ESM::RefId();
        sent.charClass.mName = "Ship Inspector";
        sent.charClass.mDescription = "Checks papers, crates, and suspicious moon sugar.";
        sent.charClass.mData = {};
        sent.charClass.mData.mAttribute = { 1, 2 };
        sent.charClass.mData.mSpecialization = ESM::Class::Stealth;
        sent.charClass.mData.mSkills[3][1] = 42;
        sent.charClass.mData.mIsPlayable = 1;
        sent.charClass.mData.mServices = 0x120;

        PacketStream stream;
        mwmp::PacketPlayerClass writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerClass reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_TRUE(received.charClass.mId.empty());
        EXPECT_EQ(received.charClass.mName, "Ship Inspector");
        EXPECT_EQ(received.charClass.mDescription, "Checks papers, crates, and suspicious moon sugar.");
        EXPECT_EQ(received.charClass.mData.mAttribute[0], 1);
        EXPECT_EQ(received.charClass.mData.mAttribute[1], 2);
        EXPECT_EQ(received.charClass.mData.mSpecialization, ESM::Class::Stealth);
        EXPECT_EQ(received.charClass.mData.mSkills[3][1], 42);
        EXPECT_EQ(received.charClass.mData.mIsPlayable, 1);
        EXPECT_EQ(received.charClass.mData.mServices, 0x120);
    }

    TEST(MpBasePacketTest, playerJournalRoundTripsEntriesIndexesAndTimestamps)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.journalChangesAreLoad = true;

        mwmp::JournalItem entry;
        entry.type = mwmp::JournalItem::ENTRY;
        entry.quest = "a1_1_findspymaster";
        entry.index = 10;
        entry.actorRefId = "caius cosades";
        entry.hasTimestamp = true;
        entry.timestamp.daysPassed = 17;
        entry.timestamp.month = 2;
        entry.timestamp.day = 5;
        sent.journalChanges.push_back(entry);

        mwmp::JournalItem index;
        index.type = mwmp::JournalItem::INDEX;
        index.quest = "a1_1_findspymaster";
        index.index = 15;
        sent.journalChanges.push_back(index);

        mwmp::JournalItem finished;
        finished.type = mwmp::JournalItem::FINISHED;
        finished.quest = "a1_1_findspymaster";
        finished.isFinished = true;
        sent.journalChanges.push_back(finished);

        PacketStream stream;
        mwmp::PacketPlayerJournal writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerJournal reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_TRUE(received.journalChangesAreLoad);
        ASSERT_EQ(received.journalChanges.size(), 3);

        EXPECT_EQ(received.journalChanges[0].type, mwmp::JournalItem::ENTRY);
        EXPECT_EQ(received.journalChanges[0].quest, "a1_1_findspymaster");
        EXPECT_EQ(received.journalChanges[0].index, 10);
        EXPECT_EQ(received.journalChanges[0].actorRefId, "caius cosades");
        EXPECT_TRUE(received.journalChanges[0].hasTimestamp);
        EXPECT_EQ(received.journalChanges[0].timestamp.daysPassed, 17);
        EXPECT_EQ(received.journalChanges[0].timestamp.month, 2);
        EXPECT_EQ(received.journalChanges[0].timestamp.day, 5);

        EXPECT_EQ(received.journalChanges[1].type, mwmp::JournalItem::INDEX);
        EXPECT_EQ(received.journalChanges[1].quest, "a1_1_findspymaster");
        EXPECT_EQ(received.journalChanges[1].index, 15);

        EXPECT_EQ(received.journalChanges[2].type, mwmp::JournalItem::FINISHED);
        EXPECT_EQ(received.journalChanges[2].quest, "a1_1_findspymaster");
        EXPECT_TRUE(received.journalChanges[2].isFinished);
    }

    TEST(MpBasePacketTest, playerBookRoundTripsLoadMarker)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.bookChangesAreLoad = true;
        sent.bookChanges.push_back({ "bk_a1_1_caiuspackage" });
        sent.bookChanges.push_back({ "bk_A1_1_DirectionsCaiusCosades" });

        PacketStream stream;
        mwmp::PacketPlayerBook writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerBook reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_TRUE(received.bookChangesAreLoad);
        ASSERT_EQ(received.bookChanges.size(), 2);
        EXPECT_EQ(received.bookChanges[0].bookId, "bk_a1_1_caiuspackage");
        EXPECT_EQ(received.bookChanges[1].bookId, "bk_A1_1_DirectionsCaiusCosades");
    }

    TEST(MpBasePacketTest, playerTopicRoundTripsLoadMarker)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.topicChangesAreLoad = true;
        sent.topicChanges.push_back({ "duties" });
        sent.topicChanges.push_back({ "caius cosades" });

        PacketStream stream;
        mwmp::PacketPlayerTopic writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerTopic reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_TRUE(received.topicChangesAreLoad);
        ASSERT_EQ(received.topicChanges.size(), 2);
        EXPECT_EQ(received.topicChanges[0].topicId, "duties");
        EXPECT_EQ(received.topicChanges[1].topicId, "caius cosades");
    }

    TEST(MpBasePacketTest, playerMiscellaneousRoundTripsSelectedEnchantedItem)
    {
        mwmp::BasePlayer sent(testGuid());
        sent.miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_ENCHANTED_ITEM;
        sent.selectedEnchantedItem = { "ring_firestorm", 2, 42, 11.5f, "golden saint" };

        PacketStream stream;
        mwmp::PacketPlayerMiscellaneous writer;
        writePlayerPacketToPayload(writer, sent, stream);

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerMiscellaneous reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.miscellaneousChangeType, mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_ENCHANTED_ITEM);
        EXPECT_EQ(received.selectedEnchantedItem.refId, "ring_firestorm");
        EXPECT_EQ(received.selectedEnchantedItem.count, 2);
        EXPECT_EQ(received.selectedEnchantedItem.charge, 42);
        EXPECT_FLOAT_EQ(received.selectedEnchantedItem.enchantmentCharge, 11.5f);
        EXPECT_EQ(received.selectedEnchantedItem.soul, "golden saint");
    }

    TEST(MpBasePacketTest, playerMiscellaneousRejectsInvalidSelectedEnchantedItemPayloads)
    {
        auto expectInvalidItem = [](mwmp::Item item) {
            mwmp::BasePlayer sent(testGuid());
            sent.miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_ENCHANTED_ITEM;
            sent.selectedEnchantedItem = std::move(item);

            PacketStream stream;
            mwmp::PacketPlayerMiscellaneous writer;
            writePlayerPacketToPayload(writer, sent, stream);

            mwmp::BasePlayer received(testGuid());
            mwmp::PacketPlayerMiscellaneous reader;
            reader.setPlayer(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
        };

        expectInvalidItem({ "$dynamic_bad_ring", 1, 10, 10.f, "" });
        expectInvalidItem({ "ring_firestorm", 0, 10, 10.f, "" });
        expectInvalidItem({ "ring_firestorm", 1000001, 10, 10.f, "" });
        expectInvalidItem({ "ring_firestorm", 1, 10, std::numeric_limits<float>::quiet_NaN(), "" });

        PacketStream stream;
        stream.Write(static_cast<unsigned int>(999));

        mwmp::BasePlayer received(testGuid());
        mwmp::PacketPlayerMiscellaneous reader;
        reader.setPlayer(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, preInitRoundTripsProtocolInfoAndChecksums)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { "Morrowind.esm", { 0x7B6AF5B9 } },
            { "Tribunal.esm", { 0xF481F334, 0x211329EF } },
        };

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums;
        mwmp::PacketPreInit reader;
        reader.setChecksums(&receivedChecksums);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(reader.getVersion(), "0.1.0");
        EXPECT_EQ(reader.getProtocolVersion(), 10);
        EXPECT_EQ(reader.getCommitHash(), "abcdef1234567890");
        ASSERT_EQ(receivedChecksums.size(), 2);
        EXPECT_EQ(receivedChecksums[0].first, "Morrowind.esm");
        ASSERT_EQ(receivedChecksums[0].second.size(), 1);
        EXPECT_EQ(receivedChecksums[0].second[0], 0x7B6AF5B9);
        EXPECT_EQ(receivedChecksums[1].first, "Tribunal.esm");
        ASSERT_EQ(receivedChecksums[1].second.size(), 2);
        EXPECT_EQ(receivedChecksums[1].second[0], 0xF481F334);
        EXPECT_EQ(receivedChecksums[1].second[1], 0x211329EF);
    }

    TEST(MpBasePacketTest, preInitEmptyChecksumResponseKeepsProtocolInfo)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums;

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums{ { "stale.esm", { 1 } } };
        mwmp::PacketPreInit reader;
        reader.setChecksums(&receivedChecksums);
        reader.Packet(&stream, false);

        ASSERT_TRUE(reader.isPacketValid());
        EXPECT_EQ(reader.getVersion(), "0.1.0");
        EXPECT_EQ(reader.getProtocolVersion(), 10);
        EXPECT_EQ(reader.getCommitHash(), "abcdef1234567890");
        EXPECT_TRUE(receivedChecksums.empty());
    }

    TEST(MpBasePacketTest, preInitRejectsMissingChecksumContainer)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { "Morrowind.esm", { 0x7B6AF5B9 } },
        };

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit reader;
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, preInitRejectsTooManyPlugins)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums;
        for (std::size_t index = 0; index < 1001; ++index)
            sentChecksums.emplace_back("plugin" + std::to_string(index) + ".esp", mwmp::PacketPreInit::HashList{ 1 });

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums;
        mwmp::PacketPreInit reader;
        reader.setChecksums(&receivedChecksums);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, preInitRejectsOversizedPluginName)
    {
        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { std::string(257, 'a'), { 0x7B6AF5B9 } },
        };

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums;
        mwmp::PacketPreInit reader;
        reader.setChecksums(&receivedChecksums);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, preInitRejectsTooManyHashesForPlugin)
    {
        mwmp::PacketPreInit::HashList hashes(51, 0x7B6AF5B9);
        mwmp::PacketPreInit::PluginContainer sentChecksums{
            { "Morrowind.esm", hashes },
        };

        PacketStream stream;
        mwmp::PacketPreInit writer;
        writer.setChecksums(&sentChecksums);
        writer.setProtocolVersionInfo("0.1.0", 10, "abcdef1234567890");
        writer.setGUID(testGuid());
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_GAME_PREINIT);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::PacketPreInit::PluginContainer receivedChecksums;
        mwmp::PacketPreInit reader;
        reader.setChecksums(&receivedChecksums);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
    }

    TEST(MpBasePacketTest, systemHandshakeRoundTripsAccountCredentials)
    {
        mwmp::BaseSystem sent(testGuid());
        sent.playerName = "alex";
        sent.serverPassword = "secret";
        sent.accountPasswordHash = "client-password-hash";

        PacketStream stream;
        mwmp::PacketSystemHandshake writer;
        writer.setSystem(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_SYSTEM_HANDSHAKE);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseSystem received(testGuid());
        mwmp::PacketSystemHandshake reader;
        reader.setSystem(&received);
        reader.Packet(&stream, false);

        EXPECT_TRUE(reader.isPacketValid());
        EXPECT_EQ(received.playerName, "alex");
        EXPECT_EQ(received.serverPassword, "secret");
        EXPECT_EQ(received.accountPasswordHash, "client-password-hash");
    }

    TEST(MpBasePacketTest, objectActivateRejectsTruncatedObjectAndActivatorPayloads)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::CLIENT_GAMEPLAY;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject target{};
        target.refId = "crate_01";
        target.refNum = 123;
        target.mpNum = 7;
        target.isPlayer = false;
        target.activatingActor.isPlayer = false;
        target.activatingActor.refId = "rat";
        target.activatingActor.refNum = 456;
        target.activatingActor.mpNum = 9;
        target.activatingActor.name = "Rat";
        sent.baseObjects.push_back(target);

        PacketStream fullStream;
        mwmp::PacketObjectActivate writer;
        writer.setObjectList(&sent);
        writer.Packet(&fullStream, true);

        ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
        PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_OBJECT_ACTIVATE);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketObjectActivate reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(reader.isPacketValid());
        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseObjects.empty());
    }

    TEST(MpBasePacketTest, customObjectPacketsRejectTruncatedPayloadsWithoutAppendingObjects)
    {
        {
            mwmp::BaseObjectList sent(testGuid());
            sent.packetOrigin = mwmp::CLIENT_GAMEPLAY;
            sent.cell.mName = "Balmora";
            sent.consoleCommand = "disable";
            sent.isValid = true;

            mwmp::BaseObject target{};
            target.refId = "crate_01";
            target.refNum = 123;
            target.mpNum = 7;
            target.isPlayer = false;
            sent.baseObjects.push_back(target);

            expectTruncatedObjectPacketInvalid<mwmp::PacketConsoleCommand>(sent, ID_CONSOLE_COMMAND);
        }

        {
            mwmp::BaseObjectList sent(testGuid());
            sent.packetOrigin = mwmp::CLIENT_GAMEPLAY;
            sent.cell.mName = "Balmora";
            sent.isValid = true;

            mwmp::BaseObject target{};
            target.refId = "crate_01";
            target.refNum = 123;
            target.mpNum = 7;
            target.isPlayer = false;
            target.soundId = "Item Misc Up";
            target.volume = 1.f;
            target.pitch = 0.9f;
            sent.baseObjects.push_back(target);

            expectTruncatedObjectPacketInvalid<mwmp::PacketObjectSound>(sent, ID_OBJECT_SOUND);
        }

        {
            mwmp::BaseObjectList sent(testGuid());
            sent.packetOrigin = mwmp::CLIENT_GAMEPLAY;
            sent.cell.mName = "Balmora";
            sent.isValid = true;

            mwmp::BaseObject target{};
            target.refId = "crate_01";
            target.refNum = 123;
            target.mpNum = 7;
            target.isPlayer = false;
            target.hittingActor.isPlayer = false;
            target.hittingActor.refId = "rat";
            target.hittingActor.refNum = 456;
            target.hittingActor.mpNum = 9;
            target.hittingActor.name = "Rat";
            target.hitAttack.success = true;
            target.hitAttack.damage = 4.f;
            target.hitAttack.block = false;
            target.hitAttack.knockdown = false;
            sent.baseObjects.push_back(target);

            expectTruncatedObjectPacketInvalid<mwmp::PacketObjectHit>(sent, ID_OBJECT_HIT);
        }
    }

    TEST(MpBasePacketTest, containerPacketRoundTripsServerMpNumWithOriginalRefNum)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::SET;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.cell.mName = "Wilderness (-3, -9)";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.refId = "mudcrab";
        container.refNum = 243;
        container.mpNum = 63;

        mwmp::ContainerItem item{};
        item.refId = "ingred_crab_meat_01";
        item.count = 1;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        item.actionCount = 0;
        container.containerItems.push_back(item);
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        EXPECT_EQ(received.packetOrigin, mwmp::SERVER_SCRIPT);
        EXPECT_EQ(received.action, mwmp::BaseObjectList::SET);
        EXPECT_EQ(received.containerSubAction, mwmp::BaseObjectList::NONE);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].refId, "mudcrab");
        EXPECT_EQ(received.baseObjects[0].refNum, 243);
        EXPECT_EQ(received.baseObjects[0].mpNum, 63);
        ASSERT_EQ(received.baseObjects[0].containerItems.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItems[0].refId, "ingred_crab_meat_01");
    }

    TEST(MpBasePacketTest, containerRequestAllowsObjectTargetWithoutRefId)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::REQUEST;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.refNum = 297461;
        container.mpNum = 0;
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        EXPECT_EQ(received.action, mwmp::BaseObjectList::REQUEST);
        EXPECT_EQ(received.containerSubAction, mwmp::BaseObjectList::NONE);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].refId, "");
        EXPECT_EQ(received.baseObjects[0].refNum, 297461);
        EXPECT_EQ(received.baseObjects[0].mpNum, 0);
    }

    TEST(MpBasePacketTest, containerPacketAcceptsServerMpNumWithoutRefId)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::REMOVE;
        sent.containerSubAction = mwmp::BaseObjectList::DRAG;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.mpNum = 63;

        mwmp::ContainerItem item{};
        item.refId = "ingred_crab_meat_01";
        item.count = 1;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        item.actionCount = 1;
        container.containerItems.push_back(item);
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].refId, "");
        EXPECT_EQ(received.baseObjects[0].refNum, 0);
        EXPECT_EQ(received.baseObjects[0].mpNum, 63);
    }

    TEST(MpBasePacketTest, containerPacketAcceptsLocalRefNumWithoutRefId)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::REMOVE;
        sent.containerSubAction = mwmp::BaseObjectList::TAKE_ALL;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.refNum = 297461;

        mwmp::ContainerItem item{};
        item.refId = "moon_sugar";
        item.count = 3;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        item.actionCount = 3;
        container.containerItems.push_back(item);
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].refId, "");
        EXPECT_EQ(received.baseObjects[0].refNum, 297461);
        EXPECT_EQ(received.baseObjects[0].mpNum, 0);
    }

    TEST(MpBasePacketTest, containerRequestRejectsAttachedItemMutations)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::REQUEST;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.refNum = 297461;

        mwmp::ContainerItem item{};
        item.refId = "moon_sugar";
        item.count = 1;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        item.actionCount = 1;
        container.containerItems.push_back(item);
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseObjects.empty());
    }

    TEST(MpBasePacketTest, containerPacketSkipsInvalidItemMutationsWithoutRejectingValidItems)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::REMOVE;
        sent.containerSubAction = mwmp::BaseObjectList::DRAG;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.mpNum = 63;

        mwmp::ContainerItem valid{};
        valid.refId = "ingred_crab_meat_01";
        valid.count = 3;
        valid.charge = -1;
        valid.enchantmentCharge = -1;
        valid.soul = "";
        valid.actionCount = 1;
        container.containerItems.push_back(valid);

        mwmp::ContainerItem zeroActionCount = valid;
        zeroActionCount.refId = "moon_sugar";
        zeroActionCount.actionCount = 0;
        container.containerItems.push_back(zeroActionCount);

        mwmp::ContainerItem dynamicRecord = valid;
        dynamicRecord.refId = "$dynamic_bad_container_item";
        dynamicRecord.actionCount = 1;
        container.containerItems.push_back(dynamicRecord);

        mwmp::ContainerItem nonFiniteCharge = valid;
        nonFiniteCharge.refId = "chitin dagger";
        nonFiniteCharge.enchantmentCharge = std::numeric_limits<double>::infinity();
        container.containerItems.push_back(nonFiniteCharge);

        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItemCount, 1u);
        ASSERT_EQ(received.baseObjects[0].containerItems.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItems[0].refId, "ingred_crab_meat_01");
        EXPECT_EQ(received.baseObjects[0].containerItems[0].actionCount, 1);
    }

    TEST(MpBasePacketTest, containerPacketSkipsImpossibleItemCountsWithoutRejectingValidItems)
    {
        constexpr int maxContainerItemStackCount = 1000000;

        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::ADD;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        container.mpNum = 63;

        mwmp::ContainerItem valid{};
        valid.refId = "ingred_crab_meat_01";
        valid.count = maxContainerItemStackCount;
        valid.charge = -1;
        valid.enchantmentCharge = -1;
        valid.soul = "";
        valid.actionCount = 0;
        container.containerItems.push_back(valid);

        mwmp::ContainerItem impossibleCount = valid;
        impossibleCount.refId = "moon_sugar";
        impossibleCount.count = maxContainerItemStackCount + 1;
        container.containerItems.push_back(impossibleCount);

        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItemCount, 1u);
        ASSERT_EQ(received.baseObjects[0].containerItems.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItems[0].refId, "ingred_crab_meat_01");
        EXPECT_EQ(received.baseObjects[0].containerItems[0].count, maxContainerItemStackCount);

        sent.action = mwmp::BaseObjectList::REMOVE;
        sent.baseObjects.clear();
        container.containerItems.clear();

        mwmp::ContainerItem validRemoval = valid;
        validRemoval.count = 1;
        validRemoval.actionCount = maxContainerItemStackCount;
        container.containerItems.push_back(validRemoval);

        mwmp::ContainerItem impossibleRemoval = validRemoval;
        impossibleRemoval.refId = "moon_sugar";
        impossibleRemoval.actionCount = maxContainerItemStackCount + 1;
        container.containerItems.push_back(impossibleRemoval);

        sent.baseObjects.push_back(container);

        stream.Reset();
        writer.Packet(&stream, true);

        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        received.baseObjects.clear();
        received.isValid = true;
        reader.Packet(&stream, false);

        ASSERT_TRUE(received.isValid);
        ASSERT_EQ(received.baseObjects.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItemCount, 1u);
        ASSERT_EQ(received.baseObjects[0].containerItems.size(), 1);
        EXPECT_EQ(received.baseObjects[0].containerItems[0].refId, "ingred_crab_meat_01");
        EXPECT_EQ(received.baseObjects[0].containerItems[0].actionCount, maxContainerItemStackCount);
    }

    TEST(MpBasePacketTest, containerPacketRejectsTruncatedHeadersAndItems)
    {
        {
            PacketStream stream;
            stream.Write(static_cast<unsigned char>(mwmp::SERVER_SCRIPT));

            mwmp::BaseObjectList received(testGuid());
            received.isValid = true;
            mwmp::PacketContainer reader;
            reader.setObjectList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseObjects.empty());
        }

        {
            mwmp::BaseObjectList sent(testGuid());
            sent.packetOrigin = mwmp::SERVER_SCRIPT;
            sent.action = mwmp::BaseObjectList::REMOVE;
            sent.containerSubAction = mwmp::BaseObjectList::DRAG;
            sent.cell.mName = "Seyda Neen";
            sent.isValid = true;

            mwmp::BaseObject container{};
            container.mpNum = 63;

            mwmp::ContainerItem item{};
            item.refId = "ingred_crab_meat_01";
            item.count = 3;
            item.charge = -1;
            item.enchantmentCharge = -1;
            item.soul = "";
            item.actionCount = 1;
            container.containerItems.push_back(item);
            sent.baseObjects.push_back(container);

            PacketStream fullStream;
            mwmp::PacketContainer writer;
            writer.setObjectList(&sent);
            writer.Packet(&fullStream, true);

            ASSERT_GT(fullStream.size(), mwmp::BasePacket::headerSize() + 1u);
            PacketStream stream(fullStream.data(), static_cast<unsigned int>(fullStream.size() - 1));

            unsigned char packetId = 0;
            stream.ResetReadPointer();
            ASSERT_TRUE(stream.Read(packetId));
            EXPECT_EQ(packetId, ID_CONTAINER);
            stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

            mwmp::BaseObjectList received(testGuid());
            received.isValid = true;
            mwmp::PacketContainer reader;
            reader.setObjectList(&received);
            reader.Packet(&stream, false);

            EXPECT_FALSE(reader.isPacketValid());
            EXPECT_FALSE(received.isValid);
            EXPECT_TRUE(received.baseObjects.empty());
        }
    }

    TEST(MpBasePacketTest, containerPacketRejectsInvalidActionAndSubActionValues)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = 255;
        sent.containerSubAction = 255;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;
        sent.baseObjects.emplace_back();

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(received.isValid);
        EXPECT_TRUE(received.baseObjects.empty());
    }

    TEST(MpBasePacketTest, nonRequestContainerRejectsObjectTargetWithoutAnyIdentity)
    {
        mwmp::BaseObjectList sent(testGuid());
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::SET;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.cell.mName = "Seyda Neen";
        sent.isValid = true;

        mwmp::BaseObject container{};
        sent.baseObjects.push_back(container);

        PacketStream stream;
        mwmp::PacketContainer writer;
        writer.setObjectList(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_CONTAINER);
        stream.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));

        mwmp::BaseObjectList received(testGuid());
        received.isValid = true;
        mwmp::PacketContainer reader;
        reader.setObjectList(&received);
        reader.Packet(&stream, false);

        EXPECT_FALSE(received.isValid);
    }

    TEST(MpBasePacketTest, containerAuthorityRejectsServerOriginSnapshotsAndRequestsFromClients)
    {
        mwmp::BaseObjectList objectList(testGuid());
        objectList.packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList.action = mwmp::BaseObjectList::REMOVE;
        objectList.containerSubAction = mwmp::BaseObjectList::DRAG;

        EXPECT_TRUE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.action = mwmp::BaseObjectList::ADD;
        objectList.containerSubAction = mwmp::BaseObjectList::NONE;
        EXPECT_TRUE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.action = mwmp::BaseObjectList::SET;
        objectList.containerSubAction = mwmp::BaseObjectList::REPLY_TO_REQUEST;
        EXPECT_TRUE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.containerSubAction = mwmp::BaseObjectList::NONE;
        EXPECT_FALSE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.action = mwmp::BaseObjectList::REQUEST;
        objectList.containerSubAction = mwmp::BaseObjectList::NONE;
        objectList.baseObjects.clear();
        EXPECT_FALSE(mwmp::isContainerPacketAllowedFromClient(objectList));

        mwmp::BaseObject lockedContainer{};
        lockedContainer.refNum = 297461;
        lockedContainer.containerItemCount = 0;
        objectList.baseObjects.push_back(lockedContainer);
        objectList.containerSubAction = mwmp::BaseObjectList::LOCK_REQUEST;
        EXPECT_TRUE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.containerSubAction = mwmp::BaseObjectList::LOCK_RELEASE;
        EXPECT_TRUE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.baseObjects[0].containerItemCount = 1;
        objectList.baseObjects[0].containerItems.emplace_back();
        EXPECT_FALSE(mwmp::isContainerPacketAllowedFromClient(objectList));

        objectList.action = mwmp::BaseObjectList::REMOVE;
        objectList.containerSubAction = mwmp::BaseObjectList::DRAG;
        objectList.baseObjects.clear();
        objectList.packetOrigin = mwmp::SERVER_SCRIPT;
        EXPECT_FALSE(mwmp::isContainerPacketAllowedFromClient(objectList));
    }

    QueryData makeMasterServerData(const char* name = "CommunityMP")
    {
        QueryData data;
        data.SetName(name);
        data.SetVersion("0.1.0");
        data.SetPlayers(2);
        data.SetMaxPlayers(64);
        data.SetGameMode("Roleplay");
        data.SetPassword(1);
        data.rules["difficulty"].type = ServerRule::Type::number;
        data.rules["difficulty"].val = 2.5;
        data.rules["motd"].type = ServerRule::Type::string;
        data.rules["motd"].str = "Welcome";
        data.players = { "Alex", "Tester" };
        data.plugins = { { "Morrowind.esm", 0x7B6AF5B9 }, { "Tribunal.esm", 0xF481F334 } };
        return data;
    }

    void expectMasterServerData(const QueryData& received, const char* name = "CommunityMP")
    {
        EXPECT_EQ(std::string(received.GetName()), name);
        EXPECT_EQ(std::string(received.GetVersion()), "0.1.0");
        EXPECT_EQ(received.GetPlayers(), 2);
        EXPECT_EQ(received.GetMaxPlayers(), 64);
        EXPECT_EQ(std::string(received.GetGameMode()), "Roleplay");
        EXPECT_EQ(received.GetPassword(), 1);
        ASSERT_EQ(received.players.size(), 2);
        EXPECT_EQ(received.players[0], "Alex");
        EXPECT_EQ(received.players[1], "Tester");
        ASSERT_EQ(received.plugins.size(), 2);
        EXPECT_EQ(received.plugins[0].name, "Morrowind.esm");
        EXPECT_EQ(received.plugins[0].hash, 0x7B6AF5B9);
        EXPECT_EQ(received.plugins[1].name, "Tribunal.esm");
        EXPECT_EQ(received.plugins[1].hash, 0xF481F334);
        ASSERT_TRUE(received.rules.contains("difficulty"));
        EXPECT_EQ(received.rules.at("difficulty").type, ServerRule::Type::number);
        EXPECT_EQ(received.rules.at("difficulty").val, 2.5);
        ASSERT_TRUE(received.rules.contains("motd"));
        EXPECT_EQ(received.rules.at("motd").type, ServerRule::Type::string);
        EXPECT_EQ(received.rules.at("motd").str, "Welcome");
    }

    TEST(MpBasePacketTest, masterAnnounceRoundTripsServerDataAndAdvertisedPort)
    {
        QueryData sent = makeMasterServerData();

        PacketStream stream;
        mwmp::PacketMasterAnnounce writer;
        writer.SetServer(&sent);
        writer.SetFunc(mwmp::PacketMasterAnnounce::FUNCTION_ANNOUNCE);
        writer.SetAdvertisedPort(25565);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_MASTER_ANNOUNCE);

        QueryData received;
        mwmp::PacketMasterAnnounce reader;
        reader.SetServer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(reader.GetFunc(), mwmp::PacketMasterAnnounce::FUNCTION_ANNOUNCE);
        EXPECT_EQ(reader.GetAdvertisedPort(), 25565);
        expectMasterServerData(received);
    }

    TEST(MpBasePacketTest, masterQueryRoundTripsListedServersWithDetailPayloads)
    {
        std::map<mwmp::PacketAddress, QueryData> sentServers;
        const mwmp::PacketAddress firstAddress = mwmp::makePacketAddress("127.0.0.1", 25565);
        const mwmp::PacketAddress secondAddress = mwmp::makePacketAddress("192.0.2.10", 25566);
        sentServers.emplace(firstAddress, makeMasterServerData("CommunityMP"));
        sentServers.emplace(secondAddress, makeMasterServerData("TES3MP Remote"));

        PacketStream stream;
        mwmp::PacketMasterQuery writer;
        writer.SetServers(&sentServers);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_MASTER_QUERY);

        std::map<mwmp::PacketAddress, QueryData> receivedServers;
        mwmp::PacketMasterQuery reader;
        reader.SetServers(&receivedServers);
        reader.Packet(&stream, false);

        ASSERT_EQ(receivedServers.size(), 2);
        ASSERT_TRUE(receivedServers.contains(firstAddress));
        ASSERT_TRUE(receivedServers.contains(secondAddress));
        expectMasterServerData(receivedServers.at(firstAddress), "CommunityMP");
        expectMasterServerData(receivedServers.at(secondAddress), "TES3MP Remote");
    }

    TEST(MpBasePacketTest, masterUpdateRoundTripsSingleServerDetails)
    {
        const mwmp::PacketAddress sentAddress = mwmp::makePacketAddress("198.51.100.20", 25570);
        std::pair<mwmp::PacketAddress, QueryData> sent(sentAddress, makeMasterServerData("TES3MP Details"));

        PacketStream stream;
        mwmp::PacketMasterUpdate writer;
        writer.SetServer(&sent);
        writer.Packet(&stream, true);

        unsigned char packetId = 0;
        stream.ResetReadPointer();
        ASSERT_TRUE(stream.Read(packetId));
        EXPECT_EQ(packetId, ID_MASTER_UPDATE);

        std::pair<mwmp::PacketAddress, QueryData> received;
        mwmp::PacketMasterUpdate reader;
        reader.SetServer(&received);
        reader.Packet(&stream, false);

        EXPECT_EQ(mwmp::packetAddressToString(received.first, false), mwmp::packetAddressToString(sentAddress, false));
        EXPECT_EQ(mwmp::packetAddressPort(received.first), mwmp::packetAddressPort(sentAddress));
        expectMasterServerData(received.second, "TES3MP Details");
    }

    TEST(MpServerPasswordTest, emptyConfiguredPasswordUsesDefaultUnpasswordedSentinel)
    {
        EXPECT_EQ(mwmp::normalizeServerPassword(""), TES3MP_DEFAULT_PASSW);
        EXPECT_EQ(mwmp::normalizeServerPassword("secret"), "secret");
        EXPECT_FALSE(mwmp::isServerPassworded(mwmp::normalizeServerPassword("")));
    }

    TEST(MpServerPasswordTest, passwordedServerAcceptsOnlyMatchingClientPassword)
    {
        EXPECT_EQ(mwmp::validateServerPassword("secret", "secret"), mwmp::ServerPasswordValidation::Accepted);
        EXPECT_EQ(mwmp::validateServerPassword("secret", "wrong"), mwmp::ServerPasswordValidation::Rejected);
        EXPECT_EQ(mwmp::validateServerPassword("secret", TES3MP_DEFAULT_PASSW), mwmp::ServerPasswordValidation::Rejected);
    }

    TEST(MpServerPasswordTest, unpasswordedServerToleratesExtraClientPassword)
    {
        EXPECT_EQ(mwmp::validateServerPassword(TES3MP_DEFAULT_PASSW, TES3MP_DEFAULT_PASSW),
            mwmp::ServerPasswordValidation::Accepted);
        EXPECT_EQ(mwmp::validateServerPassword(TES3MP_DEFAULT_PASSW, "client-entered-password"),
            mwmp::ServerPasswordValidation::AcceptedWithExtraClientPassword);
    }
}
