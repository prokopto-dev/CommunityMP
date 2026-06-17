#ifndef OPENMW_MP_COMMUNITYMPLUAEVENTSENDER_HPP
#define OPENMW_MP_COMMUNITYMPLUAEVENTSENDER_HPP

#include <string>
#include <string_view>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

class Player;

namespace mwmp
{
    class CommunityMpLuaEventSender
    {
    public:
        static bool isValidEventName(std::string_view value, std::size_t maxLength);
        static bool sendToPlayer(
            Player& player, std::string namespaceName, std::string eventName, std::string payloadJson);
        static void clearPlayer(PacketGuid guid);
    };
}

#endif // OPENMW_MP_COMMUNITYMPLUAEVENTSENDER_HPP
