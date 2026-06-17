#ifndef OPENMW_BASECLIENTPACKETPROCESSOR_HPP
#define OPENMW_BASECLIENTPACKETPROCESSOR_HPP

#include <components/openmw-mp/Base/BasePacketProcessor.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include "../LocalPlayer.hpp"
#include "../DedicatedPlayer.hpp"

namespace mwmp
{
    class BaseClientPacketProcessor
    {
    public:
        static void SetServerAddr(PacketAddress addr)
        {
            serverAddr = addr;
        }

    protected:
        inline bool isRequest()
        {
            return request;
        }

        inline bool isLocal()
        {
            return guid == myGuid;
        }

        LocalPlayer *getLocalPlayer();

    protected:
        static PacketGuid guid, myGuid;
        static PacketAddress serverAddr;

        static bool request;
    };
}

#endif //OPENMW_BASECLIENTPACKETPROCESSOR_HPP

