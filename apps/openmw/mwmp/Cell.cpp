#include <cstddef>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include <components/esm3/cellid.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/livecellref.hpp"
#include "../mwworld/worldimp.hpp"

#include "Cell.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "CellController.hpp"
#include "CellIdentity.hpp"
#include "MechanicsHelper.hpp"

#ifdef DrawState
#undef DrawState
#endif

using namespace mwmp;

namespace
{
    ESM::RefId stringRefId(const std::string& id)
    {
        if (id.empty())
            return {};

        return ESM::RefId::stringRefId(id);
    }

    std::string cellDescription(const ESM::Cell& cell)
    {
        return getCanonicalCellDescription(cell);
    }

    bool isDeleted(const MWWorld::Ptr& ptr)
    {
        return ptr.getCellRef().getCount(false) == 0;
    }

    bool isFinitePosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2])
            && std::isfinite(position.rot[0]) && std::isfinite(position.rot[1]) && std::isfinite(position.rot[2]);
    }

    std::vector<BaseActor> coalesceNewestPositionActors(const std::vector<BaseActor>& baseActors)
    {
        std::vector<BaseActor> coalescedActors;
        coalescedActors.reserve(baseActors.size());

        std::map<std::string, std::size_t> actorIndexes;
        CellController* cellController = Main::get().getCellController();

        for (const BaseActor& baseActor : baseActors)
        {
            const std::string mapIndex = cellController->generateMapIndex(baseActor);
            auto found = actorIndexes.find(mapIndex);
            if (found == actorIndexes.end())
            {
                actorIndexes.emplace(mapIndex, coalescedActors.size());
                coalescedActors.push_back(baseActor);
                continue;
            }

            BaseActor& acceptedActor = coalescedActors[found->second];
            if (isNewerPositionSequence(baseActor.positionSequence, acceptedActor.positionSequence))
                acceptedActor = baseActor;
        }

        return coalescedActors;
    }

    std::vector<BaseActor> coalesceNewestAnimFlagsActors(const std::vector<BaseActor>& baseActors)
    {
        std::vector<BaseActor> coalescedActors;
        coalescedActors.reserve(baseActors.size());

        std::map<std::string, std::size_t> actorIndexes;
        CellController* cellController = Main::get().getCellController();

        for (const BaseActor& baseActor : baseActors)
        {
            const std::string mapIndex = cellController->generateMapIndex(baseActor);
            auto found = actorIndexes.find(mapIndex);
            if (found == actorIndexes.end())
            {
                actorIndexes.emplace(mapIndex, coalescedActors.size());
                coalescedActors.push_back(baseActor);
                continue;
            }

            BaseActor& acceptedActor = coalescedActors[found->second];
            mergeNewestActorAnimFlags(acceptedActor, baseActor);
        }

        return coalescedActors;
    }

    void acceptServerActorAuthorityIfNeeded(mwmp::Cell& cell, const mwmp::ActorList& actorList)
    {
        if (!mwmp::isPacketGuidAssigned(actorList.guid))
            cell.setServerActorAuthority(true);
    }

    bool applySequencedPosition(DedicatedActor& actor, const BaseActor& baseActor)
    {
        if (!baseActor.hasPositionData)
            return true;

        if (!isFinitePosition(baseActor.position) || !isFinitePosition(baseActor.direction))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring actor position packet with invalid coordinates");
            return false;
        }

        if (actor.hasPositionData && !isNewerPositionSequence(baseActor.positionSequence, actor.positionSequence))
            return false;

        const bool hadPositionData = actor.hasPositionData;
        const ESM::Position previousPosition = actor.position;

        actor.position = baseActor.position;
        actor.direction = baseActor.direction;
        actor.movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(baseActor.movementSampleIntervalSeconds);
        actor.movementLatencySeconds = sanitizeMovementLatencySeconds(baseActor.movementLatencySeconds);
        actor.positionSequence = baseActor.positionSequence;
        actor.hasPositionData = true;

        if (hadPositionData)
            MechanicsHelper::deriveMissingMovementDirection(actor.direction, actor.position, previousPosition);
        actor.updateRemoteMovementEstimate(previousPosition, hadPositionData);

        if (!hadPositionData)
            actor.setPosition();

        return true;
    }

    bool normalizeSequencedPositionForCombat(DedicatedActor& actor, const BaseActor& baseActor)
    {
        if (!baseActor.hasPositionData)
            return false;

        if (!isFinitePosition(baseActor.position) || !isFinitePosition(baseActor.direction))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring actor combat packet with invalid movement snapshot");
            return false;
        }

        if (actor.hasPositionData && !isNewerPositionSequence(baseActor.positionSequence, actor.positionSequence))
            return true;

        return applySequencedPosition(actor, baseActor);
    }

    bool normalizeSequencedPositionForAi(DedicatedActor& actor, const BaseActor& baseActor)
    {
        if (!baseActor.hasPositionData)
            return true;

        if (!isFinitePosition(baseActor.position) || !isFinitePosition(baseActor.direction))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Ignoring actor AI packet with invalid movement snapshot");
            return false;
        }

        if (actor.hasPositionData && !isNewerPositionSequence(baseActor.positionSequence, actor.positionSequence))
            return true;

        return applySequencedPosition(actor, baseActor);
    }

    bool isActorCombatReplaySequenceAllowed(const DedicatedActor& actor, const BaseActor& baseActor)
    {
        return isActorCombatSequenceAllowed(actor, baseActor);
    }

    void acceptActorCombatReplaySequence(DedicatedActor& actor, const BaseActor& baseActor)
    {
        acceptActorCombatSequence(actor, baseActor);
    }

    MWWorld::Ptr searchExact(MWWorld::CellStore* cellStore, const BaseActor& baseActor)
    {
        MWWorld::Ptr found;
        const ESM::RefId refId = stringRefId(baseActor.refId);
        mwmp::ObjectList* objectList = nullptr;

        if (mwmp::Main::isInitialized())
            objectList = mwmp::Main::get().getNetworking()->getObjectList();

        if (baseActor.mpNum != 0 && objectList != nullptr)
        {
            const std::optional<unsigned int> localRefNum = objectList->getLocalRefNumForServerMpNum(baseActor.mpNum);
            if (localRefNum.has_value())
            {
                cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                    if (ptr.getCellRef().getRefNum().mIndex == *localRefNum
                        && (refId.empty() || ptr.getCellRef().getRefId() == refId))
                    {
                        found = ptr;
                        return false;
                    }

                    return true;
                }, true);

                if (!found.isEmpty())
                    return found;
            }
        }

        if (baseActor.refNum == 0)
            return MWWorld::Ptr();

        cellStore->forEach([&](const MWWorld::Ptr& ptr) {
            if (ptr.getCellRef().getRefNum().mIndex == baseActor.refNum
                && (refId.empty() || ptr.getCellRef().getRefId() == refId))
            {
                found = ptr;
                if (baseActor.mpNum != 0 && objectList != nullptr)
                    objectList->registerServerObjectId(ptr, baseActor.mpNum);
                return false;
            }

            return true;
        }, true);

        return found;
    }

    std::string getLegacyActorIndexForServerMpNum(unsigned int mpNum)
    {
        if (mpNum == 0 || !mwmp::Main::isInitialized())
            return "";

        const std::optional<unsigned int> localRefNum
            = mwmp::Main::get().getNetworking()->getObjectList()->getLocalRefNumForServerMpNum(mpNum);
        if (!localRefNum.has_value())
            return "";

        return mwmp::Main::get().getCellController()->generateMapIndex(*localRefNum, 0);
    }

    template <typename ActorMap>
    bool migrateActorMapIndex(
        ActorMap& actorMap, const std::string& oldIndex, const std::string& newIndex, bool localActor, const std::string& cellIndex)
    {
        if (oldIndex.empty() || oldIndex == newIndex || actorMap.count(oldIndex) == 0 || actorMap.count(newIndex) > 0)
            return false;

        auto node = actorMap.extract(oldIndex);
        node.key() = newIndex;

        auto* actor = node.mapped();
        const auto dash = newIndex.find('-');
        if (dash != std::string::npos)
        {
            actor->refNum = static_cast<unsigned int>(std::stoul(newIndex.substr(0, dash)));
            actor->mpNum = static_cast<unsigned int>(std::stoul(newIndex.substr(dash + 1)));
        }

        actorMap.insert(std::move(node));

        auto* cellController = mwmp::Main::get().getCellController();
        if (localActor)
        {
            cellController->removeLocalActorRecord(oldIndex);
            cellController->setLocalActorRecord(newIndex, cellIndex);
        }
        else
        {
            cellController->removeDedicatedActorRecord(oldIndex);
            cellController->setDedicatedActorRecord(newIndex, cellIndex);
        }

        return true;
    }
}

