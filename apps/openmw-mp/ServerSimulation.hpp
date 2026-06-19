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
    class BaseActor;
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

    struct ActorPathgridRouteState
    {
        ESM::Position destination;
        std::vector<ESM::Position> waypoints;
        std::size_t nextWaypointIndex = 0;
        bool hasDestination = false;
        bool routeBlocked = false;
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
        bool rejectClientActorPacketForServerOwnedCell(
            const ::Cell* serverCell, const BaseActorList& actorList, const char* packetName);
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
            ESM::Cell cell;
            bool hasCell = false;
        };

        struct RuntimeActorSnapshotStats
        {
            std::uint64_t snapshotBatchCount = 0;
            std::uint64_t snapshotCellCount = 0;
            std::uint64_t snapshotActorCount = 0;
            std::uint64_t rejectedClientActorMovementPackets = 0;
            std::uint64_t rejectedClientActorAiPackets = 0;
            std::uint64_t rejectedClientActorAttackPackets = 0;
            std::uint64_t rejectedClientActorCastPackets = 0;
            std::uint64_t rejectedClientActorAuthorityPackets = 0;
            std::uint64_t fallbackMovementActivationCount = 0;
            std::uint64_t fallbackMovementResumeCount = 0;
            std::uint64_t fallbackMovementSuppressedSnapshotCount = 0;
            std::uint64_t visualDirectionClearedCount = 0;
            std::uint64_t visualDirectionDerivedCount = 0;
            std::uint64_t redundantPositionSnapshotSuppressedCount = 0;
            std::uint64_t redundantAnimFlagsSnapshotSuppressedCount = 0;
            std::uint64_t rawMovementIntentSnapshotCount = 0;
            std::uint64_t transformDeltaSnapshotCount = 0;
            std::uint64_t rawMovementIntentWithoutTransformCount = 0;
            std::string lastSnapshotCellDescription;
            std::string lastRejectedClientActorPacket;
            std::string lastRejectedClientActorCellDescription;
            std::size_t lastSnapshotActorCount = 0;
            std::string lastFallbackMovementCellKey;
            unsigned int lastFallbackMovementRefNum = 0;
            unsigned int lastFallbackMovementMpNum = 0;
            std::string lastIntentWithoutTransformCellKey;
            unsigned int lastIntentWithoutTransformRefNum = 0;
            unsigned int lastIntentWithoutTransformMpNum = 0;
        };

        struct RuntimeActorMovementState
        {
            ESM::Position lastRuntimePosition;
            std::uint32_t stagnantSnapshotCount = 0;
            bool hasRuntimePosition = false;
            bool useFallbackMovement = false;
        };

        struct RuntimeFocusSelectionStats
        {
            std::size_t candidateCellCount = 0;
            std::size_t directFocusCellCount = 0;
            std::size_t deferredLoadedCellCount = 0;
            std::size_t scriptFocusCellCount = 0;
            std::size_t scriptFocusWithoutPositionCellCount = 0;
            std::size_t authorityOnlyCellCount = 0;
            std::size_t staleSimulationInterestCellCount = 0;
            std::size_t currentPlayerCellCount = 0;
            std::size_t repairedCurrentPlayerCellCount = 0;
            std::string lastDeferredLoadedCellDescription;
            std::string lastAuthorityOnlyCellDescription;
            std::string lastStaleSimulationInterestCellDescription;
            std::string lastRepairedCurrentPlayerCellDescription;
        };

        std::map<PacketGuid, PlayerMovementState> mPlayerMovementStates;
        std::map<PacketGuid, ESM::Cell> mPlayerAcceptedCells;
        std::map<ActorMovementKey, PlayerMovementState> mActorMovementStates;
        std::map<ActorMovementKey, ActorWanderState> mActorWanderStates;
        std::map<ActorMovementKey, ActorPathgridRouteState> mActorPathgridRouteStates;
        std::map<ActorMovementKey, RuntimeActorMovementState> mRuntimeActorMovementStates;
        std::map<std::string, ShadowCellAuthorityState> mShadowCellAuthority;
        RuntimeActorSnapshotStats mRuntimeActorSnapshotStats;
        RuntimeFocusSelectionStats mRuntimeFocusSelectionStats;
        std::unique_ptr<SimulationRuntime> mRuntime;
        Clock::time_point mLastTick;
        Clock::time_point mLastRuntimeActorMovementHealthLog;
        std::uint64_t mLastLoggedRawMovementIntentWithoutTransformCount = 0;
        float mActorTickAccumulator = 0.f;

        static float clampDeltaSeconds(float seconds);
        bool canAuthoritativelySimulateActors() const;
        bool runtimeOwnsActorCell(const Cell& serverCell) const;
        bool isShadowCellAuthorityCandidate(const ShadowCellAuthorityState& state, PacketGuid guid) const;
        PacketGuid getLowestPingShadowCellAuthority(
            const ShadowCellAuthorityState& state, PacketGuid excludedGuid = unassignedPacketGuid()) const;
        PacketGuid refreshShadowCellAuthority(const std::string& cellDescription, ShadowCellAuthorityState& state,
            const char* reason, PacketGuid preferredGuid = unassignedPacketGuid(),
            PacketGuid excludedGuid = unassignedPacketGuid());
        bool clearLiveCellActorAuthority(const std::string& cellDescription, const char* reason) const;
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
        bool ensurePlayerCurrentSimulationCell(Player& player, const char* reason);
        void reconcileCurrentPlayerSimulationCells(RuntimeFocusSelectionStats& focusSelectionStats);
        void updateRuntimeSimulationCells();
        void applyRuntimeActorSnapshots(const std::vector<BaseActorList>& actorLists, float deltaSeconds);
        void logRuntimeActorMovementHealthIfNeeded(Clock::time_point now);
        bool shouldUseRuntimeFallbackMovement(const ActorMovementKey& actorKey,
            const BaseActor& runtimeActor, const BaseActor* cachedActor);
        void sendCellActivityEvent(Player& player, const std::string& cellDescription,
            const ShadowCellAuthorityState& state, bool localPlayerLoaded) const;
        void broadcastCellActivityEvent(const std::string& cellDescription,
            const ShadowCellAuthorityState& state) const;
        void sendRuntimeStatusEvent(Player& player) const;
        void sendLuaBridgeReadyEvent(Player& player) const;
        void tickActors(float deltaSeconds, bool runtimeFallbackOnly = false);
    };
}

#endif // OPENMW_MP_SERVERSIMULATION_HPP
