#include <components/openmw-mp/TimedLog.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwmechanics/aiactivate.hpp"
#include "../mwmechanics/aicombat.hpp"
#include "../mwmechanics/aiescort.hpp"
#include "../mwmechanics/aifollow.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aiwander.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "DedicatedActor.hpp"
#include "Main.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"

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

    std::string refIdToString(const ESM::RefId& id)
    {
        return id.serializeText();
    }
}

DedicatedActor::DedicatedActor()
{
    drawState = static_cast<char>(MWMechanics::DrawState::Nothing);
    movementFlags = 0;
    animation.groupname = "";
    sound = "";

    hasPositionData = false;
    hasStatsDynamicData = false;
    hasReceivedInitialEquipment = false;
    hasChangedCell = true;
    wasJumping = false;
    mRemoteVelocity = osg::Vec3f();
    mSmoothedRemoteSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(movementSampleIntervalSeconds);
    mSmoothedRemoteLatencySeconds = sanitizeMovementLatencySeconds(movementLatencySeconds);
    mRemoteJitterSeconds = 0.f;
    mRemotePacketAgeSeconds = 0.f;
    mHasRemoteVelocity = false;
    mHasRemoteTimingEstimate = false;

    attack.pressed = false;
    cast.pressed = false;
}

DedicatedActor::~DedicatedActor()
{

}

void DedicatedActor::update(float dt)
{
    if (hasPositionData)
    {
        move(dt);
    }

    if (hasAnimFlagsData)
        setAnimFlags();

    setStatsDynamic();
}

void DedicatedActor::setCell(MWWorld::CellStore *cellStore)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();

    ptr = world->moveObject(ptr, cellStore, position.asVec3());
    setMovementSettings();
    world->rotateObject(ptr, position.asRotationVec3());

    hasChangedCell = true;
    resetRemoteMovementEstimate();
    resetRemoteTimingEstimate();
    mLastRemotePositionPacket = std::chrono::steady_clock::time_point();
}

void DedicatedActor::move(float dt)
{
    const ESM::Position previousVisualPosition = ptr.getRefData().getPosition();
    ESM::Position refPos = previousVisualPosition;
    MWBase::World *world = MWBase::Environment::get().getWorld();
    constexpr float maxInterpolationDistance = 512.f;
    if (std::isfinite(dt) && dt > 0.f)
        mRemotePacketAgeSeconds = std::min(mRemotePacketAgeSeconds + dt, 0.15f);

    // Apply interpolation only if the position hasn't changed too much from last time
    bool shouldInterpolate = std::abs(position.pos[0] - refPos.pos[0]) < maxInterpolationDistance &&
        std::abs(position.pos[1] - refPos.pos[1]) < maxInterpolationDistance &&
        std::abs(position.pos[2] - refPos.pos[2]) < maxInterpolationDistance;

    // Don't apply linear interpolation if the DedicatedActor has just gone through a cell change, because
    // the interpolated position will be invalid, causing a slight hopping glitch
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
        refPos.pos[0] = lerp.x();
        refPos.pos[1] = lerp.y();
        refPos.pos[2] = lerp.z();

        world->moveObject(ptr, refPos.asVec3());
        world->rotateObject(ptr, rotation);
        setMovementSettingsFromVisualDelta(previousVisualPosition);
    }
    else
    {
        setPosition();
        hasChangedCell = false;
    }
}

void DedicatedActor::setMovementSettings()
{
    setMovementSettings(direction);
}

void DedicatedActor::setMovementSettings(const ESM::Position& movementDirection)
{
    MWMechanics::Movement *move = &ptr.getClass().getMovementSettings(ptr);

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

void DedicatedActor::setMovementSettingsFromVisualDelta(const ESM::Position& previousPosition)
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

void DedicatedActor::applyRemoteJumpMovementCue(bool wasRemoteJumping)
{
    using namespace MWMechanics;

    Movement* move = &ptr.getClass().getMovementSettings(ptr);
    const bool wantsJump = isJumping || (movementFlags & CreatureStats::Flag_ForceJump) != 0;

    if (wantsJump)
    {
        if (!std::isfinite(move->mPosition[2]) || move->mPosition[2] <= 0.f)
            move->mPosition[2] = 1.f;
    }
    else if (wasRemoteJumping && !isFlying && std::isfinite(move->mPosition[2]) && move->mPosition[2] > 0.f)
    {
        move->mPosition[2] = 0.f;
    }
}

void DedicatedActor::updateRemoteMovementEstimate(const ESM::Position& previousPosition, bool hadPositionData)
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
    const float velocityDeltaSeconds = std::isfinite(deltaSeconds)
        ? std::clamp(deltaSeconds, sampleIntervalSeconds * 0.75f, sampleIntervalSeconds * 3.f)
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

void DedicatedActor::resetRemoteMovementEstimate()
{
    mRemoteVelocity = osg::Vec3f();
    mRemotePacketAgeSeconds = 0.f;
    mHasRemoteVelocity = false;
}

void DedicatedActor::updateRemoteTimingEstimate(float arrivalDeltaSeconds, bool hasPreviousPacket)
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

void DedicatedActor::resetRemoteTimingEstimate()
{
    mSmoothedRemoteSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(movementSampleIntervalSeconds);
    mSmoothedRemoteLatencySeconds = sanitizeMovementLatencySeconds(movementLatencySeconds);
    mRemoteJitterSeconds = 0.f;
    mHasRemoteTimingEstimate = false;
}

void DedicatedActor::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    world->moveObject(ptr, position.asVec3());
    setMovementSettings();
    world->rotateObject(ptr, position.asRotationVec3());
}

