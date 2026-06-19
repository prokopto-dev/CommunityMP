#include <apps/openmw/mwmechanics/steering.hpp>
#include <algorithm>
#include <boost/algorithm/clamp.hpp>
#include <cmath>
#include <components/esm/attr.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"

#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/actor.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "CellController.hpp"
#include "DedicatedPlayer.hpp"
#include "GUIController.hpp"
#include "Main.hpp"
#include "MechanicsHelper.hpp"
#include "RecordHelper.hpp"

#ifdef DrawState
#undef DrawState
#endif

using namespace mwmp;

namespace
{
    ESM::RefId stringRefId(const std::string& id)
    {
        return ESM::RefId::stringRefId(id);
    }

    ESM::RefId activeSpellSourceId(const mwmp::ActiveSpell& activeSpell)
    {
        if (!activeSpell.params.mSourceSpellId.empty())
            return activeSpell.params.mSourceSpellId;

        return stringRefId(activeSpell.id);
    }

    ESM::RefId activeSpellActiveId(const mwmp::ActiveSpell& activeSpell)
    {
        if (!activeSpell.params.mActiveSpellId.empty())
            return activeSpell.params.mActiveSpellId;

        return activeSpellSourceId(activeSpell);
    }

    std::string refIdToString(const ESM::RefId& id)
    {
        return id.serializeText();
    }
}

DedicatedPlayer::DedicatedPlayer(PacketGuid guid)
    : BasePlayer(guid)
{
    reference = 0;
    attack.pressed = false;
    cast.pressed = false;
    drawState = static_cast<char>(MWMechanics::DrawState::Nothing);
    movementFlags = 0;
    animation.groupname = "";
    sound = "";

    creatureStats.mDead = false;
    // Give this new character a temporary high fatigue so it doesn't spawn on
    // the ground
    creatureStats.mDynamic[2].mBase = 1000;

    attack.instant = false;

    MWBase::World* world = MWBase::Environment::get().getWorld();

    cell = *world->getStore().get<ESM::Cell>().find(RecordHelper::getPlaceholderInteriorCellName());
    position.pos[0] = position.pos[1] = position.pos[2] = 0;

    npc = *world->getPlayerPtr().get<ESM::NPC>()->mBase;
    npc.mId = ESM::RefId();
    previousRace = npc.mRace;
    previousDisplayCreatureName = false;

    hasReceivedInitialEquipment = false;
    hasFinishedInitialTeleportation = false;
    hasReceivedInitialPosition = false;
    hasChangedCell = true;
    markerEnabled = false;
    isLevitationPurged = false;
    hasPendingSpellsActiveChanges = false;
    hasPendingEquipmentApplication = false;

    isJumping = false;
    wasJumping = false;
    mRemoteVelocity = osg::Vec3f();
    mSmoothedRemoteSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(movementSampleIntervalSeconds);
    mSmoothedRemoteLatencySeconds = sanitizeMovementLatencySeconds(movementLatencySeconds);
    mRemoteJitterSeconds = 0.f;
    mRemotePacketAgeSeconds = 0.f;
    mHasRemoteVelocity = false;
    mHasRemoteTimingEstimate = false;
}

DedicatedPlayer::~DedicatedPlayer() {}

void DedicatedPlayer::update(float dt)
{
    if (!reference)
        return;

    if (hasPendingEquipmentApplication)
        setEquipment();

    if (hasReceivedInitialPosition)
    {
        move(dt);
    }

    setAnimFlags();

    MWMechanics::CreatureStats* ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);

    MWMechanics::DynamicStat<float> value;

    if (creatureStats.mDead)
    {
        value.readState(creatureStats.mDynamic[0]);
        ptrCreatureStats->setHealth(value);
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }

    if (ptrCreatureStats->isDead())
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);

    ptrCreatureStats->setAttacked(false);

    ptrCreatureStats->getAiSequence().stopCombat();

    ptrCreatureStats->setAlarmed(false);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Alarm, 0);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Fight, 0);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Flee, 0);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Hello, 0);
}

