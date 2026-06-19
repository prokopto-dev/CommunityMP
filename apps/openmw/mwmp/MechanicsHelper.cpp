#include <cmath>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Utils.hpp>

#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/values.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/esm3/loadweap.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/damagesourcetype.hpp"
#include "../mwmechanics/levelledlist.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellcasting.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwrender/animation.hpp"

#include "../serversimulationevents.hpp"
#include "../serversimulationmode.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "MechanicsHelper.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"
#include "ObjectList.hpp"
#include "CellController.hpp"

using namespace mwmp;

namespace
{
    ESM::RefId stringRefId(const std::string& id)
    {
        if (id.empty())
            return {};
        return ESM::RefId::stringRefId(id);
    }

    MWWorld::Ptr getCurrentPlayerPtr()
    {
        return MWBase::Environment::get().getWorld()->getPlayerPtr();
    }

    bool isFinitePosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2])
            && std::isfinite(position.rot[0]) && std::isfinite(position.rot[1]) && std::isfinite(position.rot[2]);
    }

    bool isFiniteProjectileOrigin(const mwmp::ProjectileOrigin& projectileOrigin)
    {
        for (float coordinate : projectileOrigin.origin)
        {
            if (!std::isfinite(coordinate))
                return false;
        }

        for (float coordinate : projectileOrigin.orientation)
        {
            if (!std::isfinite(coordinate))
                return false;
        }

        return true;
    }

    std::string attackTypeName(int attackType)
    {
        switch (attackType)
        {
            case ESM::Weapon::AT_Chop:
                return "chop";
            case ESM::Weapon::AT_Slash:
                return "slash";
            case ESM::Weapon::AT_Thrust:
                return "thrust";
            default:
                return {};
        }
    }

    bool isMeleeAttackType(std::string_view attackType)
    {
        return attackType == "chop" || attackType == "slash" || attackType == "thrust";
    }

    std::string_view resolveRemoteAttackType(const Attack& attack, const MWMechanics::CreatureStats& attackerStats)
    {
        if (attack.type == Attack::RANGED)
            return "shoot";

        if (attack.type != Attack::MELEE)
            return {};

        if (isMeleeAttackType(attack.attackAnimation))
            return attack.attackAnimation;

        const std::string_view lastAttackType = attackerStats.getAttackType();
        if (isMeleeAttackType(lastAttackType))
            return lastAttackType;

        return "chop";
    }

    float getVisibleRemoteAttackStrength(float attackStrength)
    {
        if (!std::isfinite(attackStrength) || attackStrength <= 0.f)
            return 0.45f;

        return std::clamp(attackStrength, 0.f, 1.f);
    }

    unsigned int getLocalHitReactionWaitFrames()
    {
        return 4;
    }

    void queueServerSimulationAttackStart(
        const MWWorld::Ptr& attacker, bool isRanged, const std::string& attackAnimation)
    {
        if (!OMW::isServerSimulationModeActive())
            return;

        OMW::ServerSimulationCombatEvent event;
        event.attacker = attacker;
        MechanicsHelper::resetAttack(&event.attack);
        event.attack.type = static_cast<char>(isRanged ? Attack::RANGED : Attack::MELEE);
        event.attack.pressed = true;
        event.attack.attackAnimation = attackAnimation;
        event.attack.shouldSend = true;
        OMW::queueServerSimulationCombatEvent(std::move(event));
    }

    void queueServerSimulationMeleeAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
        const MWWorld::Ptr& weapon, float attackStrength, int attackType, bool isHit, bool success, float damage,
        bool block, const osg::Vec3f& hitPosition, bool applyWeaponEnchantment)
    {
        if (!OMW::isServerSimulationModeActive())
            return;

        OMW::ServerSimulationCombatEvent event;
        event.attacker = attacker;
        event.victim = victim;
        event.weapon = weapon;
        MechanicsHelper::resetAttack(&event.attack);
        event.attack.type = static_cast<char>(Attack::MELEE);
        event.attack.pressed = false;
        event.attack.attackAnimation = attackTypeName(attackType);
        event.attack.attackStrength = attackStrength;
        event.attack.isHit = isHit && !victim.isEmpty();
        event.attack.success = success;
        event.attack.damage = std::max(0.f, damage);
        event.attack.block = block;
        event.attack.applyWeaponEnchantment = applyWeaponEnchantment;
        event.attack.hitPosition = MechanicsHelper::getPositionFromVector(hitPosition);

        if (!weapon.isEmpty())
            event.attack.rangedWeaponId = weapon.getCellRef().getRefId().serializeText();

        event.attack.waitingForHitReaction = event.attack.isHit && event.attack.success && !event.attack.block;
        event.attack.hitReactionWaitFrames = event.attack.waitingForHitReaction ? getLocalHitReactionWaitFrames() : 0;
        event.attack.shouldSend = true;
        OMW::queueServerSimulationCombatEvent(std::move(event));
    }

    void queueServerSimulationRangedAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
        const MWWorld::Ptr& weapon, const MWWorld::Ptr& projectile, float attackStrength, bool isHit, bool success,
        float damage, const osg::Vec3f& hitPosition, bool applyWeaponEnchantment, bool applyAmmoEnchantment)
    {
        if (!OMW::isServerSimulationModeActive())
            return;

        OMW::ServerSimulationCombatEvent event;
        event.attacker = attacker;
        event.victim = victim;
        event.weapon = weapon;
        event.projectile = projectile;
        MechanicsHelper::resetAttack(&event.attack);
        event.attack.type = static_cast<char>(Attack::RANGED);
        event.attack.pressed = false;
        event.attack.attackStrength = attackStrength;
        event.attack.isHit = isHit && !victim.isEmpty();
        event.attack.success = success;
        event.attack.damage = std::max(0.f, damage);
        event.attack.block = false;
        event.attack.applyWeaponEnchantment = applyWeaponEnchantment;
        event.attack.applyAmmoEnchantment = applyAmmoEnchantment;
        event.attack.hitPosition = MechanicsHelper::getPositionFromVector(hitPosition);

        if (!weapon.isEmpty())
            event.attack.rangedWeaponId = weapon.getCellRef().getRefId().serializeText();

        if (!projectile.isEmpty() && projectile != weapon)
            event.attack.rangedAmmoId = projectile.getCellRef().getRefId().serializeText();

        event.attack.waitingForHitReaction = event.attack.isHit && event.attack.success;
        event.attack.hitReactionWaitFrames = event.attack.waitingForHitReaction ? getLocalHitReactionWaitFrames() : 0;
        event.attack.shouldSend = true;
        OMW::queueServerSimulationCombatEvent(std::move(event));
    }

    bool hasUsableRefNum(unsigned int refNum)
    {
        return refNum != 0 && refNum != static_cast<unsigned int>(-1);
    }

    bool targetRefIdMatches(const MWWorld::Ptr& ptr, const mwmp::Target& target)
    {
        return target.refId.empty() || ptr.getCellRef().getRefId() == stringRefId(target.refId);
    }

    bool isUsableActorTarget(const MWWorld::Ptr& ptr, const mwmp::Target& target)
    {
        return !ptr.isEmpty() && ptr.getClass().isActor() && ptr.getRefData().isEnabled()
            && ptr.getCellRef().getCount(false) != 0 && targetRefIdMatches(ptr, target);
    }

    MWWorld::Ptr findActorByLocalRefNum(
        MWWorld::CellStore* cellStore, unsigned int localRefNum, const mwmp::Target& target)
    {
        if (cellStore == nullptr || !hasUsableRefNum(localRefNum))
            return {};

        MWWorld::Ptr found;
        cellStore->forEach([&](const MWWorld::Ptr& ptr) {
            if (ptr.getCellRef().getRefNum().mIndex == localRefNum && isUsableActorTarget(ptr, target))
            {
                found = ptr;
                return false;
            }

            return true;
        }, true);

        return found;
    }

    MWWorld::Ptr findUniqueActorByRefId(MWWorld::CellStore* cellStore, const mwmp::Target& target)
    {
        if (cellStore == nullptr || target.refId.empty())
            return {};

        MWWorld::Ptr found;
        bool ambiguous = false;
        cellStore->forEach([&](const MWWorld::Ptr& ptr) {
            if (!isUsableActorTarget(ptr, target))
                return true;

            if (found.isEmpty())
            {
                found = ptr;
                return true;
            }

            ambiguous = true;
            return false;
        }, true);

        if (ambiguous)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Ignoring actor target fallback for %s because the cell has multiple matching actors",
                target.refId.c_str());
            return {};
        }

        return found;
    }

    void registerTargetServerId(const MWWorld::Ptr& ptr, const mwmp::Target& target)
    {
        if (ptr.isEmpty() || target.mpNum == 0 || !mwmp::Main::isInitialized())
            return;

        mwmp::Main::get().getNetworking()->getObjectList()->registerServerObjectId(ptr, target.mpNum);
    }

    MWWorld::Ptr resolveActorTargetInCell(const mwmp::Target& target, MWWorld::CellStore* fallbackCellStore)
    {
        if (fallbackCellStore == nullptr)
            return {};

        mwmp::ObjectList* objectList = mwmp::Main::isInitialized()
            ? mwmp::Main::get().getNetworking()->getObjectList()
            : nullptr;

        if (target.mpNum != 0 && objectList != nullptr)
        {
            const std::optional<unsigned int> localRefNum = objectList->getLocalRefNumForServerMpNum(target.mpNum);
            if (localRefNum.has_value())
            {
                MWWorld::Ptr found = findActorByLocalRefNum(fallbackCellStore, *localRefNum, target);
                if (!found.isEmpty())
                    return found;
            }
        }

        if (hasUsableRefNum(target.refNum))
        {
            MWWorld::Ptr found = findActorByLocalRefNum(fallbackCellStore, target.refNum, target);
            if (!found.isEmpty())
            {
                registerTargetServerId(found, target);
                return found;
            }
        }

        if (!target.refId.empty())
        {
            MWWorld::Ptr found = findUniqueActorByRefId(fallbackCellStore, target);
            if (!found.isEmpty())
            {
                registerTargetServerId(found, target);
                return found;
            }
        }

        return {};
    }

    std::string describeTarget(const mwmp::Target& target)
    {
        if (target.isPlayer)
            return "player guid=" + mwmp::packetGuidToString(target.guid);

        return "actor refId=" + target.refId + " refNum=" + std::to_string(target.refNum)
            + " mpNum=" + std::to_string(target.mpNum);
    }

    MWWorld::Ptr resolveTargetPtr(const mwmp::Target& target, MWWorld::CellStore* fallbackCellStore = nullptr)
    {
        if (target.isPlayer)
        {
            if (target.guid == mwmp::Main::get().getLocalPlayer()->guid)
                return getCurrentPlayerPtr();

            if (mwmp::DedicatedPlayer* dedicatedPlayer = mwmp::PlayerList::getPlayer(target.guid))
                return dedicatedPlayer->getPtr();

            return {};
        }

        auto* cellController = mwmp::Main::get().getCellController();
        if (cellController->isLocalActor(target.refNum, target.mpNum))
        {
            if (mwmp::LocalActor* localActor = cellController->getLocalActor(target.refNum, target.mpNum))
                return localActor->getPtr();
        }

        if (cellController->isDedicatedActor(target.refNum, target.mpNum))
        {
            if (mwmp::DedicatedActor* dedicatedActor = cellController->getDedicatedActor(target.refNum, target.mpNum))
                return dedicatedActor->getPtr();
        }

        return resolveActorTargetInCell(target, fallbackCellStore);
    }

    void updateDynamicStatsFromPtr(const MWWorld::Ptr& ptr, bool sendLocalActorsImmediately)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return;

        if (ptr == getCurrentPlayerPtr())
        {
            mwmp::Main::get().getLocalPlayer()->updateStatsDynamic(true);
            return;
        }

        if (mwmp::DedicatedPlayer* dedicatedPlayer = mwmp::PlayerList::getPlayer(ptr))
        {
            dedicatedPlayer->restoreDynamicStats();
            return;
        }

        auto* cellController = mwmp::Main::get().getCellController();
        if (cellController->isLocalActor(ptr))
        {
            if (mwmp::LocalActor* localActor = cellController->getLocalActor(ptr))
            {
                if (sendLocalActorsImmediately)
                    localActor->sendStatsDynamic();
                else
                    localActor->updateStatsDynamic(true);
            }
            return;
        }

        if (cellController->isDedicatedActor(ptr))
        {
            if (mwmp::DedicatedActor* dedicatedActor = cellController->getDedicatedActor(ptr))
                dedicatedActor->restoreDynamicStats();
        }
    }

    void syncDynamicStatsFromPtr(const MWWorld::Ptr& ptr)
    {
        updateDynamicStatsFromPtr(ptr, true);
    }

    void queueDynamicStatsFromPtr(const MWWorld::Ptr& ptr)
    {
        updateDynamicStatsFromPtr(ptr, false);
    }

    void publishLocalDynamicStatsFromPtr(const MWWorld::Ptr& ptr, bool sendLocalActorsImmediately)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return;

        if (ptr == getCurrentPlayerPtr())
        {
            mwmp::Main::get().getLocalPlayer()->updateStatsDynamic(true);
            return;
        }

        auto* cellController = mwmp::Main::get().getCellController();
        if (cellController->isLocalActor(ptr))
        {
            if (mwmp::LocalActor* localActor = cellController->getLocalActor(ptr))
            {
                if (sendLocalActorsImmediately)
                    localActor->sendStatsDynamic();
                else
                    localActor->updateStatsDynamic(true);
            }
        }
    }

    void syncDynamicStatsAfterCombatEffect(const MWWorld::Ptr& caster, const MWWorld::Ptr& victim)
    {
        syncDynamicStatsFromPtr(caster);

        if (!victim.isEmpty() && victim != caster)
            syncDynamicStatsFromPtr(victim);
    }

    osg::Vec3f getSpellHitPosition(const MWWorld::Ptr& caster, const MWWorld::Ptr& target)
    {
        osg::Vec3f hitPosition = caster.getRefData().getPosition().asVec3();
        if (target.isEmpty())
            return hitPosition;

        hitPosition = target.getRefData().getPosition().asVec3();
        constexpr float explosionHeight = 0.7f;
        float targetHeight = MWBase::Environment::get().getWorld()->getHalfExtents(target).z() * 2.f;
        if (!target.getClass().isActor() && caster == getCurrentPlayerPtr())
        {
            const float playerHeight = MWBase::Environment::get().getWorld()->getHalfExtents(caster).z() * 2.f;
            targetHeight = std::min(targetHeight, playerHeight);
        }

        hitPosition.z() += targetHeight * explosionHeight;
        return hitPosition;
    }

    float applyAttackDamageModifiers(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
        bool isHealthDamage)
    {
        if (!isHealthDamage || damage < 0.001f)
            return damage;

        const float armor = std::max(0.f, victim.getClass().getArmorRating(victim, true));
        if (armor > 0.f)
        {
            const float armorTerm = damage / (damage + armor);
            const float armorFloor = MWBase::Environment::get()
                                         .getESMStore()
                                         ->get<ESM::GameSetting>()
                                         .find("fCombatArmorMinMult")
                                         ->mValue.getFloat();
            damage *= std::max(armorTerm, armorFloor);
        }

        damage = std::max(damage, 1.f);

        const bool attackerIsPlayer = attacker == getCurrentPlayerPtr() || mwmp::PlayerList::isDedicatedPlayer(attacker);
        const bool victimIsPlayer = victim == getCurrentPlayerPtr() || mwmp::PlayerList::isDedicatedPlayer(victim);
        if (attackerIsPlayer == victimIsPlayer)
            return damage;

        const float difficultyTerm = Settings::game().mDifficulty * 0.01f;
        const float difficultyMult = MWBase::Environment::get()
                                         .getESMStore()
                                         ->get<ESM::GameSetting>()
                                         .find("fDifficultyMult")
                                         ->mValue.getFloat();

        float difficultyScale = 0.f;
        if (victimIsPlayer)
            difficultyScale = difficultyTerm > 0.f ? difficultyTerm * difficultyMult : difficultyTerm / difficultyMult;
        else
            difficultyScale = difficultyTerm > 0.f ? -difficultyTerm / difficultyMult : -difficultyTerm * difficultyMult;

        return std::max(0.f, damage * (1.f + difficultyScale));
    }

    void applyAttackReaction(const mwmp::Attack& attack, const MWWorld::Ptr& victim, float appliedDamage)
    {
        if (victim.isEmpty() || !attack.success)
            return;

        MWMechanics::CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);

        if (attack.block)
        {
            victimStats.setBlock(true);
        }
        else if (attack.knockdown)
        {
            victimStats.setHitRecovery(false);
            victimStats.setKnockedDown(true);
        }
        else if (appliedDamage >= 0.001f && !victimStats.getKnockedDown())
        {
            victimStats.setHitRecovery(true);
        }
    }

    bool attackVisuallyDamagesHealth(
        const mwmp::Attack& attack, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
    {
        if (attack.type != mwmp::Attack::MELEE || !attack.rangedWeaponId.empty()
            || !attacker.getClass().isBipedal(attacker))
            return true;

        const MWMechanics::CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);
        return victimStats.isParalyzed() || (victimStats.getKnockedDown() && !attack.knockdown);
    }

    void playNetworkHitCue(const mwmp::Attack& attack, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
    {
        if (victim.isEmpty() || !attack.success || attack.block || attack.damage < 0.001f)
            return;

        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        if (soundManager == nullptr)
            return;

        const bool healthDamage = attackVisuallyDamagesHealth(attack, attacker, victim);

        try
        {
            if (attack.type == mwmp::Attack::MELEE && attack.rangedWeaponId.empty()
                && attacker.getClass().isBipedal(attacker))
            {
                if (attacker.getClass().isNpc() && attacker.getClass().getNpcStats(attacker).isWerewolf())
                {
                    auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                    const ESM::Sound* sound
                        = MWBase::Environment::get().getESMStore()->get<ESM::Sound>().searchRandom("WolfHit", prng);
                    if (sound != nullptr)
                        soundManager->playSound3D(victim, sound->mId, 1.0f, 1.0f);
                    return;
                }

                if (!healthDamage)
                {
                    static const std::array<ESM::RefId, 2> sounds
                        = { ESM::RefId::stringRefId("Hand To Hand Hit"), ESM::RefId::stringRefId("Hand To Hand Hit 2") };
                    auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                    soundManager->playSound3D(victim, sounds[Misc::Rng::rollDice(sounds.size(), prng)], 1.0f, 1.0f);
                    return;
                }
            }

            if (healthDamage)
                soundManager->playSound3D(victim, ESM::RefId::stringRefId("Health Damage"), 1.0f, 1.0f);
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to play network hit cue: %s", e.what());
        }
    }

    void applyNetworkHitDamageFallback(
        const MWWorld::Ptr& victim, bool isHealthDamage, float damage, float healthBefore, float fatigueBefore)
    {
        if (victim.isEmpty() || damage < 0.001f)
            return;

        MWMechanics::CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);

        if (isHealthDamage)
        {
            MWMechanics::DynamicStat<float> health = victimStats.getHealth();
            const float expectedHealth = healthBefore - damage;
            if (health.getCurrent() > expectedHealth)
            {
                health.setCurrent(expectedHealth);
                victimStats.setHealth(health);
            }
        }
        else
        {
            MWMechanics::DynamicStat<float> fatigue = victimStats.getFatigue();
            const float expectedFatigue = fatigueBefore - damage;
            if (fatigue.getCurrent() > expectedFatigue)
            {
                fatigue.setCurrent(expectedFatigue, true);
                victimStats.setFatigue(fatigue);
            }
        }
    }

    bool attackTargetsPtr(const mwmp::Attack& attack, const MWWorld::Ptr& victim)
    {
        if (victim.isEmpty() || !attack.isHit)
            return false;

        const mwmp::Target victimTarget = MechanicsHelper::getTarget(victim);
        if (attack.target.isPlayer != victimTarget.isPlayer)
            return false;

        if (victimTarget.isPlayer)
            return attack.target.guid == victimTarget.guid;

        return attack.target.refId == victimTarget.refId && attack.target.refNum == victimTarget.refNum
            && attack.target.mpNum == victimTarget.mpNum;
    }
}

