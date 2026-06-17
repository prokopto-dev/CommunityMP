#ifndef OPENMW_MP_RECEIVEDPACKET_HPP
#define OPENMW_MP_RECEIVEDPACKET_HPP

#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketDestination.hpp>

#include <utility>
#include <vector>

namespace mwmp
{
    class ReceivedPacket
    {
    public:
        ReceivedPacket(std::vector<unsigned char> data, PacketGuid guid, const PacketAddress& address)
            : mData(std::move(data))
            , mGuid(guid)
            , mAddress(address)
        {
        }

        ReceivedPacket(const ReceivedPacket&) = delete;
        ReceivedPacket& operator=(const ReceivedPacket&) = delete;

        PacketId id() const { return !mData.empty() ? mData[0] : 0; }
        unsigned char* data() const { return !mData.empty() ? const_cast<unsigned char*>(mData.data()) : nullptr; }
        unsigned int length() const { return static_cast<unsigned int>(mData.size()); }
        PacketGuid guid() const { return mGuid; }
        const PacketAddress& address() const { return mAddress; }
        PacketDestination destination() const { return PacketDestination(mGuid, mAddress); }

    private:
        std::vector<unsigned char> mData;
        PacketGuid mGuid = unassignedPacketGuid();
        PacketAddress mAddress = unassignedPacketAddress();
    };
}

#endif
