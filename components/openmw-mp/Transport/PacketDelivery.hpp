#ifndef OPENMW_MP_PACKETDELIVERY_HPP
#define OPENMW_MP_PACKETDELIVERY_HPP

namespace mwmp
{
    enum class PacketPriority
    {
        Immediate,
        High,
        Medium,
        Low
    };

    enum class PacketReliability
    {
        Unreliable,
        UnreliableSequenced,
        UnreliableWithAckReceipt,
        Reliable,
        ReliableOrdered,
        ReliableOrderedWithAckReceipt
    };

}

#endif
