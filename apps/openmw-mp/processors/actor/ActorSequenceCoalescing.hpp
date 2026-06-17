#ifndef OPENMW_PROCESSOR_ACTOR_SEQUENCE_COALESCING_HPP
#define OPENMW_PROCESSOR_ACTOR_SEQUENCE_COALESCING_HPP

#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include <components/openmw-mp/Base/ActorStatsAuthority.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>

#include "../../Cell.hpp"

namespace mwmp
{
    using ActorIdentityKey = std::pair<unsigned int, unsigned int>;

    inline bool isFiniteActorPosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2])
            && std::isfinite(position.rot[0]) && std::isfinite(position.rot[1]) && std::isfinite(position.rot[2]);
    }

    inline bool isFiniteActorMovementSnapshot(const BaseActor& actor)
    {
        return isFiniteActorPosition(actor.position) && isFiniteActorPosition(actor.direction);
    }

    inline void acceptNewestPositionActor(std::vector<BaseActor>& acceptedActors,
        std::map<ActorIdentityKey, std::size_t>& acceptedActorIndexes, const BaseActor& actor)
    {
        const ActorIdentityKey key{ actor.refNum, actor.mpNum };
        auto found = acceptedActorIndexes.find(key);
        if (found == acceptedActorIndexes.end())
        {
            acceptedActorIndexes.emplace(key, acceptedActors.size());
            acceptedActors.push_back(actor);
            return;
        }

        BaseActor& acceptedActor = acceptedActors[found->second];
        if (isNewerPositionSequence(actor.positionSequence, acceptedActor.positionSequence))
            acceptedActor = actor;
    }

    inline void acceptNewestAnimFlagsActor(std::vector<BaseActor>& acceptedActors,
        std::map<ActorIdentityKey, std::size_t>& acceptedActorIndexes, const BaseActor& actor)
    {
        const ActorIdentityKey key{ actor.refNum, actor.mpNum };
        auto found = acceptedActorIndexes.find(key);
        if (found == acceptedActorIndexes.end())
        {
            acceptedActorIndexes.emplace(key, acceptedActors.size());
            acceptedActors.push_back(actor);
            return;
        }

        BaseActor& acceptedActor = acceptedActors[found->second];
        mergeNewestActorAnimFlags(acceptedActor, actor);
    }

    inline void normalizeActorMovementSnapshot(Cell* serverCell, BaseActor& actor)
    {
        if (actor.hasPositionData && !isFiniteActorMovementSnapshot(actor))
            actor.hasPositionData = false;

        if (serverCell == nullptr || !serverCell->containsActor(actor.refNum, actor.mpNum))
            return;

        BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
        if (currentActor == nullptr)
            return;

        if (actor.hasPositionData && (!currentActor->hasPositionData
                || isNewerPositionSequence(actor.positionSequence, currentActor->positionSequence)))
        {
            currentActor->hasPositionData = true;
            currentActor->positionSequence = actor.positionSequence;
            currentActor->position = actor.position;
            currentActor->direction = actor.direction;
            currentActor->movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                actor.movementSampleIntervalSeconds);
            currentActor->movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(actor.movementLatencySeconds);
        }
        else if (currentActor->hasPositionData)
        {
            actor.hasPositionData = true;
            actor.positionSequence = currentActor->positionSequence;
            actor.position = currentActor->position;
            actor.direction = currentActor->direction;
            actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                currentActor->movementSampleIntervalSeconds);
            actor.movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(currentActor->movementLatencySeconds);
        }
    }

    inline bool filterActorListToKnownActors(Cell* serverCell, BaseActorList& actorList, bool normalizeMovement)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            if (!serverCell->containsActor(actor.refNum, actor.mpNum))
                continue;

            if (normalizeMovement)
                normalizeActorMovementSnapshot(serverCell, actor);

            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorListToKnownLiveActors(Cell* serverCell, BaseActorList& actorList, bool normalizeMovement)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (normalizeMovement)
                normalizeActorMovementSnapshot(serverCell, actor);

            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorStatsDynamicToServerAccepted(Cell* serverCell, BaseActorList& actorList)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (currentActor == nullptr)
                continue;

            if (!isClientActorStatsDynamicUpdateAllowed(currentActor, actor))
                continue;

            normalizeClientActorStatsDynamicUpdate(currentActor, actor);
            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorEquipmentToServerAccepted(Cell* serverCell, BaseActorList& actorList)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (!hasValidActorEquipment(actor))
                continue;

            if (currentActor->hasEquipmentData
                && !isNewerActorEquipmentSequence(actor.equipmentSequence, currentActor->equipmentSequence))
                continue;

            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorAnimFlagsToServerAccepted(Cell* serverCell, BaseActorList& actorList,
        std::vector<BaseActor>* correctionActors = nullptr)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());
        std::map<ActorIdentityKey, std::size_t> acceptedActorIndexes;
        std::map<ActorIdentityKey, std::size_t> correctionActorIndexes;

        const auto addAnimFlagsCorrection = [&](const BaseActor* currentActor) {
            if (currentActor == nullptr || !currentActor->hasAnimFlagsData || correctionActors == nullptr)
                return;

            acceptNewestAnimFlagsActor(*correctionActors, correctionActorIndexes, *currentActor);
        };

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (actor.hasPositionData && !isFiniteActorMovementSnapshot(actor))
                actor.hasPositionData = false;

            const bool hasNewerPosition = actor.hasPositionData
                && (!currentActor->hasPositionData
                    || isNewerPositionSequence(actor.positionSequence, currentActor->positionSequence));
            const bool hasNewerAnimFlags = actor.hasAnimFlagsData && (!currentActor->hasAnimFlagsData
                || isNewerActorAnimFlagsSequence(actor.animFlagsSequence, currentActor->animFlagsSequence));

            if (!hasNewerPosition && !hasNewerAnimFlags)
            {
                addAnimFlagsCorrection(currentActor);
                continue;
            }

            if (!hasNewerPosition && currentActor->hasPositionData)
            {
                actor.hasPositionData = true;
                actor.positionSequence = currentActor->positionSequence;
                actor.position = currentActor->position;
                actor.direction = currentActor->direction;
                actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                    currentActor->movementSampleIntervalSeconds);
                actor.movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(currentActor->movementLatencySeconds);
            }

            if (!hasNewerAnimFlags && currentActor->hasAnimFlagsData)
            {
                actor.hasAnimFlagsData = true;
                actor.animFlagsSequence = currentActor->animFlagsSequence;
                actor.movementFlags = currentActor->movementFlags;
                actor.drawState = currentActor->drawState;
                actor.isJumping = currentActor->isJumping;
                actor.isFlying = currentActor->isFlying;
            }

            acceptNewestAnimFlagsActor(acceptedActors, acceptedActorIndexes, actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorCombatToServerAccepted(Cell* serverCell, BaseActorList& actorList, bool requireMovement)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (!isActorCombatSequenceAllowed(*currentActor, actor))
                continue;

            normalizeActorMovementSnapshot(serverCell, actor);
            if (requireMovement && !actor.hasPositionData)
                continue;

            acceptActorCombatSequence(*currentActor, actor);
            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }

    inline bool filterActorDeathToServerAccepted(Cell* serverCell, BaseActorList& actorList)
    {
        if (serverCell == nullptr)
        {
            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
            if (currentActor == nullptr)
                continue;

            if (!isClientActorDeathUpdateAllowed(currentActor, actor))
                continue;

            normalizeActorMovementSnapshot(serverCell, actor);
            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

        return actorList.count != 0;
    }
}

#endif // OPENMW_PROCESSOR_ACTOR_SEQUENCE_COALESCING_HPP