void DedicatedPlayer::move(float dt)
{
    if (!reference)
        return;

    const ESM::Position previousVisualPosition = ptr.getRefData().getPosition();
    ESM::Position refPos = previousVisualPosition;
    MWBase::World* world = MWBase::Environment::get().getWorld();
    constexpr float maxInterpolationDistance = 512.f;
    if (std::isfinite(dt) && dt > 0.f)
        mRemotePacketAgeSeconds = std::min(mRemotePacketAgeSeconds + dt, 0.15f);

    // Apply interpolation only if the position hasn't changed too much from last time
    bool shouldInterpolate = std::abs(position.pos[0] - refPos.pos[0]) < maxInterpolationDistance
        && std::abs(position.pos[1] - refPos.pos[1]) < maxInterpolationDistance
        && std::abs(position.pos[2] - refPos.pos[2]) < maxInterpolationDistance;

    // Do not interpolate immediately after a cell/reference change; the previous
    // position can belong to a different cell or temporary placeholder.
    if (shouldInterpolate && !hasChangedCell)
    {
        const osg::Vec3f target = MechanicsHelper::getPredictedRemoteMovementTarget(
            position, direction, mRemoteVelocity, mHasRemoteVelocity, mRemotePacketAgeSeconds,
            mHasRemoteTimingEstimate ? mSmoothedRemoteSampleIntervalSeconds : movementSampleIntervalSeconds,
            mHasRemoteTimingEstimate ? mSmoothedRemoteLatencySeconds : movementLatencySeconds,
            mHasRemoteTimingEstimate ? mRemoteJitterSeconds : 0.f);
        const float distanceToTarget = (target - refPos.asVec3()).length();
        const float positionFactor = MechanicsHelper::getRemoteMovementInterpolationFactor(
            dt, distanceToTarget, MechanicsHelper::hasRemoteTranslationIntent(direction),
            mHasRemoteTimingEstimate ? mRemoteJitterSeconds : 0.f);
        osg::Vec3f lerp = MechanicsHelper::getLinearInterpolation(refPos.asVec3(), target,
            positionFactor);
        const osg::Vec3f rotation = MechanicsHelper::getInterpolatedRemoteRotation(
            refPos, position, MechanicsHelper::getRemoteRotationInterpolationFactor(dt));

        world->moveObject(ptr, lerp);
        world->rotateObject(ptr, osg::Vec3f(rotation.x(), 0, rotation.z()));
        setMovementSettingsFromVisualDelta(previousVisualPosition);
    }
    else
    {
        setPosition();
        hasChangedCell = false;
    }
}

bool DedicatedPlayer::readPositionPacket()
{
    const bool hadAcceptedPosition = hasAcceptedPositionPacket;
    const ESM::Position previousAcceptedPosition = acceptedPosition;

    if (!acceptPositionPacket())
        return false;

    if (hadAcceptedPosition)
    {
        MechanicsHelper::deriveMissingMovementDirection(direction, position, previousAcceptedPosition);
        acceptedDirection = direction;
    }
    updateRemoteMovementEstimate(previousAcceptedPosition, hadAcceptedPosition);

    updateMarker();

    if (!reference)
        return false;

    if (!hasReceivedInitialPosition)
    {
        hasReceivedInitialPosition = true;
        setPosition();
        hasChangedCell = false;
        return true;
    }

    return true;
}

bool DedicatedPlayer::normalizePositionPacket()
{
    if (readPositionPacket())
        return true;

    return reference != nullptr && hasAcceptedPositionPacket;
}

void DedicatedPlayer::setPosition()
{
    if (!reference)
        return;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->moveObject(ptr, position.asVec3());
    world->rotateObject(ptr, osg::Vec3f(position.rot[0], 0, position.rot[2]));
    setMovementSettings();
}

void DedicatedPlayer::setMovementSettings()
{
    setMovementSettings(direction);
}

