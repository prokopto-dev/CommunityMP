#ifndef OPENMW_MP_PLAYERQUESTSTATESTORE_HPP
#define OPENMW_MP_PLAYERQUESTSTATESTORE_HPP

#include <optional>
#include <string_view>

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
        std::optional<int> getJournalIndex(PacketGuid guid, std::string_view sourceQuestId) const;
        bool hasTopic(PacketGuid guid, std::string_view topicId) const;
        bool hasReadBook(PacketGuid guid, std::string_view bookId) const;
        void clearPlayer(PacketGuid guid);

    private:
        PlayerQuestStateStore() = default;
    };
}

#endif // OPENMW_MP_PLAYERQUESTSTATESTORE_HPP