osg::Vec3f MechanicsHelper::getLinearInterpolation(osg::Vec3f start, osg::Vec3f end, float percent)
{
    const float clampedPercent = std::clamp(percent, 0.f, 1.f);
    osg::Vec3f position(clampedPercent, clampedPercent, clampedPercent);

    return (start + osg::componentMultiply(position, (end - start)));
}

float MechanicsHelper::getRemoteMovementInterpolationFactor(float dt)
{
    return getRemoteMovementInterpolationFactor(dt, 0.f, false);
}

float MechanicsHelper::getRemoteMovementInterpolationFactor(float dt, float distanceToTarget, bool hasTranslationIntent)
{
    return getRemoteMovementInterpolationFactor(dt, distanceToTarget, hasTranslationIntent, 0.f);
}

float MechanicsHelper::getRemoteMovementInterpolationFactor(
    float dt, float distanceToTarget, bool hasTranslationIntent, float jitterSeconds)
{
    if (!std::isfinite(dt) || dt <= 0.f)
        return 0.f;

    const float clampedDistance = std::isfinite(distanceToTarget) ? std::max(0.f, distanceToTarget) : 0.f;
    const float jitterPaddingSeconds = std::min(sanitizeRemoteMovementJitterSeconds(jitterSeconds) * 0.50f, 0.035f);
    const float settledCatchupSeconds = (hasTranslationIntent ? 0.070f : 0.110f) + jitterPaddingSeconds;
    constexpr float urgentCatchupSeconds = 0.045f;
    constexpr float urgentDistance = 128.f;
    constexpr float settledDistance = 32.f;

    float catchupSeconds = settledCatchupSeconds;
    if (clampedDistance > settledDistance)
    {
        const float urgency = std::clamp((clampedDistance - settledDistance) / (urgentDistance - settledDistance), 0.f, 1.f);
        catchupSeconds = settledCatchupSeconds + (urgentCatchupSeconds - settledCatchupSeconds) * urgency;
    }

    const float factor = 1.f - std::exp(-dt / catchupSeconds);
    const float maxFrameCatchup = hasTranslationIntent ? 0.42f : 0.30f;
    return std::clamp(factor, 0.f, maxFrameCatchup);
}