mwmp::Cell::Cell(MWWorld::CellStore* cellStore)
{
    store = cellStore;
    shouldInitializeActors = false;
    authorityGuid = mwmp::unassignedPacketGuid();
    serverActorAuthority = false;

    updateTimer = 0;
}

Cell::~Cell()
{

}

void Cell::updateLocal(bool forceUpdate)
{
    if (localActors.empty())
        return;

    constexpr float timeoutSec = 1.f / 60.f;

    if (!forceUpdate)
    {
        updateTimer += MWBase::Environment::get().getFrameDuration();
        if (updateTimer < timeoutSec)
            return;

        updateTimer = std::fmod(updateTimer, timeoutSec);
    }
    else
        updateTimer = 0;

    CellController *cellController = Main::get().getCellController();
    ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
    actorList->reset();

    actorList->cell = makeActorPacketCell(*store->getCell());

    for (auto it = localActors.begin(); it != localActors.end();)
    {
        LocalActor *actor = it->second;

        MWWorld::CellStore *newStore = actor->getPtr().getCell();

        if (newStore != store)
        {
            actor->updateCell();
            std::string mapIndex = it->first;

            // If the cell this actor has moved to is under our authority, move them to it
            if (cellController->hasLocalAuthority(actor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving LocalActor %s to our authority in %s",
                    mapIndex.c_str(), cellDescription(actor->cell).c_str());
                Cell *newCell = cellController->getCell(actor->cell);
                if (newCell != nullptr)
                {
                    newCell->localActors[mapIndex] = actor;
                    cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
                }
                else
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Destination authority cell is no longer initialized");
                    cellController->removeLocalActorRecord(mapIndex);
                    delete actor;
                }
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting LocalActor %s which is no longer under our authority",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeLocalActorRecord(mapIndex);
                delete actor;
            }

            localActors.erase(it++);
        }
        else
        {
            if (actor->getPtr().getRefData().isEnabled())
            {
                if (isDeleted(actor->getPtr()))
                {
                    std::string mapIndex = it->first;
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting LocalActor %s whose reference has been deleted",
                        mapIndex.c_str(), getShortDescription().c_str());
                    cellController->removeLocalActorRecord(mapIndex);
                    delete actor;
                    localActors.erase(it++);
                }
                else
                {
                    // Forcibly update this local actor if its data has never been sent before;
                    // otherwise, use the current forceUpdate value
                    actor->update(actor->hasSentData ? forceUpdate : true);
                }
            }

            ++it;
        }
    }

    actorList->sendPositionActors();
    actorList->sendAnimFlagsActors();
    actorList->sendAnimPlayActors();
    actorList->sendSpeechActors();
    actorList->sendAttackActors();
    actorList->sendCastActors();
    actorList->sendStatsDynamicActors();
    actorList->sendDeathActors();
    actorList->sendEquipmentActors();
    actorList->sendAiActors();
    actorList->sendCellChangeActors();
}

