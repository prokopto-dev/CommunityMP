#ifndef OPENMW_PACKETMASTERANNOUNCE_HPP
#define OPENMW_PACKETMASTERANNOUNCE_HPP

#include <cstdint>

#include "../Packets/BasePacket.hpp"
#include "MasterData.hpp"

namespace mwmp
{
    class ProxyMasterPacket;
    class PacketMasterAnnounce : public BasePacket
    {
        friend class ProxyMasterPacket;
    public:
        PacketMasterAnnounce();

        void Packet(PacketStream *newBitstream, bool send) override;

        void SetServer(QueryData *server);
        void SetFunc(uint32_t keep);
        int GetFunc();
        void SetAdvertisedPort(uint16_t port);
        uint16_t GetAdvertisedPort() const;

        enum Func
        {
            FUNCTION_DELETE = 0,
            FUNCTION_ANNOUNCE,
            FUNCTION_KEEP
        };
    private:
        QueryData *server;
        uint32_t func;
        uint16_t advertisedPort;
    };
}

#endif //OPENMW_PACKETMASTERANNOUNCE_HPP
