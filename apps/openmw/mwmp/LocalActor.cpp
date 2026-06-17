#include <cmath>

#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"

#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldmodel.hpp"
#include "../mwworld/worldimp.hpp"

#include "LocalActor.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ActorList.hpp"
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
        return ESM::RefId::stringRefId(id);
    }

    std::string refIdToString(const ESM::RefId& id)
    {
        return id.serializeText();
    }

    std::string cellDescription(const ESM::Cell& cell)
    {
        return getCanonicalCellDescription(cell);
    }

    mwmp::Target activeSpellCasterTarget(const MWMechanics::ActiveSpells::ActiveSpellParams& params,
        const MWWorld::Ptr& fallback)
    {
        MWWorld::Ptr caster = MWBase::Environment::get().getWorldModel()->getPtr(params.getCaster());
        if (caster.isEmpty())
            caster = fallback;

        return MechanicsHelper::getTarget(caster);
    }
}

LocalActor::LocalActor()
{
    hasSentData = false;
    posWasChanged = false;
    equipmentChanged = false;

    wasRunning = false;
    wasSneaking = false;
    wasForceJumping = false;
    wasForceMoveJumping = false;
    wasFlying = false;

    attack.type = Attack::MELEE;
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.type = Cast::REGULAR;
    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    creatureStats.mDead = false;
    creatureStats.mDeathAnimationFinished = false;
}

LocalActor::~LocalActor()
{

}

void LocalActor::update(bool forceUpdate)
{
    refreshNetworkId();

    updateStatsDynamic(forceUpdate);
    updateEquipment(forceUpdate, false);

    if (forceUpdate || !creatureStats.mDeathAnimationFinished)
    {
        updatePosition(forceUpdate);
        updateAnimFlags(forceUpdate);
        updateAnimPlay();
        updateSpeech();
        updateAttackOrCast();
    }

    hasSentData = true;
}

void LocalActor::updateCell()
{
    refreshNetworkId();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Sending ID_ACTOR_CELL_CHANGE about %s %i-%i in cell %s to server",
                       refId.c_str(), refNum, mpNum, cellDescription(cell).c_str());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moved to cell %s",
        std::string(ptr.getCell()->getCell()->getDescription()).c_str());

    cell = makeActorPacketCell(*ptr.getCell()->getCell());
    position = ptr.getRefData().getPosition();
    const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
    for (int axis = 0; axis < 3; ++axis)
    {
        direction.pos[axis] = MechanicsHelper::sanitizeMovementComponent(movement.mPosition[axis]);
        direction.rot[axis] = MechanicsHelper::sanitizeMovementComponent(movement.mRotation[axis]);
    }
    ++positionSequence;
    hasPositionData = true;
    isFollowerCellChange = false;

    mwmp::Main::get().getNetworking()->getActorList()->addCellChangeActor(*this);
}

void LocalActor::updatePosition(bool forceUpdate)
{
    refreshNetworkId();

    bool posIsChanging = false;
    const ESM::Position ptrPosition = ptr.getRefData().getPosition();

    const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
    for (int axis = 0; axis < 3; ++axis)
    {
        direction.pos[axis] = MechanicsHelper::sanitizeMovementComponent(movement.mPosition[axis]);
        direction.rot[axis] = MechanicsHelper::sanitizeMovementComponent(movement.mRotation[axis]);
    }

    const float transformEpsilon = 0.0001f;
    bool transformWasChanged = false;
    for (int axis = 0; axis < 3; ++axis)
    {
        transformWasChanged = transformWasChanged ||
            std::abs(ptrPosition.pos[axis] - position.pos[axis]) > transformEpsilon ||
            std::abs(ptrPosition.rot[axis] - position.rot[axis]) > transformEpsilon;
    }

    if (!creatureStats.mDead)
        MechanicsHelper::deriveMissingMovementDirection(direction, ptrPosition, position);

    if (creatureStats.mDead)
    {
        posIsChanging = transformWasChanged;
    }
    else
    {
        posIsChanging = direction.pos[0] != 0 || direction.pos[1] != 0 || direction.pos[2] != 0 ||
            direction.rot[0] != 0 || direction.rot[1] != 0 || direction.rot[2] != 0 ||
            !MWBase::Environment::get().getWorld()->isOnGround(ptr) || transformWasChanged;
    }

    if (forceUpdate || posIsChanging || posWasChanged)
    {
        posWasChanged = posIsChanging;
        position = ptr.getRefData().getPosition();
        hasPositionData = true;
        ++positionSequence;
        mwmp::Main::get().getNetworking()->getActorList()->addPositionActor(*this);
    }
}