float MechanicsHelper::getRemoteRotationInterpolationFactor(float dt)
{
    if (!std::isfinite(dt) || dt <= 0.f)
        return 0.f;

    constexpr float rotationCatchupSeconds = 0.060f;
    constexpr float maxFrameCatchup = 0.50f;
    const float factor = 1.f - std::exp(-dt / rotationCatchupSeconds);
    return std::clamp(factor, 0.f, maxFrameCatchup);
}

osg::Vec3f MechanicsHelper::getInterpolatedRemoteRotation(
    const ESM::Position& currentPosition, const ESM::Position& targetPosition, float interpolationFactor)
{
    const float factor = std::clamp(interpolationFactor, 0.f, 1.f);
    osg::Vec3f rotation;

    for (int axis = 0; axis < 3; ++axis)
    {
        const float current = std::isfinite(currentPosition.rot[axis]) ? currentPosition.rot[axis] : 0.f;
        const float target = std::isfinite(targetPosition.rot[axis]) ? targetPosition.rot[axis] : current;
        const float delta = std::atan2(std::sin(target - current), std::cos(target - current));
        rotation[axis] = current + delta * factor;
    }

    return rotation;
}

bool MechanicsHelper::hasRemoteTranslationIntent(const ESM::Position& direction)
{
    constexpr float intentEpsilon = 0.01f;
    return std::abs(direction.pos[0]) > intentEpsilon || std::abs(direction.pos[1]) > intentEpsilon
        || std::abs(direction.pos[2]) > intentEpsilon;
}

osg::Vec3f MechanicsHelper::estimateRemoteVelocity(
    const ESM::Position& previousPosition, const ESM::Position& currentPosition, float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return osg::Vec3f();

    const osg::Vec3f delta = currentPosition.asVec3() - previousPosition.asVec3();
    const float distanceSquared = delta.length2();
    constexpr float minMovementDistance = 0.01f;
    constexpr float maxMovementDistance = 512.f;

    if (!std::isfinite(distanceSquared) || distanceSquared < minMovementDistance * minMovementDistance
        || distanceSquared > maxMovementDistance * maxMovementDistance)
        return osg::Vec3f();

    osg::Vec3f velocity = delta / deltaSeconds;
    const float speedSquared = velocity.length2();
    constexpr float maxRemoteSpeed = 3600.f;
    if (!std::isfinite(speedSquared))
        return osg::Vec3f();

    if (speedSquared > maxRemoteSpeed * maxRemoteSpeed)
        velocity *= maxRemoteSpeed / std::sqrt(speedSquared);

    return velocity;
}

