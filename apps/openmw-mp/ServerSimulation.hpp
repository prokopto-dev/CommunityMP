#ifndef OPENMW_MP_SERVERSIMULATION_HPP
#define OPENMW_MP_SERVERSIMULATION_HPP

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

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

        std::map<PacketGuid, PlayerMovementState> mPlayerMovementStates;
        std::map<PacketGuid, ESM::Cell> mPlayerAcceptedCells;
        std::map<ActorMovementKey, PlayerMovementState> mActorMovementStates;
        std::map<ActorMovementKey, ActorWanderState> mActorWanderStates;
        std::unique_ptr<SimulationRuntime> mRuntime;
        Clock::time_point mLastTick;
        float mActorTickAccumulator = 0.f;

        static float clampDeltaSeconds(float seconds);
        bool canAuthoritativelySimulateActors() const;
        void tickActors(float deltaSeconds);
    };
}

#endif // OPENMW_MP_SERVERSIMULATION_HPP