void LocalActor::updateAnimFlags(bool forceUpdate)
{
    refreshNetworkId();

    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWMechanics::CreatureStats ptrCreatureStats = ptr.getClass().getCreatureStats(ptr);

    using namespace MWMechanics;

    bool isRunning = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Sneak);
    bool isForceJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    isFlying = world->isFlying(ptr);
    isJumping = !world->isOnGround(ptr) && !isFlying;

    MWMechanics::DrawState currentDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();

    if (wasRunning != isRunning || wasSneaking != isSneaking ||
        wasForceJumping != isForceJumping || wasForceMoveJumping != isForceMoveJumping ||
        lastDrawState != currentDrawState || wasFlying != isFlying ||
        wasJumping || isJumping ||
        forceUpdate)
    {

        wasRunning = isRunning;
        wasSneaking = isSneaking;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = currentDrawState;

        wasFlying = isFlying;
        wasJumping = isJumping;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping || isJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        drawState = static_cast<char>(currentDrawState);

        ++animFlagsSequence;
        mwmp::Main::get().getNetworking()->getActorList()->addAnimFlagsActor(*this);
    }
}

void LocalActor::updateAnimPlay()
{
    refreshNetworkId();

    if (!animation.groupname.empty())
    {
        updatePosition(true);
        ++combatSequence;
        hasCombatData = true;
        mwmp::Main::get().getNetworking()->getActorList()->addAnimPlayActor(*this);
        animation.groupname.clear();
    }
}

void LocalActor::updateSpeech()
{
    refreshNetworkId();

    if (!sound.empty())
    {
        mwmp::Main::get().getNetworking()->getActorList()->addSpeechActor(*this);
        sound.clear();
    }
}

void LocalActor::updateStatsDynamic(bool forceUpdate)
{
    refreshNetworkId();

    if (storeStatsDynamic(forceUpdate))
        mwmp::Main::get().getNetworking()->getActorList()->addStatsDynamicActor(*this);
}

bool LocalActor::storeStatsDynamic(bool forceUpdate)
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    // Update stats when they become 0 or they have changed enough
    //
    // Also check for an oldHealth of 0 changing to something else for resurrected NPCs

    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, float limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || std::abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 0.25f) || needUpdate(oldMagicka, magicka, 7.f) ||
        needUpdate(oldFatigue, fatigue, 7.f))
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();
        creatureStats.mDeathAnimationFinished = ptrCreatureStats->isDeathAnimationFinished();
        ++statsDynamicSequence;

        return true;
    }

    return false;
}

void LocalActor::updateEquipment(bool forceUpdate, bool sendImmediately)
{
    refreshNetworkId();

    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore &invStore = ptr.getClass().getInventoryStore(ptr);
    
    // If we've never sent any data, autoEquip the actor just in case its inventory
    // slots have been cleared by a previous Container packet
    if (!hasSentData)
        invStore.autoEquip();

    if (forceUpdate)
        equipmentChanged = true;

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            auto &cellRef = it->getCellRef();
            if (cellRef.getRefId() != stringRefId(item.refId))
            {
                equipmentChanged = true;

                item.refId = refIdToString(cellRef.getRefId());
                item.charge = cellRef.getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                item.count = it->getCellRef().getCount();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentChanged = true;
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
        }
    }

    if (equipmentChanged)
    {
        ++equipmentSequence;
        hasEquipmentData = true;

        if (sendImmediately)
            sendEquipment();
        else
            mwmp::Main::get().getNetworking()->getActorList()->addEquipmentActor(*this);

        equipmentChanged = false;
    }
}