void Cell::updateDedicated(float dt)
{
    if (dedicatedActors.empty()) return;
    
    for (auto &actor : dedicatedActors)
        actor.second->update(dt);

    // Are we the authority over this cell? If so, uninitialize DedicatedActors
    // after the above update
    if (hasLocalAuthority())
        uninitializeDedicatedActors();
}

void Cell::readPositions(ActorList& actorList)
{
    acceptServerActorAuthorityIfNeeded(*this, actorList);

    if (hasLocalAuthority())
        return;

    ActorList latestActorList = actorList;
    latestActorList.baseActors = coalesceNewestPositionActors(actorList.baseActors);
    latestActorList.count = static_cast<unsigned int>(latestActorList.baseActors.size());

    initializeDedicatedActors(latestActorList);

    if (dedicatedActors.empty()) return;
    
    for (const auto &baseActor : latestActorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            applySequencedPosition(*actor, baseActor);
        }
    }
}

void Cell::readAnimFlags(ActorList& actorList)
{
    ActorList latestActorList = actorList;
    latestActorList.baseActors = coalesceNewestAnimFlagsActors(actorList.baseActors);
    latestActorList.count = static_cast<unsigned int>(latestActorList.baseActors.size());

    initializeDedicatedActors(latestActorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : latestActorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            applySequencedPosition(*actor, baseActor);

            if (actor->hasAnimFlagsData
                && !isNewerActorAnimFlagsSequence(baseActor.animFlagsSequence, actor->animFlagsSequence))
                continue;

            actor->animFlagsSequence = baseActor.animFlagsSequence;
            actor->hasAnimFlagsData = true;
            actor->movementFlags = baseActor.movementFlags;
            actor->drawState = baseActor.drawState;
            actor->isJumping = baseActor.isJumping;
            actor->isFlying = baseActor.isFlying;
            actor->setAnimFlags();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(latestActorList);
}

void Cell::readAnimPlay(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            if (!isActorCombatReplaySequenceAllowed(*actor, baseActor))
                continue;

            if (!applySequencedPosition(*actor, baseActor))
                continue;

            acceptActorCombatReplaySequence(*actor, baseActor);
            actor->animation.groupname = baseActor.animation.groupname;
            actor->animation.mode = baseActor.animation.mode;
            actor->animation.count = baseActor.animation.count;
            actor->animation.persist = baseActor.animation.persist;
            actor->playAnimation();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readStatsDynamic(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            if (actor->hasStatsDynamicData
                && !isNewerActorStatsDynamicSequence(baseActor.statsDynamicSequence, actor->statsDynamicSequence))
                continue;

            actor->creatureStats = baseActor.creatureStats;
            actor->statsDynamicSequence = baseActor.statsDynamicSequence;
            actor->hasStatsDynamicData = true;
            actor->setStatsDynamic();
        }
    }
}

