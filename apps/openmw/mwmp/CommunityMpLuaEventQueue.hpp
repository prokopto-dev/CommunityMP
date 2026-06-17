#ifndef OPENMW_COMMUNITYMPLUAEVENTQUEUE_HPP
#define OPENMW_COMMUNITYMPLUAEVENTQUEUE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

namespace mwmp
{
    struct CommunityMpLuaEvent
    {
        std::uint16_t schemaVersion = clientLuaEventSchemaVersion;
        std::uint32_t sequence = 0;
        std::string namespaceName;
        std::string eventName;
        std::string payload;
        PacketGuid subjectGuid = unassignedPacketGuid();
        bool subjectIsLocal = false;
    };

    class CommunityMpLuaEventQueue
    {
    public:
        static void pushServerEvent(const BasePlayer& player, bool subjectIsLocal);
        static std::vector<CommunityMpLuaEvent> takeServerEvents();
        static void clearServerEvents();
    };
}

#endif // OPENMW_COMMUNITYMPLUAEVENTQUEUE_HPP