void DedicatedActor::setAnimFlags()
{
    using namespace MWMechanics;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    const bool wasRemoteJumping = wasJumping;
    wasJumping = isJumping;

    ptrCreatureStats->setDrawState(static_cast<MWMechanics::DrawState>(drawState));

    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Run, (movementFlags & CreatureStats::Flag_Run) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Sneak, (movementFlags & CreatureStats::Flag_Sneak) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceJump,
        isJumping || (movementFlags & CreatureStats::Flag_ForceJump) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceMoveJump, (movementFlags & CreatureStats::Flag_ForceMoveJump) != 0);
    applyRemoteJumpMovementCue(wasRemoteJumping);
}

void DedicatedActor::setStatsDynamic()
{
    // Only set dynamic stats if we have received at least one packet about them
    if (!hasStatsDynamicData) return;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> value;

    // Resurrect this Actor if it's not supposed to be dead according to its authority
    if (!creatureStats.mDead && creatureStats.mDynamic[0].mCurrent > 0)
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
    else if (creatureStats.mDead)
        creatureStats.mDynamic[0].mCurrent = 0;

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }

    ptrCreatureStats->setDeathAnimationFinished(creatureStats.mDeathAnimationFinished);
}

void DedicatedActor::restoreDynamicStats()
{
    setStatsDynamic();
}

void DedicatedActor::setEquipment()
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
    {
        int count = equipmentItems[slot].count;

        // If we've somehow received a corrupted item with a count lower than 0, ignore it
        if (count < 0) continue;

        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        const std::string &packetRefId = equipmentItems[slot].refId;
        int packetCharge = equipmentItems[slot].charge;
        ESM::RefId storeRefId;
        bool equal = false;

        if (it != invStore.end())
        {
            storeRefId = it->getCellRef().getRefId();

            if (storeRefId != stringRefId(packetRefId)) // if other item equiped
                invStore.unequipSlot(slot);
            else
                equal = true;
        }

        if (packetRefId.empty() || equal)
            continue;

        if (!hasItem(packetRefId, packetCharge))
        {
            ptr.getClass().getContainerStore(ptr).add(stringRefId(packetRefId), count, false);
        }

        // Equip items silently if this is the first time equipment is being set for this character
        equipItem(packetRefId, packetCharge, !hasReceivedInitialEquipment);
    }

    hasReceivedInitialEquipment = true;
}