void Cell::readDeath(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            applySequencedPosition(*actor, baseActor);
            actor->creatureStats.mDead = true;
            actor->creatureStats.mDynamic[0].mCurrent = 0;

            Main::get().getCellController()->setQueuedDeathState(actor->getPtr(), baseActor.deathState);

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_ACTOR_DEATH about %s %i-%i in cell %s\n- deathState: %d\n-isInstantDeath: %s",
                actor->refId.c_str(), actor->refNum, actor->mpNum, getShortDescription().c_str(),
                baseActor.deathState, baseActor.isInstantDeath ? "true" : "false");

            if (baseActor.isInstantDeath)
            {
                actor->getPtr().getClass().getCreatureStats(actor->getPtr()).setDeathAnimationFinished(true);
                MWBase::Environment::get().getWorld()->enableActorCollision(actor->getPtr(), false);
            }
        }
    }
}

void Cell::readEquipment(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];

            if (actor->hasEquipmentData
                && !isNewerActorEquipmentSequence(baseActor.equipmentSequence, actor->equipmentSequence))
                continue;

            actor->hasEquipmentData = true;
            actor->equipmentSequence = baseActor.equipmentSequence;

            for (int slot = 0; slot < equipmentSlotCount; ++slot)
                actor->equipmentItems[slot] = baseActor.equipmentItems[slot];

            actor->setEquipment();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpeech(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->sound = baseActor.sound;
            actor->playSound();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpellsActive(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto& baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor* actor = dedicatedActors[mapIndex];
            actor->spellsActiveChanges = baseActor.spellsActiveChanges;

            int spellsActiveAction = baseActor.spellsActiveChanges.action;

            if (spellsActiveAction == SpellsActiveChanges::ADD)
                actor->addSpellsActive();
            else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
                actor->removeSpellsActive();
            else
                actor->setSpellsActive();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAi(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            if (!normalizeSequencedPositionForAi(*actor, baseActor))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Ignoring ActorAI about %s because its movement snapshot was invalid",
                    mapIndex.c_str());
                continue;
            }

            actor->aiAction = baseActor.aiAction;
            actor->aiDistance = baseActor.aiDistance;
            actor->aiDuration = baseActor.aiDuration;
            actor->aiShouldRepeat = baseActor.aiShouldRepeat;
            actor->aiCoordinates = baseActor.aiCoordinates;
            actor->hasAiTarget = baseActor.hasAiTarget;
            actor->aiTarget = baseActor.aiTarget;
            actor->setAi();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAttack(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Reading ActorAttack about %s", mapIndex.c_str());

            DedicatedActor *actor = dedicatedActors[mapIndex];
            if (!isActorCombatReplaySequenceAllowed(*actor, baseActor))
                continue;

            if (!normalizeSequencedPositionForCombat(*actor, baseActor))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Ignoring ActorAttack about %s because its movement snapshot was missing or invalid",
                    mapIndex.c_str());
                continue;
            }

            acceptActorCombatReplaySequence(*actor, baseActor);
            actor->attack = baseActor.attack;

            // Set the correct drawState here if we've somehow missed a previous
            // AnimFlags packet. Server-owned attacks can otherwise replay into an
            // idle upper body while authoritative damage still lands.
            if ((actor->attack.type == mwmp::Attack::MELEE || actor->attack.type == mwmp::Attack::RANGED)
                && actor->drawState != static_cast<char>(MWMechanics::DrawState::Weapon))
            {
                actor->drawState = static_cast<char>(MWMechanics::DrawState::Weapon);
                actor->setAnimFlags();
            }

            MechanicsHelper::processAttack(actor->attack, actor->getPtr(), false);
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readCast(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Reading ActorCast about %s", mapIndex.c_str());

            DedicatedActor *actor = dedicatedActors[mapIndex];
            if (!isActorCombatReplaySequenceAllowed(*actor, baseActor))
                continue;

            if (!normalizeSequencedPositionForCombat(*actor, baseActor))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Ignoring ActorCast about %s because its movement snapshot was missing or invalid",
                    mapIndex.c_str());
                continue;
            }

            acceptActorCombatReplaySequence(*actor, baseActor);
            actor->cast = baseActor.cast;

            // Set the correct drawState here if we've somehow we've missed a previous
            // AnimFlags packet
            if (actor->drawState != static_cast<char>(MWMechanics::DrawState::Spell))
            {
                actor->drawState = static_cast<char>(MWMechanics::DrawState::Spell);
                actor->setAnimFlags();
            }

            MechanicsHelper::processCast(actor->cast, actor->getPtr(), false);
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readCellChange(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    CellController *cellController = Main::get().getCellController();

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // Is a packet mistakenly moving the actor to the cell it's already in? If so, ignore it
        if (Misc::StringUtils::ciEqual(getShortDescription(), cellDescription(baseActor.cell)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Server says DedicatedActor %s moved to %s, but it was already there",
                mapIndex.c_str(), getShortDescription().c_str());
            continue;
        }

        if (dedicatedActors.count(mapIndex) > 0)
        {
            if (!isFinitePosition(baseActor.position) || !isFinitePosition(baseActor.direction))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                    "Ignoring ActorCellChange for %s with invalid coordinates", mapIndex.c_str());
                continue;
            }

            DedicatedActor *dedicatedActor = dedicatedActors[mapIndex];
            dedicatedActor->cell = baseActor.cell;
            dedicatedActor->position = baseActor.position;
            dedicatedActor->direction = baseActor.direction;
            dedicatedActor->movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(
                baseActor.movementSampleIntervalSeconds);
            dedicatedActor->movementLatencySeconds = sanitizeMovementLatencySeconds(baseActor.movementLatencySeconds);
            dedicatedActor->positionSequence = baseActor.positionSequence;
            dedicatedActor->hasPositionData = true;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Server says DedicatedActor %s moved to %s",
                mapIndex.c_str(), cellDescription(dedicatedActor->cell).c_str());

            MWWorld::CellStore *newStore = cellController->getCellStore(dedicatedActor->cell);
            if (!newStore)
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Destination cell doesn't exist on this client");
                MWBase::Environment::get().getWorld()->disable(dedicatedActor->getPtr());
                cellController->removeDedicatedActorRecord(mapIndex);
                delete dedicatedActor;
                dedicatedActors.erase(mapIndex);
                continue;
            }

            dedicatedActor->setCell(newStore);

            // If the cell this actor has moved to is active and not under our authority, move them to it
            if (cellController->isActiveWorldCell(dedicatedActor->cell) && !cellController->hasLocalAuthority(dedicatedActor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving DedicatedActor %s to our active cell %s",
                    mapIndex.c_str(), cellDescription(dedicatedActor->cell).c_str());
                cellController->initializeCell(dedicatedActor->cell);
                Cell *newCell = cellController->getCell(dedicatedActor->cell);
                if (newCell != nullptr)
                {
                    newCell->dedicatedActors[mapIndex] = dedicatedActor;
                    cellController->setDedicatedActorRecord(mapIndex, newCell->getShortDescription());
                }
                else
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Destination dedicated actor cell is no longer initialized");
                    cellController->removeDedicatedActorRecord(mapIndex);
                    delete dedicatedActor;
                }
            }
            else
            {
                if (cellController->hasLocalAuthority(dedicatedActor->cell))
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Creating new LocalActor based on %s in %s",
                        mapIndex.c_str(), cellDescription(dedicatedActor->cell).c_str());
                    Cell *newCell = cellController->getCell(dedicatedActor->cell);
                    if (newCell != nullptr)
                    {
                        LocalActor *localActor = new LocalActor();
                        localActor->cell = dedicatedActor->cell;
                        localActor->setPtr(dedicatedActor->getPtr());
                        localActor->position = dedicatedActor->position;
                        localActor->direction = dedicatedActor->direction;
                        localActor->movementSampleIntervalSeconds = sanitizeMovementSampleIntervalSeconds(
                            dedicatedActor->movementSampleIntervalSeconds);
                        localActor->movementLatencySeconds = sanitizeMovementLatencySeconds(
                            dedicatedActor->movementLatencySeconds);
                        localActor->positionSequence = dedicatedActor->positionSequence;
                        localActor->animFlagsSequence = dedicatedActor->animFlagsSequence;
                        localActor->hasAnimFlagsData = dedicatedActor->hasAnimFlagsData;
                        localActor->movementFlags = dedicatedActor->movementFlags;
                        localActor->drawState = dedicatedActor->drawState;
                        localActor->isFlying = dedicatedActor->isFlying;
                        localActor->creatureStats = dedicatedActor->creatureStats;
                        localActor->statsDynamicSequence = dedicatedActor->statsDynamicSequence;
                        localActor->hasStatsDynamicData = dedicatedActor->hasStatsDynamicData;

                        newCell->localActors[mapIndex] = localActor;
                        cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
                    }
                    else
                        LOG_APPEND(TimedLog::LOG_INFO, "- Destination local authority cell is no longer initialized");
                }

                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting DedicatedActor %s which is no longer needed",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeDedicatedActorRecord(mapIndex);
                delete dedicatedActor;
            }

            dedicatedActors.erase(mapIndex);
        }
    }
}

