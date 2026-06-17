#ifndef OPENMW_ACTORSTATSAUTHORITY_HPP
#define OPENMW_ACTORSTATSAUTHORITY_HPP

#include <algorithm>
#include <cmath>

#include <components/esm3/creaturestats.hpp>

#include <components/openmw-mp/Base/BaseActor.hpp>

namespace mwmp
{
    inline bool isFiniteDynamicStat(const ESM::StatState<float>& stat)
    {
        return std::isfinite(stat.mBase) && std::isfinite(stat.mCurrent) && std::isfinite(stat.mMod);
    }

    inline bool hasFiniteActorDynamicStats(const BaseActor& actor)
    {
        return isFiniteDynamicStat(actor.creatureStats.mDynamic[0])
            && isFiniteDynamicStat(actor.creatureStats.mDynamic[1])
            && isFiniteDynamicStat(actor.creatureStats.mDynamic[2]);
    }

    inline bool isClientActorStatsDynamicUpdateAllowed(const BaseActor* storedActor, const BaseActor& incoming)
    {
        if (!hasFiniteActorDynamicStats(incoming))
            return false;

        constexpr float healthDeadEpsilon = 0.001f;
        const float incomingHealth = incoming.creatureStats.mDynamic[0].mCurrent;

        if (incoming.creatureStats.mDead && incomingHealth > healthDeadEpsilon)
            return false;

        if (storedActor == nullptr || !storedActor->hasStatsDynamicData)
            return true;

        if (!isNewerActorStatsDynamicSequence(incoming.statsDynamicSequence, storedActor->statsDynamicSequence))
            return false;

        if (storedActor->creatureStats.mDead && !incoming.creatureStats.mDead)
            return false;

        constexpr float healthIncreaseEpsilon = 0.001f;
        const float storedHealth = storedActor->creatureStats.mDynamic[0].mCurrent;
        return incomingHealth <= storedHealth + healthIncreaseEpsilon;
    }

    inline void normalizeClientActorStatsDynamicUpdate(const BaseActor* storedActor, BaseActor& incoming)
    {
        if (storedActor == nullptr || !storedActor->hasStatsDynamicData)
            return;

        const float incomingHealth = incoming.creatureStats.mDynamic[0].mCurrent;
        const bool incomingDead = incoming.creatureStats.mDead;
        const bool incomingDeathAnimationFinished = incoming.creatureStats.mDeathAnimationFinished;

        incoming.creatureStats = storedActor->creatureStats;
        incoming.creatureStats.mDynamic[0].mCurrent = std::min(
            incomingHealth, storedActor->creatureStats.mDynamic[0].mCurrent);

        if (storedActor->creatureStats.mDead || incomingDead)
            incoming.creatureStats.mDead = true;

        if (incoming.creatureStats.mDead)
            incoming.creatureStats.mDeathAnimationFinished =
                storedActor->creatureStats.mDeathAnimationFinished || incomingDeathAnimationFinished;
    }

    inline bool hasServerAcceptedDeadActorState(const BaseActor& storedActor)
    {
        if (!storedActor.hasStatsDynamicData || !hasFiniteActorDynamicStats(storedActor))
            return false;

        if (!storedActor.creatureStats.mDead)
            return false;

        constexpr float healthDeadEpsilon = 0.001f;
        const float storedHealth = storedActor.creatureStats.mDynamic[0].mCurrent;
        return storedHealth <= healthDeadEpsilon;
    }

    inline bool isServerActorDeadForDeathPacket(const BaseActor& storedActor)
    {
        return hasServerAcceptedDeadActorState(storedActor);
    }

    inline bool isClientActorDeathUpdateAllowed(const BaseActor* storedActor, const BaseActor& incoming)
    {
        if (incoming.deathState == 0 || storedActor == nullptr)
            return false;

        if (!isServerActorDeadForDeathPacket(*storedActor))
            return false;

        return incoming.statsDynamicSequence == storedActor->statsDynamicSequence;
    }

    inline bool isClientActorControlUpdateAllowed(const BaseActor* storedActor)
    {
        if (storedActor == nullptr || storedActor->creatureStats.mDead)
            return false;

        if (storedActor->hasStatsDynamicData && hasFiniteActorDynamicStats(*storedActor))
        {
            constexpr float healthDeadEpsilon = 0.001f;
            const float storedHealth = storedActor->creatureStats.mDynamic[0].mCurrent;
            if (storedHealth <= healthDeadEpsilon)
                return false;
        }

        return !hasServerAcceptedDeadActorState(*storedActor);
    }
}

#endif // OPENMW_ACTORSTATSAUTHORITY_HPP
