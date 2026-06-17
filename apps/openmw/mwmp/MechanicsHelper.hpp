#ifndef OPENMW_MECHANICSHELPER_HPP
#define OPENMW_MECHANICSHELPER_HPP

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "../mwworld/containerstore.hpp"

#include <osg/Vec3f>

#include <string>

namespace MechanicsHelper
{
    osg::Vec3f getLinearInterpolation(osg::Vec3f start, osg::Vec3f end, float percent);
    float getRemoteMovementInterpolationFactor(float dt);
    float sanitizeMovementComponent(float value);
    void deriveMissingMovementDirection(
        ESM::Position& direction, const ESM::Position& currentPosition, const ESM::Position& previousPosition);
    ESM::Position getPositionFromVector(osg::Vec3f vector);

    void spawnLeveledCreatures(MWWorld::CellStore* cellStore);

    bool isUsingRangedWeapon(const MWWorld::Ptr& ptr);

    mwmp::Attack *getLocalAttack(const MWWorld::Ptr& ptr);
    mwmp::Attack *getDedicatedAttack(const MWWorld::Ptr& ptr);

    mwmp::Cast *getLocalCast(const MWWorld::Ptr& ptr);
    mwmp::Cast *getDedicatedCast(const MWWorld::Ptr& ptr);

    MWWorld::Ptr getPlayerPtr(const mwmp::Target& target);
    unsigned int getActorId(const mwmp::Target& target);

    mwmp::Item getItem(const MWWorld::Ptr& itemPtr, int count);
    mwmp::Target getTarget(const MWWorld::Ptr& ptr);
    void clearTarget(mwmp::Target& target);
    bool isEmptyTarget(const mwmp::Target& target);
    void syncLocalDynamicStatsForPtr(const MWWorld::Ptr& ptr);
    void syncLocalDynamicStatsForTarget(const mwmp::Target& target);
    void queueLocalDynamicStatsForPtr(const MWWorld::Ptr& ptr);
    void queueLocalDynamicStatsForTarget(const mwmp::Target& target);

    void assignAttackTarget(mwmp::Attack* attack, const MWWorld::Ptr& target);
    void resetAttack(mwmp::Attack* attack);
    void resetCast(mwmp::Cast* cast);
    void queueLocalAttackStart(const MWWorld::Ptr& attacker, bool isRanged, const std::string& attackAnimation);
    void queueLocalMeleeAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
        float attackStrength, int attackType, bool isHit, bool success, float damage, bool block,
        const osg::Vec3f& hitPosition, bool applyWeaponEnchantment);
    void queueLocalRangedAttack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
        const MWWorld::Ptr& projectile, float attackStrength, bool isHit, bool success, float damage,
        const osg::Vec3f& hitPosition, bool applyWeaponEnchantment, bool applyAmmoEnchantment);
    void finalizeLocalAttackReaction(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim);
    bool shouldDeferLocalAttack(mwmp::Attack& attack);
    void queueLocalCastStart(const MWWorld::Ptr& caster, const ESM::RefId& spellId);
    void queueLocalCastRelease(const MWWorld::Ptr& caster, const MWWorld::Ptr& target, char castType,
        const ESM::RefId& id, bool success, bool instant);

    // See whether playerChecked belongs to playerWithTeam's team
    // Note: This is not supposed to also check if playerWithTeam is on playerChecked's
    //       team, because it should technically be possible to be allied to someone
    //       who isn't mutually allied to you
    bool isTeamMember(const MWWorld::Ptr& playerChecked, const MWWorld::Ptr& playerWithTeam);

    bool getSpellSuccess(std::string spellId, const MWWorld::Ptr& caster);

    void processAttack(mwmp::Attack attack, const MWWorld::Ptr& attacker, bool applyAuthoritativeState = true);
    void processCast(mwmp::Cast cast, const MWWorld::Ptr& caster, bool applyAuthoritativeState = true);

    void createSpellGfx(const MWWorld::Ptr& targetPtr, const std::vector<ESM::ActiveEffect>& mEffects);

    bool isStackingSpell(const std::string& id);
    bool doesEffectListContainEffect(const ESM::EffectList& effectList, const ESM::RefId& effectId,
        const ESM::RefId& attributeId = {}, const ESM::RefId& skillId = {});
    void unequipItemsByEffect(const MWWorld::Ptr& ptr, short enchantmentType, const ESM::RefId& effectId,
        const ESM::RefId& attributeId = {}, const ESM::RefId& skillId = {});

    MWWorld::Ptr getItemPtrFromStore(const mwmp::Item& item, MWWorld::ContainerStore& store);
}


#endif //OPENMW_MECHANICSHELPER_HPP