void DedicatedPlayer::setMovementSettings(const ESM::Position& movementDirection)
{
    MWMechanics::Movement* move = &ptr.getClass().getMovementSettings(ptr);

    for (int i = 0; i < 3; ++i)
        move->mPosition[i] = std::isfinite(movementDirection.pos[i]) ? movementDirection.pos[i] : 0.f;

    // Make sure the values are valid, or we'll get an infinite error loop
    if (std::isfinite(movementDirection.rot[0]) && std::isfinite(movementDirection.rot[1])
        && std::isfinite(movementDirection.rot[2]))
    {
        move->mRotation[0] = movementDirection.rot[0];
        move->mRotation[1] = movementDirection.rot[1];
        move->mRotation[2] = movementDirection.rot[2];
    }
}

void DedicatedPlayer::setMovementSettingsFromVisualDelta(const ESM::Position& previousPosition)
{
    ESM::Position animationDirection = direction;
    animationDirection.pos[0] = 0.f;
    animationDirection.pos[1] = 0.f;
    const ESM::Position currentPosition = ptr.getRefData().getPosition();
    MechanicsHelper::deriveMissingMovementDirection(animationDirection, ptr.getRefData().getPosition(), previousPosition);

    if (animationDirection.pos[0] == 0.f && animationDirection.pos[1] == 0.f)
    {
        animationDirection.pos[0] = MechanicsHelper::sanitizeMovementComponent(direction.pos[0]);
        animationDirection.pos[1] = MechanicsHelper::sanitizeMovementComponent(direction.pos[1]);
    }

    if (animationDirection.pos[0] == 0.f && animationDirection.pos[1] == 0.f)
        MechanicsHelper::deriveMissingMovementDirection(animationDirection, position, currentPosition);

    const float deltaZ = currentPosition.pos[2] - previousPosition.pos[2];
    constexpr float minDerivedJumpRise = 0.5f;
    if (animationDirection.pos[2] <= 0.f && (isJumping || deltaZ > minDerivedJumpRise))
        animationDirection.pos[2] = 1.f;

    setMovementSettings(animationDirection);
}

void DedicatedPlayer::applyRemoteJumpMovementCue(bool wasRemoteJumping)
{
    if (!reference)
        return;

    using namespace MWMechanics;

    Movement* move = &ptr.getClass().getMovementSettings(ptr);
    const bool wantsJump = isJumping || (movementFlags & CreatureStats::Flag_ForceJump) != 0;

    if (wantsJump)
    {
        if (!std::isfinite(move->mPosition[2]) || move->mPosition[2] <= 0.f)
            move->mPosition[2] = 1.f;
    }
    else if (wasRemoteJumping && !isFlying && !hasTcl && std::isfinite(move->mPosition[2]) && move->mPosition[2] > 0.f)
    {
        move->mPosition[2] = 0.f;
    }
}

void DedicatedPlayer::updateRemoteMovementEstimate(const ESM::Position& previousPosition, bool hadPositionData)
{
    const auto now = std::chrono::steady_clock::now();
    const bool hasPreviousPacket = mLastRemotePositionPacket != std::chrono::steady_clock::time_point();
    const bool canUseArrivalDelta = hasPreviousPacket && !hasChangedCell;
    const float deltaSeconds = canUseArrivalDelta
        ? std::chrono::duration<float>(now - mLastRemotePositionPacket).count()
        : 0.f;
    mLastRemotePositionPacket = now;
    updateRemoteTimingEstimate(deltaSeconds, canUseArrivalDelta);

    mRemotePacketAgeSeconds = 0.f;
    if (!hadPositionData || !canUseArrivalDelta)
    {
        resetRemoteMovementEstimate();
        return;
    }

    const float sampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(mSmoothedRemoteSampleIntervalSeconds);
    const float minVelocityDeltaSeconds = sampleIntervalSeconds * 0.50f;
    const float maxVelocityDeltaSeconds = std::clamp(sampleIntervalSeconds * 6.f, 0.050f, 0.150f);
    const float velocityDeltaSeconds = std::isfinite(deltaSeconds)
        ? std::clamp(deltaSeconds, minVelocityDeltaSeconds, maxVelocityDeltaSeconds)
        : sampleIntervalSeconds;
    const osg::Vec3f sampleVelocity = MechanicsHelper::estimateRemoteVelocity(
        previousPosition, position, velocityDeltaSeconds);
    const bool hasTranslationIntent = MechanicsHelper::hasRemoteTranslationIntent(direction);
    const bool hasVelocitySample = hasTranslationIntent && sampleVelocity.length2() > 1.f;
    if (!hasVelocitySample)
    {
        if (!hasTranslationIntent)
            resetRemoteMovementEstimate();
        else if (mHasRemoteVelocity)
        {
            mRemoteVelocity *= 0.85f;
            mHasRemoteVelocity = mRemoteVelocity.length2() > 1.f;
        }
        return;
    }

    mRemoteVelocity = MechanicsHelper::smoothRemoteVelocity(
        mRemoteVelocity, sampleVelocity, deltaSeconds, mHasRemoteVelocity);
    mHasRemoteVelocity = true;
}