void Cell::initializeLocalActor(const MWWorld::Ptr& ptr)
{
    if (!hasLocalAuthority())
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "- Skipping LocalActor initialization in %s without local authority",
            getShortDescription().c_str());
        return;
    }

    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    LocalActor *actor = new LocalActor();
    actor->cell = makeActorPacketCell(*store->getCell());
    actor->setPtr(ptr);
    actor->position = ptr.getRefData().getPosition();

    localActors[mapIndex] = actor;

    Main::get().getCellController()->setLocalActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeLocalActors()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Initializing LocalActors in %s", getShortDescription().c_str());

    if (!hasLocalAuthority())
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "- Skipping LocalActor initialization in %s without local authority",
            getShortDescription().c_str());
        return;
    }

    store->forEach([&](const MWWorld::Ptr& ptr) {
        if (ptr.getClass().isActor())
        {
            // If this Ptr is lacking a unique index, ignore it
            if (ptr.getCellRef().getRefNum().mIndex == 0)
                return true;

            // If this Ptr is disabled or deleted, ignore it
            if (!ptr.getRefData().isEnabled() || isDeleted(ptr))
                return true;

            std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
            const std::string legacyIndex
                = Main::get().getCellController()->generateMapIndex(ptr.getCellRef().getRefNum().mIndex, 0);

            migrateActorMapIndex(localActors, legacyIndex, mapIndex, true, getShortDescription());

            // Only initialize this actor if it isn't already initialized
            if (localActors.count(mapIndex) == 0)
                initializeLocalActor(ptr);
        }

        return true;
    }, true);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActors in %s", getShortDescription().c_str());
}