osg::Vec3f MechanicsHelper::smoothRemoteVelocity(
    const osg::Vec3f& previousVelocity, const osg::Vec3f& sampleVelocity, float deltaSeconds, bool hasPreviousVelocity)
{
    if (!hasPreviousVelocity || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return sampleVelocity;

    const float previousSpeedSquared = previousVelocity.length2();
    const float sampleSpeedSquared = sampleVelocity.length2();
    if (!std::isfinite(previousSpeedSquared) || !std::isfinite(sampleSpeedSquared))
        return sampleVelocity;

    if (previousSpeedSquared > 1.f && sampleSpeedSquared > 1.f && previousVelocity * sampleVelocity < 0.f)
        return sampleVelocity;

    constexpr float velocityBlendSeconds = 0.045f;
    const float blend = std::clamp(1.f - std::exp(-deltaSeconds / velocityBlendSeconds), 0.20f, 0.80f);
    return previousVelocity + (sampleVelocity - previousVelocity) * blend;
}

float MechanicsHelper::smoothRemoteTimingValue(float previousValue, float sampleValue, float deltaSeconds, bool hasPreviousValue)
{
    if (!std::isfinite(sampleValue))
        return std::isfinite(previousValue) ? previousValue : 0.f;

    if (!hasPreviousValue || !std::isfinite(previousValue) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return sampleValue;

    constexpr float timingBlendSeconds = 0.20f;
    const float blend = std::clamp(1.f - std::exp(-deltaSeconds / timingBlendSeconds), 0.05f, 0.35f);
    return previousValue + (sampleValue - previousValue) * blend;
}

float MechanicsHelper::sanitizeRemoteMovementJitterSeconds(float seconds)
{
    constexpr float maxJitterSeconds = 0.12f;

    if (!std::isfinite(seconds) || seconds <= 0.f)
        return 0.f;

    return std::min(seconds, maxJitterSeconds);
}

osg::Vec3f MechanicsHelper::getPredictedRemoteMovementTarget(
    const ESM::Position& position, const ESM::Position& direction, const osg::Vec3f& velocity, bool hasVelocity,
    float packetAgeSeconds, float sampleIntervalSeconds, float latencySeconds)
{
    return getPredictedRemoteMovementTarget(position, direction, velocity, hasVelocity, packetAgeSeconds,
        sampleIntervalSeconds, latencySeconds, 0.f);
}

osg::Vec3f MechanicsHelper::getPredictedRemoteMovementTarget(
    const ESM::Position& position, const ESM::Position& direction, const osg::Vec3f& velocity, bool hasVelocity,
    float packetAgeSeconds, float sampleIntervalSeconds, float latencySeconds, float jitterSeconds)
{
    osg::Vec3f target = position.asVec3();
    if (!hasVelocity || !hasRemoteTranslationIntent(direction))
        return target;

    const float speedSquared = velocity.length2();
    if (!std::isfinite(speedSquared) || speedSquared < 1.f)
        return target;

    constexpr float baseVisualLeadSeconds = 0.f;
    constexpr float sampleIntervalLeadScale = 0.10f;
    constexpr float latencyLeadScale = 0.f;
    constexpr float jitterLeadScale = 0.f;
    constexpr float maxPacketAgeLeadSeconds = 0.012f;
    constexpr float maxSampleIntervalLeadSeconds = 0.006f;
    constexpr float maxLatencyLeadSeconds = 0.f;
    constexpr float maxJitterLeadSeconds = 0.f;
    constexpr float maxVisualLeadSeconds = 0.018f;
    constexpr float maxVisualLeadDistance = 8.f;
    const float packetAgeLeadSeconds = std::isfinite(packetAgeSeconds)
        ? std::clamp(packetAgeSeconds, 0.f, maxPacketAgeLeadSeconds)
        : 0.f;
    const float sampleIntervalLeadSeconds = std::clamp(
        sanitizeMovementSampleIntervalSeconds(sampleIntervalSeconds) * sampleIntervalLeadScale,
        0.f, maxSampleIntervalLeadSeconds);
    const float latencyLeadSeconds = std::clamp(
        sanitizeMovementLatencySeconds(latencySeconds) * latencyLeadScale, 0.f, maxLatencyLeadSeconds);
    const float jitterLeadSeconds = std::clamp(
        sanitizeRemoteMovementJitterSeconds(jitterSeconds) * jitterLeadScale, 0.f, maxJitterLeadSeconds);
    const float visualLeadSeconds = std::clamp(
        baseVisualLeadSeconds + packetAgeLeadSeconds + sampleIntervalLeadSeconds + latencyLeadSeconds
            + jitterLeadSeconds,
        baseVisualLeadSeconds, maxVisualLeadSeconds);
    osg::Vec3f lead = velocity * visualLeadSeconds;
    const float leadDistanceSquared = lead.length2();
    if (!std::isfinite(leadDistanceSquared))
        return target;

    if (leadDistanceSquared > maxVisualLeadDistance * maxVisualLeadDistance)
        lead *= maxVisualLeadDistance / std::sqrt(leadDistanceSquared);

    return target + lead;
}

float MechanicsHelper::sanitizeMovementComponent(float value)
{
    constexpr float movementEpsilon = 0.0001f;
    if (!std::isfinite(value) || std::abs(value) <= movementEpsilon)
        return 0.f;

    return value;
}

void MechanicsHelper::deriveMissingMovementDirection(
    ESM::Position& direction, const ESM::Position& currentPosition, const ESM::Position& previousPosition)
{
    if (direction.pos[0] != 0.f || direction.pos[1] != 0.f)
        return;

    const float deltaX = currentPosition.pos[0] - previousPosition.pos[0];
    const float deltaY = currentPosition.pos[1] - previousPosition.pos[1];
    const float horizontalDistanceSquared = deltaX * deltaX + deltaY * deltaY;
    constexpr float minDerivedMovementDistance = 0.01f;
    constexpr float maxDerivedMovementDistance = 512.f;

    if (horizontalDistanceSquared < minDerivedMovementDistance * minDerivedMovementDistance
        || horizontalDistanceSquared > maxDerivedMovementDistance * maxDerivedMovementDistance)
        return;

    const float yaw = currentPosition.rot[2];
    if (!std::isfinite(yaw))
        return;

    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    const float localSide = deltaX * cosYaw - deltaY * sinYaw;
    const float localForward = deltaX * sinYaw + deltaY * cosYaw;
    const float localDistance = std::sqrt(localSide * localSide + localForward * localForward);

    if (localDistance <= 0.f)
        return;

    direction.pos[0] = sanitizeMovementComponent(localSide / localDistance);
    direction.pos[1] = sanitizeMovementComponent(localForward / localDistance);
}

ESM::Position MechanicsHelper::getPositionFromVector(osg::Vec3f vector)
{
    ESM::Position position;
    position.pos[0] = vector.x();
    position.pos[1] = vector.y();
    position.pos[2] = vector.z();

    return position;
}

// Inspired by similar code in mwclass\creaturelevlist.cpp
//
// TODO: Add handling of scaling based on leveled list's assigned scale
void MechanicsHelper::spawnLeveledCreatures(MWWorld::CellStore* cellStore)
{
    mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
    objectList->reset();
    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;

    int spawnCount = 0;

    std::vector<MWWorld::Ptr> leveledCreatureRefs;
    cellStore->forEachType<ESM::CreatureLevList>([&](const MWWorld::Ptr& ptr) {
        leveledCreatureRefs.push_back(ptr);
        return true;
    });

    for (const MWWorld::Ptr& ptr : leveledCreatureRefs)
    {
        const ESM::RefId id = MWMechanics::getLevelledItem(
            ptr.get<ESM::CreatureLevList>()->mBase, true, MWBase::Environment::get().getWorld()->getPrng());

        if (!id.empty())
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            MWWorld::ManualRef manualRef(store, id);
            manualRef.getPtr().getCellRef().setPosition(ptr.getCellRef().getPosition());
            MWWorld::Ptr placed = MWBase::Environment::get().getWorld()->placeObject(manualRef.getPtr(), ptr.getCell(),
                                                                                     ptr.getCellRef().getPosition());
            objectList->addObjectSpawn(placed);
            MWBase::Environment::get().getWorld()->deleteObject(placed);

            spawnCount++;
        }
    }

    if (spawnCount > 0)
        objectList->sendObjectSpawn();
}

bool MechanicsHelper::isUsingRangedWeapon(const MWWorld::Ptr& ptr)
{
    if (ptr.getClass().hasInventoryStore(ptr))
    {
        MWWorld::InventoryStore &inventoryStore = ptr.getClass().getInventoryStore(ptr);
        MWWorld::ContainerStoreIterator weaponSlot = inventoryStore.getSlot(
            MWWorld::InventoryStore::Slot_CarriedRight);

        if (weaponSlot != inventoryStore.end() && weaponSlot->getType() == ESM::Weapon::sRecordId)
        {
            const ESM::Weapon* weaponRecord = weaponSlot->get<ESM::Weapon>()->mBase;

            if (weaponRecord->mData.mType >= ESM::Weapon::MarksmanBow)
                return true;
        }
    }

    return false;
}

Attack *MechanicsHelper::getLocalAttack(const MWWorld::Ptr& ptr)
{
    if (ptr == getCurrentPlayerPtr())
        return &mwmp::Main::get().getLocalPlayer()->attack;
    else if (mwmp::Main::get().getCellController()->isLocalActor(ptr))
    {
        if (mwmp::LocalActor* localActor = mwmp::Main::get().getCellController()->getLocalActor(ptr))
            return &localActor->attack;
    }

    return nullptr;
}

Attack *MechanicsHelper::getDedicatedAttack(const MWWorld::Ptr& ptr)
{
    if (mwmp::PlayerList::isDedicatedPlayer(ptr))
        return &mwmp::PlayerList::getPlayer(ptr)->attack;
    else if (mwmp::Main::get().getCellController()->isDedicatedActor(ptr))
    {
        if (mwmp::DedicatedActor* dedicatedActor = mwmp::Main::get().getCellController()->getDedicatedActor(ptr))
            return &dedicatedActor->attack;
    }

    return nullptr;
}

Cast *MechanicsHelper::getLocalCast(const MWWorld::Ptr& ptr)
{
    if (ptr == getCurrentPlayerPtr())
        return &mwmp::Main::get().getLocalPlayer()->cast;
    else if (mwmp::Main::get().getCellController()->isLocalActor(ptr))
    {
        if (mwmp::LocalActor* localActor = mwmp::Main::get().getCellController()->getLocalActor(ptr))
            return &localActor->cast;
    }

    return nullptr;
}

Cast *MechanicsHelper::getDedicatedCast(const MWWorld::Ptr& ptr)
{
    if (mwmp::PlayerList::isDedicatedPlayer(ptr))
        return &mwmp::PlayerList::getPlayer(ptr)->cast;
    else if (mwmp::Main::get().getCellController()->isDedicatedActor(ptr))
    {
        if (mwmp::DedicatedActor* dedicatedActor = mwmp::Main::get().getCellController()->getDedicatedActor(ptr))
            return &dedicatedActor->cast;
    }

    return nullptr;
}

MWWorld::Ptr MechanicsHelper::getPlayerPtr(const Target& target)
{
    if (target.guid == mwmp::Main::get().getLocalPlayer()->guid)
    {
        return getCurrentPlayerPtr();
    }
    else
    {
        mwmp::DedicatedPlayer* dedicatedPlayer = mwmp::PlayerList::getPlayer(target.guid);

        if (dedicatedPlayer != nullptr)
        {
            return dedicatedPlayer->getPtr();
        }
    }

    return MWWorld::Ptr();
}

unsigned int MechanicsHelper::getActorId(const mwmp::Target& target)
{
    int actorId = -1;
    MWWorld::Ptr targetPtr;

    if (target.isPlayer)
    {
        targetPtr = getPlayerPtr(target);
    }
    else
    {
        auto controller = mwmp::Main::get().getCellController();
        if (controller->isLocalActor(target.refNum, target.mpNum))
        {
            if (mwmp::LocalActor* localActor = controller->getLocalActor(target.refNum, target.mpNum))
                targetPtr = localActor->getPtr();
        }
        else if (controller->isDedicatedActor(target.refNum, target.mpNum))
        {
            if (mwmp::DedicatedActor* dedicatedActor = controller->getDedicatedActor(target.refNum, target.mpNum))
                targetPtr = dedicatedActor->getPtr();
        }
    }

    if (!targetPtr.isEmpty())
    {
        actorId = targetPtr.getCellRef().getRefNum().mIndex;
    }

    return actorId;
}

mwmp::Item MechanicsHelper::getItem(const MWWorld::Ptr& itemPtr, int count)
{
    mwmp::Item item;

    if (itemPtr.getClass().isGold(itemPtr))
        item.refId = MWWorld::ContainerStore::sGoldId.serializeText();
    else
        item.refId = itemPtr.getCellRef().getRefId().serializeText();

    item.count = count;
    item.charge = itemPtr.getCellRef().getCharge();
    item.enchantmentCharge = itemPtr.getCellRef().getEnchantmentCharge();
    item.soul = itemPtr.getCellRef().getSoul().serializeText();

    return item;
}

mwmp::Target MechanicsHelper::getTarget(const MWWorld::Ptr& ptr)
{
    mwmp::Target target;
    clearTarget(target);

    if (!ptr.isEmpty())
    {
        if (ptr == getCurrentPlayerPtr())
        {
            target.isPlayer = true;
            target.guid = mwmp::Main::get().getLocalPlayer()->guid;
        }
        else if (mwmp::PlayerList::isDedicatedPlayer(ptr))
        {
            target.isPlayer = true;
            target.guid = mwmp::PlayerList::getPlayer(ptr)->guid;
        }
        else
        {
            MWWorld::CellRef *ptrRef = &ptr.getCellRef();

            if (ptrRef)
            {
                const auto [refNum, mpNum] = mwmp::Main::get().getCellController()->getActorNetworkId(ptr);

                target.isPlayer = false;
                target.refId = ptrRef->getRefId().serializeText();
                target.refNum = refNum;
                target.mpNum = mpNum;
                target.name = std::string(ptr.getClass().getName(ptr));
            }
        }
    }

    return target;
}

void MechanicsHelper::clearTarget(mwmp::Target& target)
{
    target.isPlayer = false;
    target.refId.clear();
    target.refNum = static_cast<unsigned int>(-1);
    target.mpNum = static_cast<unsigned int>(-1);

    target.name.clear();
}

bool MechanicsHelper::isEmptyTarget(const mwmp::Target& target)
{
    if (target.isPlayer == false && target.refId.empty())
        return true;

    return false;
}

void MechanicsHelper::syncLocalDynamicStatsForPtr(const MWWorld::Ptr& ptr)
{
    publishLocalDynamicStatsFromPtr(ptr, true);
}

void MechanicsHelper::syncLocalDynamicStatsForTarget(const mwmp::Target& target)
{
    if (isEmptyTarget(target))
        return;

    publishLocalDynamicStatsFromPtr(resolveTargetPtr(target), true);
}

void MechanicsHelper::queueLocalDynamicStatsForPtr(const MWWorld::Ptr& ptr)
{
    publishLocalDynamicStatsFromPtr(ptr, false);
}

void MechanicsHelper::queueLocalDynamicStatsForTarget(const mwmp::Target& target)
{
    if (isEmptyTarget(target))
        return;

    publishLocalDynamicStatsFromPtr(resolveTargetPtr(target), false);
}

void MechanicsHelper::assignAttackTarget(Attack* attack, const MWWorld::Ptr& target)
{
    if (target == getCurrentPlayerPtr())
    {
        attack->target.isPlayer = true;
        attack->target.guid = mwmp::Main::get().getLocalPlayer()->guid;
    }
    else if (mwmp::PlayerList::isDedicatedPlayer(target))
    {
        attack->target.isPlayer = true;
        attack->target.guid = mwmp::PlayerList::getPlayer(target)->guid;
    }
    else
    {
        MWWorld::CellRef *targetRef = &target.getCellRef();
        const auto [refNum, mpNum] = mwmp::Main::get().getCellController()->getActorNetworkId(target);

        attack->target.isPlayer = false;
        attack->target.refId = targetRef->getRefId().serializeText();
        attack->target.refNum = refNum;
        attack->target.mpNum = mpNum;
    }
}

void MechanicsHelper::resetAttack(Attack* attack)
{
    attack->target.isPlayer = false;
    attack->type = Attack::MELEE;
    attack->attackAnimation.clear();
    attack->rangedWeaponId.clear();
    attack->rangedAmmoId.clear();
    attack->damage = 0.f;
    attack->attackStrength = 0.f;
    attack->isHit = false;
    attack->success = false;
    attack->knockdown = false;
    attack->block = false;
    attack->pressed = false;
    attack->instant = false;
    attack->applyWeaponEnchantment = false;
    attack->applyAmmoEnchantment = false;
    attack->waitingForHitReaction = false;
    attack->hitReactionWaitFrames = 0;
    attack->hitPosition.pos[0] = attack->hitPosition.pos[1] = attack->hitPosition.pos[2] = 0;
    attack->target.guid = unassignedPacketGuid();
    attack->target.refId.clear();
    attack->target.refNum = 0;
    attack->target.mpNum = 0;
}

void MechanicsHelper::resetCast(Cast* cast)
{
    cast->target.isPlayer = false;
    cast->isHit = false;
    cast->success = false;
    cast->pressed = false;
    cast->instant = false;
    cast->hasProjectile = false;
    cast->spellId.clear();
    cast->itemId.clear();
    cast->target.guid = unassignedPacketGuid();
    cast->target.refId.clear();
    cast->target.refNum = 0;
    cast->target.mpNum = 0;
}

void MechanicsHelper::queueLocalAttackStart(
    const MWWorld::Ptr& attacker, bool isRanged, const std::string& attackAnimation)
{
    queueServerSimulationAttackStart(attacker, isRanged, attackAnimation);

    if (!mwmp::Main::isInitialized())
        return;

    Attack* attack = getLocalAttack(attacker);
    if (attack == nullptr)
        return;

    resetAttack(attack);
    attack->type = static_cast<char>(isRanged ? Attack::RANGED : Attack::MELEE);
    attack->pressed = true;
    attack->attackAnimation = attackAnimation;
    attack->shouldSend = true;
}

void MechanicsHelper::queueLocalMeleeAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
    const MWWorld::Ptr& weapon, float attackStrength, int attackType, bool isHit, bool success, float damage, bool block,
    const osg::Vec3f& hitPosition, bool applyWeaponEnchantment)
{
    queueServerSimulationMeleeAttack(attacker, victim, weapon, attackStrength, attackType, isHit, success, damage, block,
        hitPosition, applyWeaponEnchantment);

    if (!mwmp::Main::isInitialized())
        return;

    Attack* attack = getLocalAttack(attacker);
    if (attack == nullptr)
        return;

    resetAttack(attack);
    attack->type = static_cast<char>(Attack::MELEE);
    attack->pressed = false;
    attack->attackAnimation = attackTypeName(attackType);
    attack->attackStrength = attackStrength;
    attack->isHit = isHit && !victim.isEmpty();
    attack->success = success;
    attack->damage = std::max(0.f, damage);
    attack->block = block;
    attack->applyWeaponEnchantment = applyWeaponEnchantment;
    attack->hitPosition = getPositionFromVector(hitPosition);

    if (!weapon.isEmpty())
        attack->rangedWeaponId = weapon.getCellRef().getRefId().serializeText();

    if (attack->isHit)
        assignAttackTarget(attack, victim);

    attack->waitingForHitReaction = attack->isHit && attack->success && !attack->block;
    attack->hitReactionWaitFrames = attack->waitingForHitReaction ? getLocalHitReactionWaitFrames() : 0;
    attack->shouldSend = true;
}