void DedicatedPlayer::resetRemoteMovementEstimate()
{
    mRemoteVelocity = osg::Vec3f();
    mRemotePacketAgeSeconds = 0.f;
    mHasRemoteVelocity = false;
}

void DedicatedPlayer::updateRemoteTimingEstimate(float arrivalDeltaSeconds, bool hasPreviousPacket)
{
    const float sampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(movementSampleIntervalSeconds);
    const float latencySeconds = sanitizeMovementLatencySeconds(movementLatencySeconds);

    if (!hasPreviousPacket || !mHasRemoteTimingEstimate)
    {
        mSmoothedRemoteSampleIntervalSeconds = sampleIntervalSeconds;
        mSmoothedRemoteLatencySeconds = latencySeconds;
        mRemoteJitterSeconds = 0.f;
        mHasRemoteTimingEstimate = true;
        return;
    }

    mSmoothedRemoteSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(
        MechanicsHelper::smoothRemoteTimingValue(mSmoothedRemoteSampleIntervalSeconds, sampleIntervalSeconds,
            arrivalDeltaSeconds, true));
    mSmoothedRemoteLatencySeconds = sanitizeMovementLatencySeconds(
        MechanicsHelper::smoothRemoteTimingValue(mSmoothedRemoteLatencySeconds, latencySeconds,
            arrivalDeltaSeconds, true));

    const float expectedIntervalSeconds = sanitizeMovementSampleIntervalSeconds(mSmoothedRemoteSampleIntervalSeconds);
    const float jitterSampleSeconds = std::isfinite(arrivalDeltaSeconds) && arrivalDeltaSeconds > 0.f
        ? std::abs(arrivalDeltaSeconds - expectedIntervalSeconds)
        : 0.f;
    mRemoteJitterSeconds = MechanicsHelper::sanitizeRemoteMovementJitterSeconds(
        MechanicsHelper::smoothRemoteTimingValue(mRemoteJitterSeconds, jitterSampleSeconds, arrivalDeltaSeconds, true));
}

void DedicatedPlayer::resetRemoteTimingEstimate()
{
    mSmoothedRemoteSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(movementSampleIntervalSeconds);
    mSmoothedRemoteLatencySeconds = sanitizeMovementLatencySeconds(movementLatencySeconds);
    mRemoteJitterSeconds = 0.f;
    mHasRemoteTimingEstimate = false;
}

void DedicatedPlayer::setBaseInfo()
{
    // Use the previous race if the new one doesn't exist
    if (!RecordHelper::doesRecordIdExist<ESM::Race>(npc.mRace))
        npc.mRace = previousRace;

    if (!reference)
    {
        npc.mId = RecordHelper::createRecord(npc)->mId;
        createReference(npc.mId);
    }
    else
    {
        RecordHelper::overrideRecord(npc);
        reloadPtr();
    }

    // Only set equipment if the player isn't disguised as a creature
    if (ptr.get<ESM::NPC>() != nullptr)
        setEquipment();

    setShapeshift();
    if (hasPendingSpellsActiveChanges)
        applySpellsActiveChanges();

    previousRace = npc.mRace;
}

