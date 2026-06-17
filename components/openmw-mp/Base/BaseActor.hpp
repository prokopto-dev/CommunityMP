#ifndef OPENMW_BASEACTOR_HPP
#define OPENMW_BASEACTOR_HPP

#include <cstdint>

#include <components/esm3/loadcell.hpp>

#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/Base/Sequence.hpp>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

namespace mwmp
{
    inline bool isNewerPositionSequence(std::uint32_t incoming, std::uint32_t current)
    {
        return isNewerSequence(incoming, current);
    }

    inline bool isNewerActorAnimFlagsSequence(std::uint32_t incoming, std::uint32_t current)
    {
        return isNewerSequence(incoming, current);
    }

    inline bool isNewerActorStatsDynamicSequence(std::uint32_t incoming, std::uint32_t current)
    {
        return isNewerSequence(incoming, current);
    }

    inline bool isNewerActorEquipmentSequence(std::uint32_t incoming, std::uint32_t current)
    {
        return isNewerSequence(incoming, current);
    }

    inline bool isNewerActorCombatSequence(std::uint32_t incoming, std::uint32_t current)
    {
        return isNewerSequence(incoming, current);
    }

    class BaseActor;

    inline bool hasValidActorEquipment(const BaseActor& actor);
    inline void mergeNewestActorPosition(BaseActor& target, const BaseActor& incoming);
    inline void mergeNewestActorAnimFlags(BaseActor& target, const BaseActor& incoming);
    inline bool isActorCombatSequenceAllowed(const BaseActor& storedActor, const BaseActor& incoming);
    inline void acceptActorCombatSequence(BaseActor& storedActor, const BaseActor& incoming);

    class BaseActor
    {
    public:

        BaseActor()
        {
        }

        std::string refId = "";
        unsigned int refNum = 0;
        unsigned int mpNum = 0;

        ESM::Position position;
        ESM::Position direction;
        float movementSampleIntervalSeconds = 1.f / 60.f;
        float movementLatencySeconds = 0.f;
        std::uint32_t positionSequence = 0;

        ESM::Cell cell;

        unsigned int movementFlags = 0;
        char drawState = 0;
        bool isJumping = false;
        bool isFlying = false;
        std::uint32_t animFlagsSequence = 0;

        std::string sound;

        SimpleCreatureStats creatureStats;
        std::uint32_t statsDynamicSequence = 0;

        Animation animation;
        char deathState = 0;
        bool isInstantDeath = false;
        Attack attack;
        Cast cast;
        bool hasCombatData = false;
        std::uint32_t combatSequence = 0;

        Target killer;

        bool isFollowerCellChange = false;

        bool hasAiTarget = false;
        Target aiTarget;
        unsigned int aiAction = 0;
        unsigned int aiDistance = 0;
        unsigned int aiDuration = 0;
        bool aiShouldRepeat = false;
        ESM::Position aiCoordinates;

        bool hasPositionData = false;
        bool hasAnimFlagsData = false;
        bool hasStatsDynamicData = false;
        bool hasEquipmentData = false;
        bool hasAiData = false;

        std::uint32_t equipmentSequence = 0;
        Item equipmentItems[equipmentSlotCount];
        SpellsActiveChanges spellsActiveChanges;
    };

    inline bool hasValidActorEquipment(const BaseActor& actor)
    {
        for (const Item& item : actor.equipmentItems)
        {
            if (!isValidEquipmentItem(item))
                return false;
        }

        return true;
    }

    inline void mergeNewestActorPosition(BaseActor& target, const BaseActor& incoming)
    {
        if (!incoming.hasPositionData)
            return;

        if (target.hasPositionData && !isNewerPositionSequence(incoming.positionSequence, target.positionSequence))
            return;

        target.hasPositionData = true;
        target.positionSequence = incoming.positionSequence;
        target.position = incoming.position;
        target.direction = incoming.direction;
        target.movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(incoming.movementSampleIntervalSeconds);
        target.movementLatencySeconds = sanitizeMovementLatencySeconds(incoming.movementLatencySeconds);
    }

    inline void mergeNewestActorAnimFlags(BaseActor& target, const BaseActor& incoming)
    {
        mergeNewestActorPosition(target, incoming);

        if (!incoming.hasAnimFlagsData)
            return;

        if (target.hasAnimFlagsData
            && !isNewerActorAnimFlagsSequence(incoming.animFlagsSequence, target.animFlagsSequence))
            return;

        target.hasAnimFlagsData = true;
        target.animFlagsSequence = incoming.animFlagsSequence;
        target.movementFlags = incoming.movementFlags;
        target.drawState = incoming.drawState;
        target.isJumping = incoming.isJumping;
        target.isFlying = incoming.isFlying;
    }

    inline bool isActorCombatSequenceAllowed(const BaseActor& storedActor, const BaseActor& incoming)
    {
        return !storedActor.hasCombatData || isNewerActorCombatSequence(incoming.combatSequence, storedActor.combatSequence);
    }

    inline void acceptActorCombatSequence(BaseActor& storedActor, const BaseActor& incoming)
    {
        storedActor.hasCombatData = true;
        storedActor.combatSequence = incoming.combatSequence;
    }

    class BaseActorList
    {
    public:

        BaseActorList()
        {

        }

        enum ACTOR_ACTION
        {
            SET = 0,
            ADD = 1,
            REMOVE = 2,
            REQUEST = 3
        };

        enum AI_ACTION
        {
            CANCEL = 0,
            ACTIVATE = 1,
            COMBAT = 2,
            ESCORT = 3,
            FOLLOW = 4,
            TRAVEL = 5,
            WANDER = 6
        };

        PacketGuid guid;

        std::vector<BaseActor> baseActors;

        unsigned int count;

        ESM::Cell cell;

        unsigned char action; // 0 - Clear and set in entirety, 1 - Add item, 2 - Remove item, 3 - Request items

        bool isValid;
    };
}

#endif //OPENMW_BASEACTOR_HPP