void LocalActor::updateAttackOrCast()
{
    refreshNetworkId();

    const bool attackReady = attack.shouldSend && !MechanicsHelper::shouldDeferLocalAttack(attack);

    if (attackReady || cast.shouldSend)
        updatePosition(true);

    if (attackReady)
    {
        ++combatSequence;
        hasCombatData = true;
        mwmp::Main::get().getNetworking()->getActorList()->addAttackActor(*this);
        if (attack.isHit && attack.success)
            MechanicsHelper::queueLocalDynamicStatsForTarget(attack.target);
        attack.shouldSend = false;
    }
    if (cast.shouldSend)
    {
        const bool castReleased = !cast.pressed;
        const bool castSucceeded = cast.success;

        ++combatSequence;
        hasCombatData = true;
        mwmp::Main::get().getNetworking()->getActorList()->addCastActor(*this);
        if (castReleased)
        {
            updateStatsDynamic(true);
            if (castSucceeded)
                MechanicsHelper::queueLocalDynamicStatsForTarget(cast.target);
        }
        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalActor::sendEquipment()
{
    refreshNetworkId();

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
}

void LocalActor::sendStatsDynamic()
{
    refreshNetworkId();
    storeStatsDynamic(true);

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->Send();
}

void LocalActor::sendSpellsActive()
{
    refreshNetworkId();

    MWMechanics::ActiveSpells& activeSpells = ptr.getClass().getCreatureStats(ptr).getActiveSpells();

    spellsActiveChanges.activeSpells.clear();

    for (const auto& ptrSpell : activeSpells)
    {
        mwmp::ActiveSpell packetSpell;
        packetSpell.id = refIdToString(ptrSpell.getSourceSpellId());
        packetSpell.isStackingSpell = ptrSpell.hasFlag(ESM::ActiveSpells::Flag_Stackable);
        packetSpell.caster = activeSpellCasterTarget(ptrSpell, ptr);
        packetSpell.params.mActiveSpellId = ptrSpell.getActiveSpellId();
        packetSpell.params.mSourceSpellId = ptrSpell.getSourceSpellId();
        packetSpell.params.mDisplayName = ptrSpell.getDisplayName();
        packetSpell.params.mEffects = ptrSpell.getEffects();
        spellsActiveChanges.activeSpells.push_back(packetSpell);
    }

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    refreshNetworkId();

    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = activeSpellCasterTarget(params, ptr);
    spell.timestampDay = 0;
    spell.timestampHour = 0;
    spell.params.mActiveSpellId = params.getActiveSpellId();
    spell.params.mSourceSpellId = params.getSourceSpellId();
    spell.params.mEffects = params.getEffects();
    spell.params.mDisplayName = params.getDisplayName();
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
    refreshNetworkId();

    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.timestampDay = timestamp.getDay();
    spell.timestampHour = timestamp.getHour();
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendDeath(char newDeathState)
{
    refreshNetworkId();
    updatePosition(true);
    deathState = newDeathState;
    sendStatsDynamic();

    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(ptr);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_ACTOR_DEATH about %s %i-%i in cell %s to server\n- deathState: %d",
        refId.c_str(), refNum, mpNum, cellDescription(cell).c_str(), deathState);

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

MWWorld::Ptr LocalActor::getPtr()
{
    return ptr;
}

void LocalActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = refIdToString(ptr.getCellRef().getRefId());
    refreshNetworkId();

    lastDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();
    oldHealth = ptr.getClass().getCreatureStats(ptr).getHealth();
    oldMagicka = ptr.getClass().getCreatureStats(ptr).getMagicka();
    oldFatigue = ptr.getClass().getCreatureStats(ptr).getFatigue();
}

void LocalActor::refreshNetworkId()
{
    if (ptr.isEmpty())
        return;

    const auto [networkRefNum, networkMpNum] = Main::get().getCellController()->getActorNetworkId(ptr);
    refNum = networkRefNum;
    mpNum = networkMpNum;
}