void DedicatedPlayer::setStatsDynamic()
{
    if (!reference)
        return;

    MWMechanics::CreatureStats* ptrCreatureStats = &getPtr().getClass().getCreatureStats(getPtr());
    MWMechanics::DynamicStat<float> value;

    if (creatureStats.mDead)
    {
        die();
        return;
    }

    auto applyDynamicStat = [&](int index) {
        if (index < 0 || index >= 3)
            return;

        value.readState(creatureStats.mDynamic[index]);
        ptrCreatureStats->setDynamic(index, value);
    };

    if (exchangeFullInfo)
    {
        for (int i = 0; i < 3; ++i)
            applyDynamicStat(i);
    }
    else
    {
        for (auto statsDynamicIndex : statsDynamicIndexChanges)
            applyDynamicStat(statsDynamicIndex);
    }

    if (ptrCreatureStats->isDead() || creatureStats.mDynamic[0].mCurrent > 0)
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
}

void DedicatedPlayer::restoreDynamicStats()
{
    if (!reference)
        return;

    MWMechanics::CreatureStats* ptrCreatureStats = &getPtr().getClass().getCreatureStats(getPtr());
    MWMechanics::DynamicStat<float> value;

    if (creatureStats.mDead)
    {
        value.readState(creatureStats.mDynamic[0]);
        ptrCreatureStats->setHealth(value);
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }

    if (ptrCreatureStats->isDead())
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
}

void DedicatedPlayer::setAnimFlags()
{
    if (!reference)
        return;

    using namespace MWMechanics;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    const bool wasRemoteJumping = wasJumping;

    // Until we figure out a better workaround for disabling player gravity,
    // simply cast Levitate over and over on a player that's supposed to be flying
    if (!isFlying && !hasTcl && !isLevitationPurged)
    {
        ptr.getClass().getCreatureStats(ptr).getActiveSpells().purgeEffect(ptr, ESM::MagicEffect::Levitate);
        isLevitationPurged = true;
    }
    else if ((isFlying || hasTcl) && !world->isFlying(ptr))
    {
        MWMechanics::CastSpell levitationCast(ptr, ptr);
        levitationCast.mHitPosition = ptr.getRefData().getPosition().asVec3();
        levitationCast.mAlwaysSucceed = true;
        levitationCast.cast(stringRefId("Levitate"));
        isLevitationPurged = false;
    }

    if (isJumping && !wasJumping)
    {
        wasJumping = true;
    }
    else if (wasJumping && !isJumping)
    {
        wasJumping = false;
    }

    MWMechanics::CreatureStats* ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);

    ptrCreatureStats->setDrawState(static_cast<MWMechanics::DrawState>(drawState));

    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Run, (movementFlags & CreatureStats::Flag_Run) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Sneak, (movementFlags & CreatureStats::Flag_Sneak) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceJump,
        isJumping || (movementFlags & CreatureStats::Flag_ForceJump) != 0);
    ptrCreatureStats->setMovementFlag(
        CreatureStats::Flag_ForceMoveJump, (movementFlags & CreatureStats::Flag_ForceMoveJump) != 0);
    applyRemoteJumpMovementCue(wasRemoteJumping);
}

void DedicatedPlayer::setAttributes()
{
    if (!reference)
        return;

    MWMechanics::CreatureStats* ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::AttributeValue attributeValue;

    for (int i = 0; i < 8; ++i)
    {
        attributeValue.readState(creatureStats.mAttributes[i]);
        ptrCreatureStats->setAttribute(ESM::Attribute::indexToRefId(i), attributeValue);
    }
}

void DedicatedPlayer::setSkills()
{
    if (!reference)
        return;

    // Go no further if the player is disguised as a creature
    if (ptr.get<ESM::NPC>() == nullptr)
        return;

    MWMechanics::NpcStats* ptrNpcStats = &ptr.getClass().getNpcStats(ptr);
    MWMechanics::SkillValue skillValue;

    for (int i = 0; i < 27; ++i)
    {
        skillValue.readState(npcStats.mSkills[i]);
        ptrNpcStats->setSkill(ESM::Skill::indexToRefId(i), skillValue);
    }
}

