#ifndef OPENMW_MP_COMMUNITYMPCLIENTLUAEVENTHANDLER_HPP
#define OPENMW_MP_COMMUNITYMPCLIENTLUAEVENTHANDLER_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

class Player;

namespace mwmp
{
    struct CommunityMpPlayerObservation
    {
        std::uint16_t schemaVersion = 0;
        std::uint32_t sequence = 0;
        std::string kind;
        std::string objectId;
        std::string cellKey;
        std::string cellId;
        std::string cellName;
        std::string cellDisplayName;
        std::string cellRegion;
        std::string worldSpaceId;
        double simulationTime = 0.0;
        double gridX = 0.0;
        double gridY = 0.0;
        double positionX = 0.0;
        double positionY = 0.0;
        double positionZ = 0.0;
        std::chrono::steady_clock::time_point receivedAt;
        bool isExterior = false;
        bool hasGrid = false;
        bool hasPosition = false;
    };

    struct CommunityMpClientStateObservation
    {
        std::uint16_t schemaVersion = 0;
        std::uint32_t sequence = 0;
        std::string kind;
        std::string objectId;
        double simulationTime = 0.0;
        int releaseQuestStage = 0;
        std::chrono::steady_clock::time_point receivedAt;
        bool releaseJournalRecovered = false;
        bool releaseTopicsApplied = false;
    };

    class CommunityMpClientLuaEventHandler
    {
    public:
        static bool handlePlayerEvent(Player& player);
        static std::optional<CommunityMpPlayerObservation> getLatestLocationObservation(PacketGuid guid);
        static std::optional<CommunityMpClientStateObservation> getLatestStateObservation(PacketGuid guid);
        static std::optional<CommunityMpPlayerObservation> getLatestObservation(PacketGuid guid);
        static void clearPlayer(PacketGuid guid);
    };
}

#endif // OPENMW_MP_COMMUNITYMPCLIENTLUAEVENTHANDLER_HPP
