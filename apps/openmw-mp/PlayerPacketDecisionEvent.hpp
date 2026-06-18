#ifndef OPENMW_MP_PLAYERPACKETDECISIONEVENT_HPP
#define OPENMW_MP_PLAYERPACKETDECISIONEVENT_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

class Player;

namespace mwmp
{
    struct PlayerPacketDecisionEvent
    {
        std::string_view packetName;
        std::string_view reason;
        bool accepted = false;
        bool corrected = false;
        std::uint32_t attemptedSequence = 0;
        std::uint32_t authoritativeSequence = 0;
        std::uint32_t acceptedSequence = 0;
        int attemptedAction = -1;
        int authoritativeAction = -1;
        std::size_t attemptedItemCount = 0;
        std::size_t authoritativeItemCount = 0;
        std::size_t attemptedChangedSlotCount = 0;
        std::size_t authoritativeChangedSlotCount = 0;
        bool exchangeFullInfo = false;
        bool loadSnapshot = false;
    };

    void sendPlayerPacketDecisionEvent(::Player& player, const PlayerPacketDecisionEvent& event);
}

#endif // OPENMW_MP_PLAYERPACKETDECISIONEVENT_HPP