void MechanicsHelper::queueLocalCastStart(const MWWorld::Ptr& caster, const ESM::RefId& spellId)
{
    if (!mwmp::Main::isInitialized() || spellId.empty())
        return;

    Cast* cast = getLocalCast(caster);
    if (cast == nullptr)
        return;

    resetCast(cast);
    cast->type = static_cast<char>(Cast::REGULAR);
    cast->pressed = true;
    cast->success = false;
    cast->instant = false;
    cast->spellId = spellId.serializeText();
    cast->shouldSend = true;
}

void MechanicsHelper::queueLocalCastRelease(const MWWorld::Ptr& caster, const MWWorld::Ptr& target, char castType,
    const ESM::RefId& id, bool success, bool instant)
{
    if (!mwmp::Main::isInitialized() || id.empty())
        return;

    if (castType != Cast::REGULAR && castType != Cast::ITEM)
        return;

    // Item cast packets do not carry pressed/success state, so only send them
    // once the local cast has succeeded.
    if (castType == Cast::ITEM && !success)
        return;

    Cast* cast = getLocalCast(caster);
    if (cast == nullptr)
        return;

    resetCast(cast);
    cast->type = castType;
    cast->pressed = false;
    cast->success = success;
    cast->instant = instant;

    if (castType == Cast::ITEM)
        cast->itemId = id.serializeText();
    else
        cast->spellId = id.serializeText();

    if (!target.isEmpty())
        cast->target = getTarget(target);

    cast->shouldSend = true;
}

