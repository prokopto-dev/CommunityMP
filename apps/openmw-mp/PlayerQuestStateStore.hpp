#ifndef OPENMW_MP_PLAYERQUESTSTATESTORE_HPP
#define OPENMW_MP_PLAYERQUESTSTATESTORE_HPP

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

class Player;

namespace mwmp
{
    class PlayerQuestStateStore
    {
    public:
        static PlayerQuestStateStore& get();

        void applyJournalChanges(::Player& player);
        void applyTopicChanges(::Player& player);
        void applyBookChanges(::Player& player);
        void clearPlayer(PacketGuid guid);

    private:
        PlayerQuestStateStore() = default;
    };
}

#endif // OPENMW_MP_PLAYERQUESTSTATESTORE_HPP
