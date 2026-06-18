#ifndef OPENMW_BASESTRUCTS_HPP
#define OPENMW_BASESTRUCTS_HPP

#include <cmath>
#include <string>

#include <components/esm3/activespells.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/statstate.hpp>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

namespace mwmp
{
    inline constexpr float defaultMovementSampleIntervalSeconds = 1.f / 60.f;
    inline constexpr float minMovementSampleIntervalSeconds = 1.f / 60.f;
    inline constexpr float maxMovementSampleIntervalSeconds = 0.25f;

    inline float sanitizeMovementSampleIntervalSeconds(float seconds)
    {
        if (!std::isfinite(seconds))
            return defaultMovementSampleIntervalSeconds;

        if (seconds < minMovementSampleIntervalSeconds)
            return minMovementSampleIntervalSeconds;

        if (seconds > maxMovementSampleIntervalSeconds)
            return maxMovementSampleIntervalSeconds;

        return seconds;
    }

    inline float sanitizeMovementLatencySeconds(float seconds)
    {
        constexpr float maxMovementLatencySeconds = 0.35f;

        if (!std::isfinite(seconds) || seconds <= 0.f)
            return 0.f;

        if (seconds > maxMovementLatencySeconds)
            return maxMovementLatencySeconds;

        return seconds;
    }

    namespace DialogueChoiceType
    {
        enum DIALOGUE_CHOICE
        {
            TOPIC,
            PERSUASION,
            COMPANION_SHARE,
            BARTER,
            SPELLS,
            TRAVEL,
            SPELLMAKING,
            ENCHANTING,
            TRAINING,
            REPAIR
        };
    }

    enum PACKET_ORIGIN
    {
        CLIENT_GAMEPLAY = 0,
        CLIENT_CONSOLE = 1,
        CLIENT_DIALOGUE = 2,
        CLIENT_SCRIPT_LOCAL = 3,
        CLIENT_SCRIPT_GLOBAL = 4,
        SERVER_SCRIPT = 5
    };

    enum VARIABLE_TYPE
    {
        SHORT,
        LONG,
        FLOAT,
        INT,
        STRING
    };

    struct ClientVariable
    {
        std::string id;
        int internalIndex;

        char variableType;

        int intValue;
        float floatValue;
        std::string stringValue;
    };

    struct Time
    {
        float hour;
        int day;
        int month;
        int year;

        int daysPassed;
        float timeScale;
    };

    struct Item
    {
        std::string refId;
        int count = 0;
        int charge = -1;
        float enchantmentCharge = -1.f;
        std::string soul;

        inline bool operator==(const Item& rhs)
        {
            return refId == rhs.refId && count == rhs.count && charge == rhs.charge &&
                enchantmentCharge == rhs.enchantmentCharge && soul == rhs.soul;
        }
    };

    inline constexpr int equipmentSlotCount = 19;
    inline constexpr int maxEquipmentItemStackCount = 1000000;

    inline bool isValidEquipmentItem(const Item& item)
    {
        if (!std::isfinite(item.enchantmentCharge))
            return false;

        if (item.refId.empty())
            return item.count == 0;

        if (item.refId.find("$dynamic") != std::string::npos)
            return false;

        return item.count > 0 && item.count <= maxEquipmentItemStackCount;
    }

    struct ProjectileOrigin
    {
        float origin[3] = {};
        float orientation[4] = {};
    };
    
    struct Target
    {
        bool isPlayer = false;

        std::string refId;
        unsigned int refNum = static_cast<unsigned int>(-1);
        unsigned int mpNum = static_cast<unsigned int>(-1);

        std::string name; // Remove this once the server can get names corresponding to different refIds

        PacketGuid guid;
    };

    class Attack
    {
    public:

        Target target;

        enum TYPE
        {
            MELEE = 0,
            RANGED
        };

        char type = MELEE;
        std::string attackAnimation;

        std::string rangedWeaponId;
        std::string rangedAmmoId;

        ESM::Position hitPosition;
        ProjectileOrigin projectileOrigin;

        float damage = 0;
        float attackStrength = 0;

        bool isHit = false;
        bool success = false;
        bool block = false;
        
        bool pressed = false;
        bool instant = false;
        bool knockdown = false;
        bool applyWeaponEnchantment = false;
        bool applyAmmoEnchantment = false;

        bool shouldSend = false;
        bool waitingForHitReaction = false;
        unsigned int hitReactionWaitFrames = 0;
    };

    class Cast
    {
    public:

        Target target;

        char type = 0; // 0 - regular magic, 1 - item magic
        enum TYPE
        {
            REGULAR = 0,
            ITEM
        };

        std::string spellId; // id of spell (e.g. "fireball")
        std::string itemId;

        bool hasProjectile = false;
        ProjectileOrigin projectileOrigin;

        bool isHit = false;
        bool success = false;
        bool pressed = false;
        bool instant = false;

        bool shouldSend = false;
    };

    struct SpellCooldown
    {
        std::string id;
        int startTimestampDay;
        double startTimestampHour;
    };

    struct ActiveSpell
    {
        std::string id;
        bool isStackingSpell = false;
        int timestampDay = 0;
        double timestampHour = 0;
        Target caster;
        ESM::ActiveSpells::ActiveSpellParams params;
    };

    struct SpellsActiveChanges
    {
        std::vector<ActiveSpell> activeSpells;
        enum ACTION_TYPE
        {
            SET = 0,
            ADD,
            REMOVE
        };
        int action = SET; // 0 - Clear and set in entirety, 1 - Add spell, 2 - Remove spell
    };

    struct Animation
    {
        std::string groupname;
        int mode = 0;
        int count = 0;
        bool persist = false;
    };

    struct SimpleCreatureStats
    {
        ESM::StatState<float> mDynamic[3];
        bool mDead = false;
        bool mDeathAnimationFinished = false;
    };
}

#endif //OPENMW_BASESTRUCTS_HPP