void DedicatedPlayer::setEquipment()
{
    if (!reference)
    {
        hasPendingEquipmentApplication = true;
        return;
    }

    // Go no further if the player is disguised as a creature
    if (!ptr.getClass().hasInventoryStore(ptr))
    {
        hasPendingEquipmentApplication = true;
        return;
    }

    hasPendingEquipmentApplication = false;

    bool equippedSomething = false;

    MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);
    const auto applySlot = [&](int slot) {
        if (slot < 0 || slot >= MWWorld::InventoryStore::Slots)
            return;

        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        const std::string& packetRefId = equipmentItems[slot].refId;
        ESM::RefId ptrItemId;
        bool equal = false;

        if (it != invStore.end())
        {
            ptrItemId = it->getCellRef().getRefId();

            if (ptrItemId != stringRefId(packetRefId)) // if other item is now equipped
            {
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);

                // Remove the items that are no longer equipped, except for throwing weapons and ranged weapon ammo that
                // have just run out but still need to be kept briefly so they can be used in attacks about to be
                // released
                bool shouldRemove = true;

                if (attack.type == mwmp::Attack::RANGED && packetRefId.empty() && !attack.pressed)
                {
                    if (slot == MWWorld::InventoryStore::Slot_CarriedRight
                        && ptrItemId == stringRefId(attack.rangedWeaponId))
                        shouldRemove = false;
                    else if (slot == MWWorld::InventoryStore::Slot_Ammunition
                        && ptrItemId == stringRefId(attack.rangedAmmoId))
                        shouldRemove = false;
                }

                if (shouldRemove)
                {
                    store.remove(ptrItemId, store.count(ptrItemId), false);
                }
            }
            else
                equal = true;
        }

        if (packetRefId.empty() || equal)
            return;

        const int count = equipmentItems[slot].count;
        ptr.getClass().getContainerStore(ptr).add(stringRefId(packetRefId), count, false);
        // Equip items silently if this is the first time equipment is being set for this character
        equipItem(packetRefId, !hasReceivedInitialEquipment);
        equippedSomething = true;
    };

    if (exchangeFullInfo)
    {
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            applySlot(slot);
    }
    else
    {
        for (const int slot : equipmentIndexChanges)
            applySlot(slot);
    }

    // Only track the initial equipment as received if at least one item has been equipped
    if (equippedSomething)
        hasReceivedInitialEquipment = true;
}

void DedicatedPlayer::setShapeshift()
{
    if (!reference)
        return;

    MWBase::World* world = MWBase::Environment::get().getWorld();

    bool isNpc = ptr.get<ESM::NPC>() != nullptr;

    if (creatureRefId != previousCreatureRefId || displayCreatureName != previousDisplayCreatureName)
    {
        if (!creatureRefId.empty() && RecordHelper::doesRecordIdExist<ESM::Creature>(creatureRefId))
        {
            deleteReference();

            const ESM::Creature* tmpCreature
                = world->getStore().get<ESM::Creature>().search(stringRefId(creatureRefId));
            creature = *tmpCreature;
            creature.mScript = ESM::RefId();
            if (!displayCreatureName)
                creature.mName = npc.mName;
            LOG_APPEND(TimedLog::LOG_INFO, "- %s is disguised as %s", npc.mName.c_str(), creatureRefId.c_str());

            // Is this our first time creating a creature record id for this player? If so, keep it around
            // and reuse it
            if (creatureRecordId.empty())
            {
                creature.mId = creatureRecordId = RecordHelper::createRecord(creature)->mId;
                LOG_APPEND(
                    TimedLog::LOG_INFO, "- Creating new creature record %s", creatureRecordId.serializeText().c_str());
            }
            else
            {
                creature.mId = creatureRecordId;
                RecordHelper::overrideRecord(creature);
            }

            LOG_APPEND(TimedLog::LOG_INFO, "- Creating reference for %s", creature.mId.serializeText().c_str());
            createReference(creature.mId);
        }
        // This player was already a creature, but the new creature refId was empty or
        // invalid, so we'll turn this player into their NPC self again as a result
        else if (!isNpc)
        {
            if (reference)
            {
                deleteReference();
            }

            RecordHelper::overrideRecord(npc);
            createReference(npc.mId);
            reloadPtr();
        }

        previousCreatureRefId = creatureRefId;
        previousDisplayCreatureName = displayCreatureName;
    }

    if (ptr.get<ESM::NPC>() != nullptr)
    {
        MWBase::Environment::get().getMechanicsManager()->setWerewolf(ptr, isWerewolf);

        if (!isWerewolf)
            setEquipment();
    }

    MWBase::Environment::get().getWorld()->scaleObject(ptr, scale);
}