void MechanicsHelper::queueLocalRangedAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
    const MWWorld::Ptr& weapon, const MWWorld::Ptr& projectile, float attackStrength, bool isHit, bool success,
    float damage, const osg::Vec3f& hitPosition, bool applyWeaponEnchantment, bool applyAmmoEnchantment)
{
    queueServerSimulationRangedAttack(attacker, victim, weapon, projectile, attackStrength, isHit, success, damage,
        hitPosition, applyWeaponEnchantment, applyAmmoEnchantment);

    if (!mwmp::Main::isInitialized())
        return;

    Attack* attack = getLocalAttack(attacker);
    if (attack == nullptr)
        return;

    resetAttack(attack);
    attack->type = static_cast<char>(Attack::RANGED);
    attack->pressed = false;
    attack->attackStrength = attackStrength;
    attack->isHit = isHit && !victim.isEmpty();
    attack->success = success;
    attack->damage = std::max(0.f, damage);
    attack->block = false;
    attack->applyWeaponEnchantment = applyWeaponEnchantment;
    attack->applyAmmoEnchantment = applyAmmoEnchantment;
    attack->hitPosition = getPositionFromVector(hitPosition);

    if (!weapon.isEmpty())
        attack->rangedWeaponId = weapon.getCellRef().getRefId().serializeText();

    if (!projectile.isEmpty() && projectile != weapon)
        attack->rangedAmmoId = projectile.getCellRef().getRefId().serializeText();

    if (attack->isHit)
        assignAttackTarget(attack, victim);

    attack->waitingForHitReaction = attack->isHit && attack->success;
    attack->hitReactionWaitFrames = attack->waitingForHitReaction ? getLocalHitReactionWaitFrames() : 0;
    attack->shouldSend = true;
}

