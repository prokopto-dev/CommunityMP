#ifndef OPENMW_MP_PACKETTRANSPORT_HPP
#define OPENMW_MP_PACKETTRANSPORT_HPP

#include <cstddef>
#include <cstdint>

#include <components/openmw-mp/Transport/PacketDelivery.hpp>
#include <components/openmw-mp/Transport/PacketDestination.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

namespace mwmp
{
    class PacketTransport
    {
    public:
        virtual ~PacketTransport() = default;

        virtual uint32_t send(const unsigned char* data, std::size_t length, PacketPriority priority,
            PacketReliability reliability, int8_t orderChannel, const PacketDestination& destination, bool broadcast)
            = 0;

        virtual ReceivedPacket* receive() = 0;
        virtual void deallocatePacket(ReceivedPacket* packet) = 0;
        virtual void closeConnection(const PacketDestination& destination, bool sendNotification) = 0;
        virtual void banAddress(const char* ipAddress) = 0;
        virtual void unbanAddress(const char* ipAddress) = 0;

        virtual PacketAddress getPacketAddress(PacketGuid guid) const = 0;
        virtual PacketGuid getMyGuid() const = 0;
        virtual unsigned short numberOfConnections() const = 0;
        virtual unsigned int maxConnections() const = 0;
        virtual int averagePing(const PacketDestination& destination) const = 0;
        virtual unsigned short port() const = 0;
    };
}

#endif