void Cell::initializeDedicatedActor(const MWWorld::Ptr& ptr)
{
    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    DedicatedActor *actor = new DedicatedActor();
    actor->cell = makeActorPacketCell(*store->getCell());
    actor->setPtr(ptr);

    dedicatedActors[mapIndex] = actor;

    Main::get().getCellController()->setDedicatedActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeDedicatedActors(ActorList& actorList)
{
    acceptServerActorAuthorityIfNeeded(*this, actorList);

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // If this key doesn't exist, create it
        if (dedicatedActors.count(mapIndex) == 0)
        {
            const std::string legacyIndex = getLegacyActorIndexForServerMpNum(baseActor.mpNum);
            if (migrateActorMapIndex(dedicatedActors, legacyIndex, mapIndex, false, getShortDescription()))
                continue;

            MWWorld::Ptr ptrFound = searchExact(store, baseActor);

            if (ptrFound.isEmpty()) continue;

            initializeDedicatedActor(ptrFound);
        }
    }
}

void Cell::uninitializeLocalActors()
{
    for (const auto &actor : localActors)
    {
        Main::get().getCellController()->removeLocalActorRecord(actor.first);
        delete actor.second;
    }

    localActors.clear();
}

void Cell::uninitializeDedicatedActors(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);
        auto found = dedicatedActors.find(mapIndex);
        if (found == dedicatedActors.end())
            continue;

        Main::get().getCellController()->removeDedicatedActorRecord(mapIndex);
        delete found->second;
        dedicatedActors.erase(found);
    }
}