void MechanicsHelper::finalizeLocalAttackReaction(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
{
    if (!mwmp::Main::isInitialized() || attacker.isEmpty() || victim.isEmpty())
        return;

    Attack* attack = getLocalAttack(attacker);
    if (attack == nullptr || !attack->shouldSend || !attack->success || attack->block
        || !attackTargetsPtr(*attack, victim))
        return;

    attack->knockdown = victim.getClass().getCreatureStats(victim).getKnockedDown();
    attack->waitingForHitReaction = false;
    attack->hitReactionWaitFrames = 0;
}

bool MechanicsHelper::shouldDeferLocalAttack(Attack& attack)
{
    if (!attack.shouldSend || !attack.waitingForHitReaction)
        return false;

    if (attack.hitReactionWaitFrames > 0)
    {
        --attack.hitReactionWaitFrames;
        return true;
    }

    attack.waitingForHitReaction = false;
    return false;
}

bool MechanicsHelper::getSpellSuccess(std::string spellId, const MWWorld::Ptr& caster)
{
    return Misc::Rng::roll0to99() < MWMechanics::getSpellSuccessChance(stringRefId(spellId), caster, nullptr, true, false);
}

bool MechanicsHelper::isTeamMember(const MWWorld::Ptr& playerChecked, const MWWorld::Ptr& playerWithTeam)
{
    bool isTeamMember = false;
    bool playerCheckedIsLocal = playerChecked == getCurrentPlayerPtr();
    bool playerCheckedIsDedicated = !playerCheckedIsLocal ? mwmp::PlayerList::isDedicatedPlayer(playerChecked) : false;
    bool playerWithTeamIsLocal = !playerCheckedIsLocal ? playerWithTeam == getCurrentPlayerPtr() : false;
    bool playerWithTeamIsDedicated = !playerWithTeamIsLocal ? mwmp::PlayerList::isDedicatedPlayer(playerWithTeam) : false;

    if (playerCheckedIsLocal || playerCheckedIsDedicated)
    {
        if (playerWithTeamIsLocal || playerWithTeamIsDedicated)
        {
            PacketGuid playerCheckedGuid;

            if (playerCheckedIsLocal)
                playerCheckedGuid = mwmp::Main::get().getLocalPlayer()->guid;
            else
                playerCheckedGuid = PlayerList::getPlayer(playerChecked)->guid;

            if (playerWithTeamIsLocal)
                isTeamMember = Utils::vectorContains(mwmp::Main::get().getLocalPlayer()->alliedPlayers, playerCheckedGuid);
            else
                isTeamMember = Utils::vectorContains(PlayerList::getPlayer(playerWithTeam)->alliedPlayers, playerCheckedGuid);
        }
    }

    return isTeamMember;
}

void MechanicsHelper::processAttack(Attack attack, const MWWorld::Ptr& attacker, bool applyAuthoritativeState)
{
    if (attacker.isEmpty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring attack from missing actor reference");
        return;
    }

    if (attack.type != Attack::MELEE && attack.type != Attack::RANGED)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring attack with invalid type %i", attack.type);
        return;
    }

    if (attack.isHit && (!std::isfinite(attack.damage) || attack.damage < 0 || !isFinitePosition(attack.hitPosition)))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Ignoring attack hit with invalid damage or hit position: damage=%f", attack.damage);
        return;
    }

    if (attack.type == Attack::RANGED && !isFiniteProjectileOrigin(attack.projectileOrigin))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring ranged attack with invalid projectile origin");
        return;
    }

    const std::string attackerName(attacker.getClass().getName(attacker));
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Processing attack from %s of type %i",
        attackerName.c_str(), attack.type);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- pressed: %s", attack.pressed ? "true" : "false");

    if (!attack.pressed)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "- success: %s", attack.success ? "true" : "false");

        if (attack.success)
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- damage: %f", attack.damage);
    }
    else
    {
        if (attack.type == attack.MELEE)
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- animation: %s", attack.attackAnimation.c_str());
    }

    MWMechanics::CreatureStats& attackerStats = attacker.getClass().getCreatureStats(attacker);
    std::string_view attackType = resolveRemoteAttackType(attack, attackerStats);

    if (attack.type == Attack::MELEE || attack.type == Attack::RANGED)
        attackerStats.setDrawState(MWMechanics::DrawState::Weapon);

    if (!attackType.empty())
        attackerStats.setAttackType(attackType);

    if (attack.pressed)
    {
        attackerStats.setAttackingOrSpell(true);

        try
        {
            MWBase::Environment::get().getMechanicsManager()->replayAttackStart(attacker, attackType);
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to replay attack start for %s: %s",
                attackerName.c_str(), e.what());
        }
    }
    else
    {
        const float replayAttackStrength = getVisibleRemoteAttackStrength(attack.attackStrength);

        try
        {
            MWBase::Environment::get().getMechanicsManager()->replayAttackRelease(
                attacker, attackType, replayAttackStrength);
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to replay attack release for %s: %s",
                attackerName.c_str(), e.what());
        }

        attackerStats.setAttackingOrSpell(false);
    }

    MWWorld::Ptr victim = resolveTargetPtr(attack.target, attacker.getCell());

    if (attack.isHit)
    {
        if (victim.isEmpty())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring attack hit with unresolved target: %s",
                describeTarget(attack.target).c_str());
            return;
        }

        if (!applyAuthoritativeState)
        {
            const float visualDamageCue = attack.success ? std::max(attack.damage, 1.f) : 0.f;
            playNetworkHitCue(attack, attacker, victim);
            applyAttackReaction(attack, victim, visualDamageCue);
            return;
        }

        bool isRanged = attack.type == attack.RANGED;

        MWWorld::Ptr weaponPtr;
        MWWorld::Ptr ammoPtr;

        bool usedTempWeapon = false;
        bool usedTempRangedAmmo = false;

        // Get the attacker's current weapon
        //
        // Note: if using hand-to-hand, the weapon is equal to inv.end()
        if (attacker.getClass().hasInventoryStore(attacker))
        {
            MWWorld::InventoryStore &inventoryStore = attacker.getClass().getInventoryStore(attacker);
            MWWorld::ContainerStoreIterator weaponSlot = inventoryStore.getSlot(
                MWWorld::InventoryStore::Slot_CarriedRight);

            weaponPtr = weaponSlot != inventoryStore.end() ? *weaponSlot : MWWorld::Ptr();

            // Is the currently selected weapon different from the one recorded for this attack?
            // If so, try to find the correct one in the attacker's inventory and use it here. If it
            // no longer exists, add it back temporarily.
            const ESM::RefId attackWeaponId = stringRefId(attack.rangedWeaponId);
            if (!attackWeaponId.empty())
            {
                if (!weaponPtr.isEmpty() && weaponPtr.getCellRef().getRefId() != attackWeaponId)
                    weaponPtr = MWWorld::Ptr();

                if (weaponPtr.isEmpty())
                {
                    weaponPtr = inventoryStore.search(attackWeaponId);

                    if (weaponPtr.isEmpty())
                    {
                        try
                        {
                            weaponPtr = *attacker.getClass().getContainerStore(attacker).add(attackWeaponId, 1, false);
                            usedTempWeapon = true;
                        }
                        catch (const std::exception& e)
                        {
                            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring attack weapon %s: %s",
                                attack.rangedWeaponId.c_str(), e.what());
                        }
                    }
                }
            }

            if (isRanged)
            {
                if (!attack.rangedAmmoId.empty())
                {
                    MWWorld::ContainerStoreIterator ammoSlot = inventoryStore.getSlot(
                        MWWorld::InventoryStore::Slot_Ammunition);
                    ammoPtr = ammoSlot != inventoryStore.end() ? *ammoSlot : MWWorld::Ptr();

                    const ESM::RefId rangedAmmoId = stringRefId(attack.rangedAmmoId);
                    if (!ammoPtr.isEmpty() && ammoPtr.getCellRef().getRefId() != rangedAmmoId)
                        ammoPtr = MWWorld::Ptr();

                    if (ammoPtr.isEmpty() && !rangedAmmoId.empty())
                    {
                        ammoPtr = inventoryStore.search(rangedAmmoId);

                        if (ammoPtr.isEmpty())
                        {
                            try
                            {
                                ammoPtr
                                    = *attacker.getClass().getContainerStore(attacker).add(rangedAmmoId, 1, false);
                                usedTempRangedAmmo = true;
                            }
                            catch (const std::exception& e)
                            {
                                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring ranged attack ammo %s: %s",
                                    attack.rangedAmmoId.c_str(), e.what());
                            }
                        }
                    }
                }
            }

            if (!weaponPtr.isEmpty() && weaponPtr.getType() != ESM::Weapon::sRecordId)
                weaponPtr = MWWorld::Ptr();
        }

        if (!weaponPtr.isEmpty())
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- weapon: %s\n- isRanged: %s\n- applyWeaponEnchantment: %s\n- applyAmmoEnchantment: %s",
                weaponPtr.getCellRef().getRefId().serializeText().c_str(), isRanged ? "true" : "false", attack.applyWeaponEnchantment ? "true" : "false",
                attack.applyAmmoEnchantment ? "true" : "false");

            if (attack.success && attack.applyWeaponEnchantment)
            {
                MWMechanics::CastSpell cast(attacker, victim, isRanged);
                cast.mHitPosition = attack.hitPosition.asVec3();
                cast.cast(weaponPtr, false);
            }

            if (isRanged && !ammoPtr.isEmpty() && attack.success && attack.applyAmmoEnchantment)
            {
                MWMechanics::CastSpell cast(attacker, victim, isRanged);
                cast.mHitPosition = attack.hitPosition.asVec3();
                cast.cast(ammoPtr, false);
            }
        }

        if (!victim.isEmpty())
        {
            bool isHealthDamage = true;
            float damage = attack.success ? attack.damage : 0.f;

            if (weaponPtr.isEmpty())
            {
                if (attacker.getClass().isBipedal(attacker))
                {
                    MWMechanics::CreatureStats &victimStats = victim.getClass().getCreatureStats(victim);
                    isHealthDamage = victimStats.isParalyzed() || victimStats.getKnockedDown();
                }
            }

            if (!isRanged && attack.block)
            {
                victim.getClass().getCreatureStats(victim).setBlock(true);
                damage = 0;
            }

            damage = applyAttackDamageModifiers(damage, attacker, victim, isHealthDamage);

            std::map<std::string, float> damages;
            damages[isHealthDamage ? "health" : "fatigue"] = damage;

            const ESM::RefId object = weaponPtr.isEmpty() ? ESM::RefId() : weaponPtr.getCellRef().getRefId();
            MWMechanics::CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);
            const float healthBefore = victimStats.getHealth().getCurrent();
            const float fatigueBefore = victimStats.getFatigue().getCurrent();
            try
            {
                victim.getClass().onHit(victim, damages, object, attacker, attack.success,
                    isRanged ? MWMechanics::DamageSourceType::Ranged : MWMechanics::DamageSourceType::Melee);
            }
            catch (const std::exception& e)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Network hit side effects failed for %s; applying authoritative damage fallback: %s",
                    describeTarget(getTarget(victim)).c_str(), e.what());
                if (attack.success)
                    applyNetworkHitDamageFallback(victim, isHealthDamage, damage, healthBefore, fatigueBefore);
            }
            applyAttackReaction(attack, victim, damage);

            if (attack.success)
                syncDynamicStatsFromPtr(victim);
        }

        // Remove temporary items that may have been added above for serialized attacks.
        if (attacker.getClass().hasInventoryStore(attacker))
        {
            MWWorld::InventoryStore &inventoryStore = attacker.getClass().getInventoryStore(attacker);

            if (usedTempWeapon && !weaponPtr.isEmpty())
                inventoryStore.remove(weaponPtr, 1);
            
            if (usedTempRangedAmmo && !ammoPtr.isEmpty())
                inventoryStore.remove(ammoPtr, 1);
        }
    }
}

