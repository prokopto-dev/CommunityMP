#ifndef OPENMW_MP_PACKETDESTINATION_HPP
#define OPENMW_MP_PACKETDESTINATION_HPP

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

namespace mwmp
{
    class PacketDestination
    {
    public:
        PacketDestination() = default;

        PacketDestination(const PacketAddress& address)
            : mAddress(address)
        {
        }

        PacketDestination(PacketGuid guid)
            : mGuid(guid)
        {
        }

        PacketDestination(PacketGuid guid, const PacketAddress& address)
            : mGuid(guid)
            , mAddress(address)
        {
        }

        PacketGuid guid() const { return mGuid; }
        const PacketAddress& address() const { return mAddress; }
        bool hasGuid() const { return isPacketGuidAssigned(mGuid); }
        bool hasAddress() const { return isPacketAddressAssigned(mAddress); }
        bool empty() const { return !hasGuid() && !hasAddress(); }

    private:
        PacketGuid mGuid = unassignedPacketGuid();
        PacketAddress mAddress = unassignedPacketAddress();
    };
}

#endif