void Cell::uninitializeDedicatedActors()
{
    for (const auto &actor : dedicatedActors)
    {
        Main::get().getCellController()->removeDedicatedActorRecord(actor.first);
        delete actor.second;
    }

    dedicatedActors.clear();
}

LocalActor *Cell::getLocalActor(std::string actorIndex)
{
    auto found = localActors.find(actorIndex);
    if (found == localActors.end())
        return nullptr;

    return found->second;
}

DedicatedActor *Cell::getDedicatedActor(std::string actorIndex)
{
    auto found = dedicatedActors.find(actorIndex);
    if (found == dedicatedActors.end())
        return nullptr;

    return found->second;
}

bool Cell::hasLocalAuthority()
{
    return !serverActorAuthority && mwmp::isPacketGuidAssigned(authorityGuid)
        && authorityGuid == Main::get().getLocalPlayer()->guid;
}

bool Cell::hasServerActorAuthority() const
{
    return serverActorAuthority;
}

void Cell::setAuthority(const PacketGuid& guid)
{
    authorityGuid = guid;
    serverActorAuthority = false;
}

void Cell::setServerActorAuthority(bool enabled)
{
    if (serverActorAuthority == enabled)
        return;

    serverActorAuthority = enabled;
    if (!serverActorAuthority)
        return;

    authorityGuid = mwmp::unassignedPacketGuid();
    shouldInitializeActors = false;

    if (!localActors.empty())
        uninitializeLocalActors();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Cell %s is now using server actor authority",
        getShortDescription().c_str());
}

MWWorld::CellStore *Cell::getCellStore()
{
    return store;
}

std::string Cell::getShortDescription()
{
    return getCanonicalCellDescription(makeActorPacketCell(*store->getCell()));
}

