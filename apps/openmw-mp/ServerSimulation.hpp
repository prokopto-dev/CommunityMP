#ifndef OPENMW_MP_SERVERSIMULATION_HPP
#define OPENMW_MP_SERVERSIMULATION_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include "SimulationRuntime.hpp"

class Cell;
class Player;

namespace mwmp
{
    class ActorPacket;
    class BaseActorList;
    class PlayerPacket;

    struct ActorWanderState
    {
        ESM::Position origin;
        ESM::Position destination;
        float remainingDecisionSeconds = 0.f;
        std::uint32_t decisionSequence = 0;
        bool hasOrigin = false;
        bool hasDestination = false;
    };

    class ServerSimulation
    {
    public:
        ServerSimulation();

        void tick();
        void removePlayer(PacketGuid guid);
        void noteCellLoadedByPlayer(unsigned short playerId, std::string cellDescription);
        void noteCellUnloadedByPlayer(unsigned short playerId, std::string cellDescription);
        void auditShadowCellAuthority(const std::string& cellDescription, const char* context) const;
        std::optional<PacketGuid> getShadowCellAuthority(const std::string& cellDescription) const;
        std::size_t getShadowCellVisitorCount(const std::string& cellDescription) const;
        void sendLuaBridgeState(::Player& player) const;

        bool acceptActorAttacks(BaseActorList& actorList, ::Cell& serverCell);
        bool acceptActorCasts(BaseActorList& actorList, ::Cell& serverCell);
        bool acceptPlayerCast(::Player& caster);
        bool acceptActorAiSnapshot(BaseActorList& actorList, ::Cell& serverCell);
        void applyPlayerAttack(::Player& attacker);
        bool acceptActorMovementSnapshot(ActorPacket& packet, BaseActorList& actorList, ::Cell& serverCell);
        bool acceptPlayerCellChange(::Player& player, PlayerPacket& packet);
        bool acceptPlayerMovementSnapshot(::Player& player, PlayerPacket& packet);
        bool acceptServerAuthoredPlayerState(::Player& player, bool cellChangePacket = false);
        bool isRedundantServerAuthoredPosition(const ::Player& player) const;
        void sendPlayerVisualStateToLoaded(::Player& player, PlayerPacket& packet);
        SimulationRuntime& runtime();
        const SimulationRuntime& runtime() const;

    private:
        using Clock = std::chrono::steady_clock;

        struct PlayerMovementState
        {
            Clock::time_point lastMovementPacket;
            Clock::time_point lastServerCellChangePacket;
            ESM::Position lastServerCellChangePosition;
            ESM::Position lastServerCellChangeDirection;
            std::uint32_t lastServerCellChangePositionSequence = 0;
            ESM::Position lastVisualPosition;
            std::uint32_t lastVisualPositionSequence = 0;
            bool hasServerCellChangePacket = false;
            bool hasVisualPosition = false;
        };

        struct ActorMovementKey
        {
            std::string cellKey;
            unsigned int refNum = 0;
            unsigned int mpNum = 0;

            friend bool operator<(const ActorMovementKey& left, const ActorMovementKey& right)
            {
                if (left.cellKey != right.cellKey)
                    return left.cellKey < right.cellKey;

                if (left.refNum != right.refNum)
                    return left.refNum < right.refNum;

                return left.mpNum < right.mpNum;
            }
        };

        struct ShadowCellAuthorityState
        {
            std::vector<PacketGuid> visitors;
            PacketGuid authority = unassignedPacketGuid();
        };

        std::map<PacketGuid, PlayerMovementState> mPlayerMovementStates;
        std::map<PacketGuid, ESM::Cell> mPlayerAcceptedCells;
        std::map<ActorMovementKey, PlayerMovementState> mActorMovementStates;
        std::map<ActorMovementKey, ActorWanderState> mActorWanderStates;
        std::map<std::string, ShadowCellAuthorityState> mShadowCellAuthority;
        std::unique_ptr<SimulationRuntime> mRuntime;
        Clock::time_point mLastTick;
        float mActorTickAccumulator = 0.f;

        static float clampDeltaSeconds(float seconds);
        bool canAuthoritativelySimulateActors() const;
        bool isShadowCellAuthorityCandidate(const ShadowCellAuthorityState& state, PacketGuid guid) const;
        PacketGuid getLowestPingShadowCellAuthority(
            const ShadowCellAuthorityState& state, PacketGuid excludedGuid = unassignedPacketGuid()) const;
        PacketGuid refreshShadowCellAuthority(const std::string& cellDescription, ShadowCellAuthorityState& state,
            const char* reason, PacketGuid preferredGuid = unassignedPacketGuid(),
            PacketGuid excludedGuid = unassignedPacketGuid());
        bool applyShadowCellAuthorityToLiveCell(const std::string& cellDescription,
            const ShadowCellAuthorityState& state, bool forceBroadcast = false) const;
        bool requestActorListSnapshotFromAuthority(const std::string& cellDescription,
            const ShadowCellAuthorityState& state, Cell& liveCell, const char* reason) const;
        void sendShadowCellAuthorityEvent(Player& player, const std::string& cellDescription,
            const ShadowCellAuthorityState& state) const;
        void broadcastShadowCellAuthorityEvent(
            const std::string& cellDescription, const ShadowCellAuthorityState& state) const;
        bool updateCellSimulationInterest(const std::string& cellDescription,
            const ShadowCellAuthorityState& state) const;
        void sendCellActivityEvent(Player& player, const std::string& cellDescription,
            const ShadowCellAuthorityState& state, bool localPlayerLoaded) const;
        void broadcastCellActivityEvent(const std::string& cellDescription,
            const ShadowCellAuthorityState& state) const;
        void sendRuntimeStatusEvent(Player& player) const;
        void sendLuaBridgeReadyEvent(Player& player) const;
        void tickActors(float deltaSeconds);
    };
}

#endif // OPENMW_MP_SERVERSIMULATION_HPP