void DedicatedPlayer::setCell()
{
    // Prevent cell update when reference doesn't exist
    if (!reference)
        return;

    MWBase::World* world = MWBase::Environment::get().getWorld();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Server says DedicatedPlayer %s moved to %s", npc.mName.c_str(),
        std::string(cell.getDescription()).c_str());

    MWWorld::CellStore* cellStore = Main::get().getCellController()->getCellStore(cell);

    if (!cellStore)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "%s", "- Cell doesn't exist on this client");
        world->disable(getPtr());
        return;
    }
    else
        world->enable(getPtr());

    // Make sure the Ptr's dynamic stats and anim flags are up-to-date, so it doesn't show up
    // knocked down or in a jump loop when it shouldn't
    setStatsDynamic();
    setAnimFlags();

    // Allow this player's reference to move across a cell now that a manual cell
    // update has been called
    setPtr(world->moveObject(ptr, cellStore, position.asVec3()));
    setMovementSettings();
    world->rotateObject(ptr, osg::Vec3f(position.rot[0], 0, position.rot[2]));
    hasChangedCell = true;
    resetRemoteMovementEstimate();
    resetRemoteTimingEstimate();
    mLastRemotePositionPacket = std::chrono::steady_clock::time_point();

    // Remove the marker entirely if this player has moved to an interior that is inactive for us
    if (!cell.isExterior() && !Main::get().getCellController()->isActiveWorldCell(cell))
        removeMarker();
    // Otherwise, update their marker so the player shows up in the right cell on the world map
    else
    {
        enableMarker();
    }

    // If this player is now in a cell that we are the local authority over, we should send them all
    // NPC data in that cell
    if (Main::get().getCellController()->hasLocalAuthority(cell))
    {
        if (Cell* mpCell = Main::get().getCellController()->getCell(cell))
            mpCell->updateLocal(true);
    }

    hasFinishedInitialTeleportation = true;
}

void DedicatedPlayer::playAnimation()
{
    if (!reference)
        return;

    MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(
        getPtr(), animation.groupname, animation.mode, animation.count, animation.persist);
}

void DedicatedPlayer::playSpeech()
{
    if (!reference)
        return;

    MWBase::Environment::get().getSoundManager()->say(getPtr(), VFS::Path::Normalized(sound));
}

void DedicatedPlayer::equipItem(std::string itemId, bool noSound)
{
    if (!reference || !ptr.getClass().hasInventoryStore(ptr))
        return;

    for (const auto& itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (itemPtr.getCellRef().getRefId() == stringRefId(itemId))
        {
            std::shared_ptr<MWWorld::Action> action = itemPtr.getClass().use(itemPtr);
            action->execute(ptr, noSound);
            break;
        }
    }
}

void DedicatedPlayer::die()
{
    if (!reference)
        return;

    MWMechanics::DynamicStat<float> health;
    creatureStats.mDead = true;
    health.readState(creatureStats.mDynamic[0]);
    health.setCurrent(0);
    health.writeState(creatureStats.mDynamic[0]);

    ptr.getClass().getCreatureStats(ptr).setHealth(health);
}

void DedicatedPlayer::resurrect()
{
    if (!reference)
        return;

    creatureStats.mDead = false;
    if (creatureStats.mDynamic[0].mMod < 1)
        creatureStats.mDynamic[0].mMod = 1;
    creatureStats.mDynamic[0].mCurrent = creatureStats.mDynamic[0].mMod;

    MWBase::Environment::get().getMechanicsManager()->resurrect(getPtr());

    MWMechanics::DynamicStat<float> health;
    health.readState(creatureStats.mDynamic[0]);
    getPtr().getClass().getCreatureStats(getPtr()).setHealth(health);
}