void DedicatedActor::setAi()
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Fight, 0);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- actor cellRef: %s %i-%i",
        ptr.getCellRef().getRefId().serializeText().c_str(), ptr.getCellRef().getRefNum().mIndex, 0);

    if (aiAction == mwmp::BaseActorList::CANCEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Cancelling AI sequence");

        ptrCreatureStats->getAiSequence().clear();
    }
    else if (aiAction == mwmp::BaseActorList::TRAVEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Travelling to %f, %f, %f",
            aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

        MWMechanics::AiTravel package(aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2], false);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (aiAction == mwmp::BaseActorList::WANDER)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Wandering for distance %i and duration %i, repetition is %s",
            aiDistance, aiDuration, aiShouldRepeat ? "true" : "false");

        std::vector<unsigned char> idleList;

        MWMechanics::AiWander package(aiDistance, aiDuration, -1, idleList, aiShouldRepeat);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (hasAiTarget)
    {
        MWWorld::Ptr targetPtr;

        if (aiTarget.isPlayer)
        {
            targetPtr = MechanicsHelper::getPlayerPtr(aiTarget);

            LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has player target %s",
                std::string(targetPtr.getClass().getName(targetPtr)).c_str());
        }
        else
        {
            mwmp::CellController* cellController = mwmp::Main::get().getCellController();
            if (cellController->isLocalActor(aiTarget.refNum, aiTarget.mpNum))
            {
                if (mwmp::LocalActor* localActor = cellController->getLocalActor(aiTarget.refNum, aiTarget.mpNum))
                    targetPtr = localActor->getPtr();
            }
            else if (cellController->isDedicatedActor(aiTarget.refNum, aiTarget.mpNum))
            {
                if (mwmp::DedicatedActor* dedicatedActor = cellController->getDedicatedActor(aiTarget.refNum, aiTarget.mpNum))
                    targetPtr = dedicatedActor->getPtr();
            }

            if (!targetPtr.isEmpty())
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has actor target %s %i-%i",
                    targetPtr.getCellRef().getRefId().serializeText().c_str(), aiTarget.refNum, aiTarget.mpNum);
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has invalid actor target %i-%i",
                    aiTarget.refNum, aiTarget.mpNum);
            }

        }

        if (!targetPtr.isEmpty())
        {
            if (aiAction == mwmp::BaseActorList::ACTIVATE)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Activating target");

                MWMechanics::AiActivate package(targetPtr.getCellRef().getRefId(), false);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }

            if (aiAction == mwmp::BaseActorList::COMBAT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Starting combat with target");

                MWMechanics::AiCombat package(targetPtr);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::ESCORT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Being escorted by target, for duration %i, to coordinates %f, %f, %f",
                    aiDuration, aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

                MWMechanics::AiEscort package(targetPtr.getCellRef().getRefNum(),
                    targetPtr.getCell()->getCell()->getNameId(), aiDuration,
                    aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2], false);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::FOLLOW)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Following target");

                MWMechanics::AiFollow package(targetPtr);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
        }
    }
}

void DedicatedActor::playAnimation()
{
    if (!animation.groupname.empty())
    {
        MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(ptr,
            animation.groupname, animation.mode, animation.count, animation.persist);

        animation.groupname.clear();
    }
}

void DedicatedActor::playSound()
{
    if (!sound.empty())
    {
        MWBase::Environment::get().getSoundManager()->say(ptr, VFS::Path::Normalized(sound));

        sound.clear();
    }
}

bool DedicatedActor::hasItem(std::string itemId, int charge)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (itemPtr.getCellRef().getRefId() == stringRefId(itemId) && itemPtr.getCellRef().getCharge() == charge)
            return true;
    }

    return false;
}

void DedicatedActor::equipItem(std::string itemId, int charge, bool noSound)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (itemPtr.getCellRef().getRefId() == stringRefId(itemId) && itemPtr.getCellRef().getCharge() == charge)
        {
            std::shared_ptr<MWWorld::Action> action = itemPtr.getClass().use(itemPtr);
            action->execute(ptr, noSound);
            break;
        }
    }
}

void DedicatedActor::addSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MechanicsHelper::createSpellGfx(getPtr(), activeSpell.params.mEffects);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        MWWorld::Ptr caster = MechanicsHelper::getPlayerPtr(activeSpell.caster);
        if (caster.isEmpty())
            caster = getPtr();
        MWMechanics::ActiveSpells::ActiveSpellParams params(
            caster, stringRefId(activeSpell.id), activeSpell.params.mDisplayName, ESM::RefNum());
        params.setActiveSpellId(activeSpell.params.mActiveSpellId);
        params.getEffects() = activeSpell.params.mEffects;
        params.setFlag(ESM::ActiveSpells::Flag_Temporary);
        if (activeSpell.isStackingSpell)
            params.setFlag(ESM::ActiveSpells::Flag_Stackable);
        activeSpells.addSpell(params);
    }
}

void DedicatedActor::removeSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            activeSpells.removeEffectsByActiveSpellId(getPtr(), stringRefId(activeSpell.id));
        }
        else
        {
            activeSpells.removeEffectsBySourceSpellId(getPtr(), stringRefId(activeSpell.id));
        }
    }
}

void DedicatedActor::setSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();
    activeSpells.clear(getPtr());

    // Proceed by adding spells active
    addSpellsActive();
}

MWWorld::Ptr DedicatedActor::getPtr()
{
    return ptr;
}

void DedicatedActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = refIdToString(ptr.getCellRef().getRefId());
    const auto [networkRefNum, networkMpNum] = Main::get().getCellController()->getActorNetworkId(ptr);
    refNum = networkRefNum;
    mpNum = networkMpNum;

    position = ptr.getRefData().getPosition();
    drawState = static_cast<char>(ptr.getClass().getCreatureStats(ptr).getDrawState());
}

void DedicatedActor::reloadPtr()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->disable(ptr);
    world->enable(ptr);
}