void MechanicsHelper::processCast(Cast cast, const MWWorld::Ptr& caster, bool applyAuthoritativeState)
{
    if (caster.isEmpty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring cast from missing actor reference");
        return;
    }

    if (cast.type != Cast::REGULAR && cast.type != Cast::ITEM)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring cast with invalid type %i", cast.type);
        return;
    }

    if ((cast.type == Cast::REGULAR && cast.spellId.empty()) || (cast.type == Cast::ITEM && cast.itemId.empty()))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring cast with empty spell or item id");
        return;
    }

    if (cast.hasProjectile && !isFiniteProjectileOrigin(cast.projectileOrigin))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring cast with invalid projectile origin");
        return;
    }

    const std::string casterName(caster.getClass().getName(caster));
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Processing cast from %s of type %i",
        casterName.c_str(), cast.type);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- pressed: %s", cast.pressed ? "true" : "false");

    if (!cast.pressed)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "- success: %s", cast.success ? "true" : "false");
    }

    caster.getClass().getCreatureStats(caster).setAttackingOrSpell(cast.pressed);
    MWMechanics::CreatureStats &casterStats = caster.getClass().getCreatureStats(caster);

    MWWorld::Ptr victim;
    bool castApplied = false;

    if (cast.type == cast.REGULAR)
    {
        const ESM::RefId spellId = stringRefId(cast.spellId);
        casterStats.getSpells().setSelectedSpell(spellId);

        LOG_APPEND(TimedLog::LOG_VERBOSE, "- spellId: %s", cast.spellId.c_str());

        if (!applyAuthoritativeState)
            return;

        if (cast.success)
        {
            victim = resolveTargetPtr(cast.target, caster.getCell());
            const ESM::Spell* spell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(spellId);
            if (spell == nullptr)
            {
                LOG_APPEND(TimedLog::LOG_ERROR, "- Ignoring cast with unknown spellId: %s", cast.spellId.c_str());
                return;
            }

            MWMechanics::CastSpell remoteCast(caster, victim, false, cast.instant);
            remoteCast.mHitPosition = getSpellHitPosition(caster, victim);
            remoteCast.mAlwaysSucceed = true;
            castApplied = remoteCast.cast(spell);
        }
    }
    else if (cast.type == cast.ITEM)
    {
        casterStats.getSpells().setSelectedSpell(ESM::RefId());
        LOG_APPEND(TimedLog::LOG_VERBOSE, "- itemId: %s", cast.itemId.c_str());

        if (!applyAuthoritativeState)
            return;

        if (!caster.getClass().hasInventoryStore(caster))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring item cast from actor without inventory store");
            return;
        }

        victim = resolveTargetPtr(cast.target, caster.getCell());
        MWWorld::InventoryStore& inventoryStore = caster.getClass().getInventoryStore(caster);
        const ESM::RefId itemId = stringRefId(cast.itemId);

        MWWorld::ContainerStoreIterator it = inventoryStore.begin();
        for (; it != inventoryStore.end(); ++it)
        {
            if (it->getCellRef().getRefId() == itemId)
                break;
        }

        // Add the item if it's missing
        if (it == inventoryStore.end())
            it = caster.getClass().getContainerStore(caster).add(itemId, 1, false);

        inventoryStore.setSelectedEnchantItem(it);
        MWMechanics::CastSpell remoteCast(caster, victim, false, cast.instant);
        remoteCast.mHitPosition = getSpellHitPosition(caster, victim);
        castApplied = remoteCast.cast(*it);
        inventoryStore.setSelectedEnchantItem(inventoryStore.end());
    }

    if (castApplied)
        syncDynamicStatsAfterCombatEffect(caster, victim);
}

void MechanicsHelper::createSpellGfx(const MWWorld::Ptr& targetPtr, const std::vector<ESM::ActiveEffect>& mEffects)
{
    for (auto&& effect : mEffects)
    {
        const ESM::MagicEffect* magicEffect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(effect.mEffectId);

        const ESM::Static* castStatic;
        if (!magicEffect->mHit.empty())
            castStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find(magicEffect->mHit);
        else
            castStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find(ESM::RefId::stringRefId("VFX_DefaultHit"));

        bool loop = (magicEffect->mData.mFlags & ESM::MagicEffect::ContinuousVfx) != 0;
        // Note: in case of non actor, a free effect should be fine as well
        MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(targetPtr);
        if (anim && !castStatic->mModel.empty())
        {
            const auto model = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(castStatic->mModel));
            anim->addEffect(model, magicEffect->mId.serializeText(), loop, "", magicEffect->mParticle);
        }
    }
}

bool MechanicsHelper::isStackingSpell(const std::string& id)
{
    return !MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(stringRefId(id));
}

bool MechanicsHelper::doesEffectListContainEffect(const ESM::EffectList& effectList, const ESM::RefId& effectId,
    const ESM::RefId& attributeId, const ESM::RefId& skillId)
{
    for (const auto &effect : effectList.mList)
    {
        if (effect.mData.mEffectID == effectId)
        {
            if (attributeId.empty() || effect.mData.mAttribute == attributeId)
            {
                if (skillId.empty() || effect.mData.mSkill == skillId)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void MechanicsHelper::unequipItemsByEffect(const MWWorld::Ptr& ptr, short enchantmentType, const ESM::RefId& effectId,
    const ESM::RefId& attributeId, const ESM::RefId& skillId)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::InventoryStore &ptrInventory = ptr.getClass().getInventoryStore(ptr);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        if (ptrInventory.getSlot(slot) != ptrInventory.end())
        {
            MWWorld::ConstContainerStoreIterator itemIterator = ptrInventory.getSlot(slot);
            ESM::RefId enchantmentName = itemIterator->getClass().getEnchantment(*itemIterator);

            if (!enchantmentName.empty())
            {
                const ESM::Enchantment* enchantment = world->getStore().get<ESM::Enchantment>().find(enchantmentName);

                if (enchantment->mData.mType == enchantmentType && doesEffectListContainEffect(enchantment->mEffects, effectId, attributeId, skillId))
                    ptrInventory.unequipSlot(slot);
            }
        }
    }
}

MWWorld::Ptr MechanicsHelper::getItemPtrFromStore(const mwmp::Item& item, MWWorld::ContainerStore& store)
{
    MWWorld::Ptr closestPtr;

    for (MWWorld::ContainerStoreIterator storeIterator = store.begin(); storeIterator != store.end(); ++storeIterator)
    {
        // Enchantment charges are often in the process of refilling themselves, so don't check for them here
        if (storeIterator->getCellRef().getRefId() == stringRefId(item.refId) &&
            item.count == storeIterator->getCellRef().getCount() &&
            item.charge == storeIterator->getCellRef().getCharge() &&
            storeIterator->getCellRef().getSoul() == stringRefId(item.soul))
        {
            // If we have no closestPtr, set it to the Ptr corresponding to this storeIterator; otherwise, make
            // sure the storeIterator's enchantmentCharge is closer to our goal than that of the previous closestPtr
            if (closestPtr.isEmpty() || std::abs(storeIterator->getCellRef().getEnchantmentCharge() - item.enchantmentCharge) <
                std::abs(closestPtr.getCellRef().getEnchantmentCharge() - item.enchantmentCharge))
            {
                closestPtr = *storeIterator;
            }
        }
    }

    return closestPtr;
}