void DedicatedPlayer::addSpellsActive()
{
    if (!reference)
        return;

    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MechanicsHelper::createSpellGfx(getPtr(), activeSpell.params.mEffects);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        MWWorld::Ptr caster = MechanicsHelper::getPlayerPtr(activeSpell.caster);
        if (caster.isEmpty())
            caster = getPtr();
        const ESM::RefId sourceSpellId = activeSpellSourceId(activeSpell);
        MWMechanics::ActiveSpells::ActiveSpellParams params(
            caster, sourceSpellId, activeSpell.params.mDisplayName, ESM::RefNum());
        params.setActiveSpellId(activeSpellActiveId(activeSpell));
        params.getEffects() = activeSpell.params.mEffects;
        params.setFlag(ESM::ActiveSpells::Flag_Temporary);
        if (activeSpell.isStackingSpell)
            params.setFlag(ESM::ActiveSpells::Flag_Stackable);
        activeSpells.addSpell(params);
    }
}

void DedicatedPlayer::removeSpellsActive()
{
    if (!reference)
        return;

    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            activeSpells.removeEffectsByActiveSpellId(getPtr(), activeSpellActiveId(activeSpell));
        }
        else
        {
            activeSpells.removeEffectsBySourceSpellId(getPtr(), activeSpellSourceId(activeSpell));
        }
    }
}

void DedicatedPlayer::setSpellsActive()
{
    if (!reference)
        return;

    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();
    activeSpells.clear(getPtr());

    // Proceed by adding spells active
    addSpellsActive();
}

void DedicatedPlayer::applySpellsActiveChanges()
{
    if (!reference)
    {
        hasPendingSpellsActiveChanges = true;
        return;
    }

    hasPendingSpellsActiveChanges = false;

    int spellsActiveAction = spellsActiveChanges.action;

    if (spellsActiveAction == SpellsActiveChanges::ADD)
        addSpellsActive();
    else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
        removeSpellsActive();
    else
        setSpellsActive();
}

void DedicatedPlayer::updateMarker()
{
    if (!markerEnabled)
    {
        return;
    }

    GUIController* gui = Main::get().getGUIController();

    if (gui->mPlayerMarkers.contains(marker))
    {
        gui->mPlayerMarkers.deleteMarker(marker);
        marker = gui->createMarker(guid);
        gui->mPlayerMarkers.addMarker(marker);
    }
    else
    {
        gui->mPlayerMarkers.addMarker(marker, true);
    }
}

void DedicatedPlayer::enableMarker()
{
    markerEnabled = true;
    updateMarker();
}

void DedicatedPlayer::removeMarker()
{
    if (!markerEnabled)
        return;

    markerEnabled = false;
    GUIController* gui = Main::get().getGUIController();

    if (gui->mPlayerMarkers.contains(marker))
    {
        Main::get().getGUIController()->mPlayerMarkers.deleteMarker(marker);
    }
}

void DedicatedPlayer::createReference(const ESM::RefId& recId)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();

    reference = new MWWorld::ManualRef(world->getStore(), recId, 1);

    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new reference pointer for %s", npc.mName.c_str());

    ptr = world->placeObject(reference->getPtr(), Main::get().getCellController()->getCellStore(cell), position);
    hasChangedCell = true;

    ESM::CustomMarker mEditingMarker = Main::get().getGUIController()->createMarker(guid);
    marker = mEditingMarker;
    enableMarker();
}

void DedicatedPlayer::deleteReference()
{
    removeMarker();

    if (!reference)
        return;

    LOG_APPEND(TimedLog::LOG_INFO, "- Deleting reference");
    if (!ptr.isEmpty())
        MWBase::Environment::get().getWorld()->deleteObject(ptr);

    delete reference;
    reference = nullptr;
    ptr = MWWorld::Ptr();
    hasReceivedInitialPosition = false;
    hasFinishedInitialTeleportation = false;
    resetRemoteMovementEstimate();
    resetRemoteTimingEstimate();
}

MWWorld::Ptr DedicatedPlayer::getPtr()
{
    return ptr;
}

MWWorld::ManualRef* DedicatedPlayer::getRef()
{
    return reference;
}

bool DedicatedPlayer::hasReference() const
{
    return reference != nullptr;
}

void DedicatedPlayer::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;
}

void DedicatedPlayer::reloadPtr()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->disable(ptr);
    world->enable(ptr);
}
