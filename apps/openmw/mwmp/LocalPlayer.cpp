#include <algorithm>
#include <cmath>

#include <components/esm/attr.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/misc/mathutil.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/soundmanager.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/spellcasting.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwphysics/iphysicsbackend.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/globalvariablename.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldmodel.hpp"
#include "../mwworld/worldimp.hpp"

#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ObjectList.hpp"
#include "PlayerList.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"
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

    ESM::RefId attributeRefId(int index)
    {
        return ESM::Attribute::indexToRefId(index);
    }

    ESM::Attribute::AttributeID attributeIdForStats(int index)
    {
        return ESM::Attribute::AttributeID(attributeRefId(index).getRefIdString());
    }

    ESM::RefId skillRefId(int index)
    {
        return ESM::Skill::indexToRefId(index);
    }

    mwmp::Target activeSpellCasterTarget(const MWMechanics::ActiveSpells::ActiveSpellParams& params,
        const MWWorld::Ptr& fallback)
    {
        MWWorld::Ptr caster = MWBase::Environment::get().getWorldModel()->getPtr(params.getCaster());
        if (caster.isEmpty())
            caster = fallback;

        return MechanicsHelper::getTarget(caster);
    }

    void applyActiveSpellPacketFlags(
        MWMechanics::ActiveSpells::ActiveSpellParams& params, const mwmp::ActiveSpell& activeSpell)
    {
        params.setFlag(ESM::ActiveSpells::Flag_Temporary);
        if (activeSpell.isStackingSpell)
            params.setFlag(ESM::ActiveSpells::Flag_Stackable);
    }

    bool hasValidLocalPlayerRace()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
        const ESM::RefId& race = ptrPlayer.get<ESM::NPC>()->mBase->mRace;

        return !race.empty() && world->getStore().get<ESM::Race>().search(race) != nullptr;
    }

    MWGui::GuiMode charGenModeForStage(int stage)
    {
        switch (stage)
        {
        case 0:
            return MWGui::GM_Name;
        case 1:
            return MWGui::GM_Race;
        case 2:
            return MWGui::GM_Class;
        case 3:
            return MWGui::GM_Birth;
        default:
            return MWGui::GM_Review;
        }
    }

    int charGenStageForMode(MWGui::GuiMode mode)
    {
        switch (mode)
        {
        case MWGui::GM_Name:
            return 0;
        case MWGui::GM_Race:
            return 1;
        case MWGui::GM_Class:
        case MWGui::GM_ClassPick:
        case MWGui::GM_ClassCreate:
        case MWGui::GM_ClassGenerate:
            return 2;
        case MWGui::GM_Birth:
            return 3;
        case MWGui::GM_Review:
            return 4;
        default:
            return -1;
        }
    }

    int activeCharGenStage(const MWBase::WindowManager& windowManager)
    {
        const int topStage = charGenStageForMode(windowManager.getMode());
        if (topStage != -1)
            return topStage;

        if (windowManager.containsMode(MWGui::GM_Name))
            return 0;
        if (windowManager.containsMode(MWGui::GM_Race))
            return 1;
        if (windowManager.containsMode(MWGui::GM_Class) || windowManager.containsMode(MWGui::GM_ClassPick)
            || windowManager.containsMode(MWGui::GM_ClassCreate) || windowManager.containsMode(MWGui::GM_ClassGenerate))
            return 2;
        if (windowManager.containsMode(MWGui::GM_Birth))
            return 3;
        if (windowManager.containsMode(MWGui::GM_Review))
            return 4;

        return -1;
    }

    void setOpenMwCharGenFinished(bool finished)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const int state = finished ? -1 : 1;

        if (world->getGlobalInt(MWWorld::Globals::sCharGenState) != state)
            world->setGlobalInt(MWWorld::Globals::sCharGenState, state);
    }

    constexpr float serverEquipmentReloadTimeout = 1.0f;

    constexpr float dynamicObjectSyncInterval = 0.1f;
    constexpr float dynamicObjectPositionEpsilonSquared = 4.0f;
    constexpr float dynamicObjectRotationEpsilon = 0.025f;

    float quaternionAngularDistance(const osg::Quat& left, const osg::Quat& right)
    {
        const double dot = std::abs(left.x() * right.x() + left.y() * right.y() + left.z() * right.z()
            + left.w() * right.w());
        return static_cast<float>(2.0 * std::acos(std::clamp(dot, 0.0, 1.0)));
    }

    osg::Quat objectRotationFromPosition(const ESM::Position& position)
    {
        return osg::Quat(position.rot[2], osg::Vec3f(0.0f, 0.0f, -1.0f))
            * osg::Quat(position.rot[1], osg::Vec3f(0.0f, -1.0f, 0.0f))
            * osg::Quat(position.rot[0], osg::Vec3f(-1.0f, 0.0f, 0.0f));
    }

    ESM::Position makeDynamicObjectPosition(
        const MWPhysics::IPhysicsBackend::DynamicBodySnapshot& snapshot)
    {
        ESM::Position position = snapshot.mPtr.getRefData().getPosition();
        position.pos[0] = snapshot.mPosition.x();
        position.pos[1] = snapshot.mPosition.y();
        position.pos[2] = snapshot.mPosition.z();

        const osg::Vec3f rotation = Misc::toEulerAnglesZYX(snapshot.mRotation);
        position.rot[0] = rotation.x();
        position.rot[1] = rotation.y();
        position.rot[2] = rotation.z();
        return position;
    }

    bool dynamicObjectPositionChanged(const ESM::Position& previous, const ESM::Position& current)
    {
        return (previous.asVec3() - current.asVec3()).length2() > dynamicObjectPositionEpsilonSquared;
    }

    bool dynamicObjectRotationChanged(const ESM::Position& previous, const ESM::Position& current)
    {
        return quaternionAngularDistance(objectRotationFromPosition(previous), objectRotationFromPosition(current))
            > dynamicObjectRotationEpsilon;
    }
}

std::map<std::string, int> storedItemRemovals;

LocalPlayer::LocalPlayer()
{
    deathTime = time(0);
    receivedCharacter = false;
    receivedCell = false;

    // Wait for the server to request character generation instead of opening the
    // name menu during the login round-trip for existing characters.
    charGenState.currentStage = 1;
    charGenState.endStage = 1;
    charGenState.isFinished = true;
    mCharGenBaseInfo.blank();
    mCharGenClass.blank();
    mHasCharGenClass = false;

    ignorePosPacket = false;
    ignoreJailTeleportation = false;
    ignoreJailSkillIncreases = false;
    
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    isChangingRegion = false;
    cellChangeReason = mwmp::CELL_CHANGE_REASON_NORMAL;

    jailProgressText = "";
    jailEndText = "";

    isUsingBed = false;
    avoidSendingInventoryPackets = false;
    isReceivingQuickKeys = false;
    isPlayingAnimation = false;
    diedSinceArrestAttempt = false;
}

LocalPlayer::~LocalPlayer()
{

}

Networking *LocalPlayer::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

MWWorld::Ptr LocalPlayer::getPlayerPtr()
{
    return MWBase::Environment::get().getWorld()->getPlayerPtr();
}

void LocalPlayer::update()
{
    static float updateTimer = 0;
    const float timeoutSec = 0.015f;

    if (mServerEquipmentReloadTimer > 0.f)
        mServerEquipmentReloadTimer = std::max(0.f,
            mServerEquipmentReloadTimer - MWBase::Environment::get().getFrameDuration());

    if ((updateTimer += MWBase::Environment::get().getFrameDuration()) >= timeoutSec)
    {
        updateTimer = std::fmod(updateTimer, timeoutSec);
        updateCell();
        getNetworking()->getWorldstate()->sendWeatherIfAuthorityChanged();
        updatePosition();
        updateAnimFlags();
        updateEquipment();
        updateStatsDynamic();
        updateAttackOrCast();
        updateDynamicObjects(timeoutSec);
        updateAttributes();
        updateSkills();
        updateLevel();
        updateBounty();
        updateReputation();
    }
}

void LocalPlayer::updateDynamicObjects(float dt)
{
    if (!isLoggedIn() || !receivedCell)
        return;

    mDynamicObjectSyncTimer += dt;
    if (mDynamicObjectSyncTimer < dynamicObjectSyncInterval)
        return;
    mDynamicObjectSyncTimer = std::fmod(mDynamicObjectSyncTimer, dynamicObjectSyncInterval);

    const auto* physics = dynamic_cast<const MWPhysics::IPhysicsBackend*>(
        MWBase::Environment::get().getWorld()->getRayCasting());
    if (physics == nullptr)
        return;

    const std::vector<MWPhysics::IPhysicsBackend::DynamicBodySnapshot> snapshots
        = physics->getDynamicBodySnapshots();
    if (snapshots.empty())
        return;

    mwmp::ObjectList* objectList = getNetworking()->getObjectList();
    mwmp::CellController* cellController = mwmp::Main::get().getCellController();

    for (const auto& snapshot : snapshots)
    {
        if (snapshot.mPtr.isEmpty() || snapshot.mPtr.getCell() == nullptr
            || snapshot.mPtr.getCell()->getCell() == nullptr)
            continue;

        const ESM::Cell& cell = snapshot.mPtr.getCell()->getCell()->getEsm3();
        if (!cellController->hasLocalAuthority(cell))
            continue;

        ESM::Position position = makeDynamicObjectPosition(snapshot);
        DynamicObjectSyncState& state = mDynamicObjectSyncStates[snapshot.mPtr.mRef];

        if (!state.mHasLastSent)
        {
            state.mLastSent = position;
            state.mHasLastSent = true;
            state.mLastActive = snapshot.mActive;
            continue;
        }

        const bool settledAfterMoving = state.mLastActive && !snapshot.mActive;
        const bool changed = dynamicObjectPositionChanged(state.mLastSent, position)
            || dynamicObjectRotationChanged(state.mLastSent, position);
        if (!changed && !settledAfterMoving)
        {
            state.mLastActive = snapshot.mActive;
            continue;
        }

        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectMove(snapshot.mPtr, position);
        objectList->sendObjectMove();

        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectRotate(snapshot.mPtr, position);
        objectList->sendObjectRotate();

        state.mLastSent = position;
        state.mLastActive = snapshot.mActive;
    }
}

void LocalPlayer::expectServerEquipmentReload()
{
    mServerEquipmentReloadTimer = serverEquipmentReloadTimeout;
}

void LocalPlayer::completeServerEquipmentReload()
{
    mServerEquipmentReloadTimer = 0.f;
}

void LocalPlayer::setCharGenBaseInfo(const ESM::NPC& character)
{
    mCharGenBaseInfo = character;
}

void LocalPlayer::setCharGenClass(const ESM::Class& selectedClass)
{
    mCharGenClass = selectedClass;
    mHasCharGenClass = true;
}

bool LocalPlayer::processCharGen()
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    const auto sendCharGenState = [&]() {
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();
    };

    const auto closePendingCharGenStage = [&]() {
        if (mPendingCharGenStage == -1)
            return;

        if (mPendingCharGenStage >= charGenState.endStage)
        {
            charGenState.currentStage = charGenState.endStage;
        }
        else if (charGenState.currentStage == mPendingCharGenStage)
        {
            charGenState.currentStage = std::min(mPendingCharGenStage + 1, charGenState.endStage);
        }

        mPendingCharGenStage = -1;
    };

    if (!charGenState.isFinished)
    {
        setOpenMwCharGenFinished(false);

        const int activeStage = activeCharGenStage(*windowManager);
        if (activeStage != -1)
        {
            if (mPendingCharGenStage != -1 && activeStage != mPendingCharGenStage)
                closePendingCharGenStage();

            if (activeStage != mPendingCharGenStage)
            {
                if (activeStage >= charGenState.endStage)
                {
                    charGenState.currentStage = charGenState.endStage;
                }
                else if (charGenState.currentStage != activeStage)
                {
                    charGenState.currentStage = activeStage;
                    sendCharGenState();
                }

                mPendingCharGenStage = activeStage;
            }

            return false;
        }
    }

    if (!charGenState.isFinished)
        closePendingCharGenStage();

    // If the current stage of CharGen is not the last one,
    // move to the next one
    if (charGenState.currentStage < charGenState.endStage)
    {
        const int currentStage = charGenState.currentStage;

        windowManager->pushGuiMode(charGenModeForStage(charGenState.currentStage));
        sendCharGenState();

        mPendingCharGenStage = currentStage;

        return false;
    }

    // If we've reached the last stage of CharGen, send the
    // corresponding packets and mark CharGen as finished
    if (!charGenState.isFinished)
    {
        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
        npc = *ptrPlayer.get<ESM::NPC>()->mBase;

        if (!mCharGenBaseInfo.mRace.empty())
        {
            if (npc.mRace != mCharGenBaseInfo.mRace || npc.mHead != mCharGenBaseInfo.mHead
                || npc.mHair != mCharGenBaseInfo.mHair || npc.isMale() != mCharGenBaseInfo.isMale())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Using cached OpenMW CharGen race data for TES3MP base info: race=%s, head=%s, hair=%s",
                    mCharGenBaseInfo.mRace.serializeText().c_str(),
                    mCharGenBaseInfo.mHead.serializeText().c_str(),
                    mCharGenBaseInfo.mHair.serializeText().c_str());
            }

            npc.mRace = mCharGenBaseInfo.mRace;
            npc.mHead = mCharGenBaseInfo.mHead;
            npc.mHair = mCharGenBaseInfo.mHair;
            npc.setIsMale(mCharGenBaseInfo.isMale());
        }

        birthsign = refIdToString(world->getPlayer().getBirthSign());

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Sending ID_PLAYER_BASEINFO to server with my CharGen info: race=%s, head=%s, hair=%s, birthsign=%s",
            npc.mRace.serializeText().c_str(), npc.mHead.serializeText().c_str(), npc.mHair.serializeText().c_str(),
            birthsign.c_str());
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();

        // Send stats packets if this is the 2nd round of CharGen that
        // only happens for new characters
        if (charGenState.endStage != 1)
        {
            updateStatsDynamic(true);
            updateAttributes(true);
            updateSkills(true);
            updateLevel(true);
            sendClass();
            mCharGenClass.blank();
            mHasCharGenClass = false;
            sendSpellbook();

            charGenState.currentStage = charGenState.endStage;
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();
        }

        // Mark character generation as finished until overridden by a new ID_PLAYER_CHARGEN packet
        charGenState.isFinished = true;
        setOpenMwCharGenFinished(true);
    }

    return true;
}

bool LocalPlayer::hasLoadedCharacter() const
{
    if (!receivedCell || !hasValidLocalPlayerRace())
        return false;

    if (receivedCharacter)
        return true;

    return charGenState.isFinished && charGenState.endStage > 1;
}

bool LocalPlayer::isLoggedIn() const
{
    return hasLoadedCharacter();
}

bool LocalPlayer::canSendJournalChanges()
{
    if (isLoggedIn())
        return true;

    if (receivedCharacter)
        return true;

    return charGenState.endStage > 1;
}

bool LocalPlayer::isApplyingServerTopicChanges() const
{
    return mApplyingServerTopicChanges;
}

void LocalPlayer::updateStatsDynamic(bool forceUpdate)
{
    if (statsDynamicIndexChanges.size() > 0)
        statsDynamicIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    static MWMechanics::DynamicStat<float> oldHealth(ptrCreatureStats->getHealth());
    static MWMechanics::DynamicStat<float> oldMagicka(ptrCreatureStats->getMagicka());
    static MWMechanics::DynamicStat<float> oldFatigue(ptrCreatureStats->getFatigue());


    // Update stats when they become 0 or they have changed enough
    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, float limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || std::abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 0.25f))
        statsDynamicIndexChanges.push_back(0);

    if (forceUpdate || needUpdate(oldMagicka, magicka, 4.f))
        statsDynamicIndexChanges.push_back(1);

    if (forceUpdate || needUpdate(oldFatigue, fatigue, 4.f))
        statsDynamicIndexChanges.push_back(2);

    if (forceUpdate || statsDynamicIndexChanges.size() > 0)
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();

        exchangeFullInfo = false;
        ++statsDynamicSequence;
        acceptCurrentStatsDynamicPacket();
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->Send();
    }
}

void LocalPlayer::updateAttributes(bool forceUpdate)
{
    // Only send attributes if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (attributeIndexChanges.size() > 0)
        attributeIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 8; ++i)
    {
        const ESM::RefId attributeId = attributeRefId(i);
        const MWMechanics::AttributeValue& attribute = ptrNpcStats.getAttribute(attributeId);
        const int skillIncrease = ptrNpcStats.getSkillIncreasesForAttribute(attributeIdForStats(i));

        if (attribute.getBase() != creatureStats.mAttributes[i].mBase ||
            attribute.getModifier() != creatureStats.mAttributes[i].mMod ||
            attribute.getDamage() != creatureStats.mAttributes[i].mDamage ||
            skillIncrease != npcStats.mSkillIncrease[i] ||
            forceUpdate)
        {
            attributeIndexChanges.push_back(static_cast<unsigned char>(i));
            attribute.writeState(creatureStats.mAttributes[i]);
            npcStats.mSkillIncrease[i] = skillIncrease;
        }
    }

    if (attributeIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->Send();
    }
}

void LocalPlayer::updateSkills(bool forceUpdate)
{
    // Only send skills if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (skillIndexChanges.size() > 0)
        skillIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 27; ++i)
    {
        const MWMechanics::SkillValue& skill = ptrNpcStats.getSkill(skillRefId(i));

        // Update a skill if its base value has changed at all or its progress has changed enough
        if (skill.getBase() != npcStats.mSkills[i].mBase ||
            skill.getModifier() != npcStats.mSkills[i].mMod ||
            skill.getDamage() != npcStats.mSkills[i].mDamage ||
            abs(skill.getProgress() - npcStats.mSkills[i].mProgress) > 0.75 ||
            forceUpdate)
        {
            skillIndexChanges.push_back(static_cast<unsigned char>(i));
            skill.writeState(npcStats.mSkills[i]);
        }
    }

    if (skillIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->Send();
    }
}

void LocalPlayer::updateLevel(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getLevel() != creatureStats.mLevel ||
        ptrNpcStats.getLevelProgress() != npcStats.mLevelProgress ||
        forceUpdate)
    {
        creatureStats.mLevel = ptrNpcStats.getLevel();
        npcStats.mLevelProgress = ptrNpcStats.getLevelProgress();
        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->Send();
    }
}

void LocalPlayer::updateBounty(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getBounty() != npcStats.mBounty || forceUpdate)
    {
        npcStats.mBounty = ptrNpcStats.getBounty();
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->Send();
    }
}

void LocalPlayer::updateReputation(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getReputation() != npcStats.mReputation || forceUpdate)
    {
        npcStats.mReputation = ptrNpcStats.getReputation();
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->Send();
    }
}

void LocalPlayer::updatePosition(bool forceUpdate, bool reliable, bool sendPacket)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    static bool posWasChanged = false;
    static bool wasJumping = false;
    static bool sentJumpEnd = true;
    static ESM::Position oldPosition;
    static bool hasOldPosition = false;

    position = ptrPlayer.getRefData().getPosition();
    if (!hasOldPosition)
    {
        oldPosition = position;
        hasOldPosition = true;
    }

    const auto sendPosition = [this, reliable, sendPacket]()
    {
        if (!sendPacket)
            return;

        ++positionSequence;
        PlayerPacket* packet = getNetworking()->getPlayerPacket(ID_PLAYER_POSITION);
        packet->setPlayer(this);

        if (reliable)
            packet->SendWithReliability(true, PacketReliability::ReliableOrdered);
        else
            packet->Send();
    };

    const MWMechanics::Movement& movement = ptrPlayer.getClass().getMovementSettings(ptrPlayer);
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
            std::abs(position.pos[axis] - oldPosition.pos[axis]) > transformEpsilon ||
            std::abs(position.rot[axis] - oldPosition.rot[axis]) > transformEpsilon;
    }
    if (!isPlayingAnimation)
        MechanicsHelper::deriveMissingMovementDirection(direction, position, oldPosition);

    bool posIsChanging = (direction.pos[0] != 0 || direction.pos[1] != 0 ||
        direction.pos[2] != 0 || direction.rot[0] != 0 || direction.rot[1] != 0 || direction.rot[2] != 0 ||
        (!world->isOnGround(ptrPlayer) && !world->isFlying(ptrPlayer)) || transformWasChanged);

    if (!sendPacket && forceUpdate)
    {
        oldPosition = position;
        posWasChanged = false;
        wasJumping = !world->isOnGround(ptrPlayer) && !world->isFlying(ptrPlayer);
        sentJumpEnd = true;
        return;
    }

    // Animations can change a player's position without actually creating directional movement,
    // so update positions accordingly
    if (!posIsChanging && isPlayingAnimation)
    {
        if (MWBase::Environment::get().getMechanicsManager()->checkAnimationPlaying(ptrPlayer, animation.groupname))
            posIsChanging = true;
        else
            isPlayingAnimation = false;
    }

    if (forceUpdate || posIsChanging || posWasChanged)
    {
        oldPosition = position;

        posWasChanged = posIsChanging;

        if (!wasJumping && !world->isOnGround(ptrPlayer) && !world->isFlying(ptrPlayer))
            wasJumping = true;

        sendPosition();
    }
    else if (wasJumping && world->isOnGround(ptrPlayer))
    {
        wasJumping = false;
        sentJumpEnd = false;
    }
    // Packet with jump end position has to be sent one tick after above check
    else if (!sentJumpEnd)
    {
        sentJumpEnd = true;
        position = ptrPlayer.getRefData().getPosition();
        oldPosition = position;
        sendPosition();
    }
}

void LocalPlayer::updateCell(bool forceUpdate, bool sendPositionPacket)
{
    const MWWorld::Cell *worldCell = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell()->getCell();
    const ESM::Cell *ptrCell = &worldCell->getEsm3();

    // If the LocalPlayer's Ptr cell is different from the LocalPlayer's packet cell, proceed
    if (forceUpdate || !Main::get().getCellController()->isSameCell(*ptrCell, cell))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_CHANGE about LocalPlayer to server");

        LOG_APPEND(TimedLog::LOG_INFO, "- Moved from %s to %s", cell.getDescription().c_str(),
                   ptrCell->getDescription().c_str());

        if (cell.mRegion != ptrCell->mRegion)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Changed region from %s to %s",
                cell.mRegion.empty() ? "none" : cell.mRegion.serializeText().c_str(),
                ptrCell->mRegion.empty() ? "none" : ptrCell->mRegion.serializeText().c_str());

            isChangingRegion = true;
        }

        cell = *ptrCell;
        previousCellPosition = position;

        // Cell-change packets carry the current transform. Sending a standalone
        // position first lets the server judge a legal door teleport against the
        // old cell and send a stale correction back during the load fade.
        if (sendPositionPacket)
        {
            updatePosition(true, false, false);
            ++positionSequence;
            mHasPendingCellChangePositionSequence = true;
            mPendingCellChangePositionSequence = positionSequence;
        }

        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->Send();

        isChangingRegion = false;
        cellChangeReason = mwmp::CELL_CHANGE_REASON_NORMAL;

        // If this is an interior cell, are there any other players in it? If so,
        // enable their markers
        if (!ptrCell->isExterior())
        {
            mwmp::PlayerList::enableMarkers(*ptrCell);
        }
    }

}

void LocalPlayer::updateEquipment(bool forceUpdate)
{
    if (mServerEquipmentReloadTimer > 0.f)
        return;

    if (equipmentIndexChanges.size() > 0)
        equipmentIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &invStore = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            MWWorld::CellRef &cellRef = it->getCellRef();

            if (cellRef.getRefId() != stringRefId(item.refId) ||
                cellRef.getCharge() != item.charge ||
                Utils::compareFloats(cellRef.getEnchantmentCharge(), item.enchantmentCharge, 1.0f) == false ||
                cellRef.getCount() != item.count ||
                forceUpdate)
            {
                equipmentIndexChanges.push_back(slot);

                item.refId = refIdToString(it->getCellRef().getRefId());
                item.count = it->getCellRef().getCount();
                item.charge = it->getCellRef().getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentIndexChanges.push_back(slot);
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
        }
    }

    if (equipmentIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        ++equipmentSequence;
        acceptCurrentEquipmentPacket();
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->Send();
    }
}

void LocalPlayer::updateInventory(bool forceUpdate)
{
    static bool invChanged = false;

    if (forceUpdate)
        invChanged = true;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    auto setItem = [](Item &item, const MWWorld::Ptr &iter) {
        item.refId = refIdToString(iter.getCellRef().getRefId());
        if (item.refId.find("$dynamic") != std::string::npos)
            return true;
        item.count = iter.getCellRef().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = refIdToString(iter.getCellRef().getSoul());

        return false;
    };

    if (!invChanged)
    {
        for (const auto &itemOld : inventoryChanges.items)
        {
            auto result = ptrInventory.begin();
            for (; result != ptrInventory.end(); ++result)
            {
                if(setItem(item, *result))
                    continue;

                if (item == itemOld)
                    break;
            }
            if (result == ptrInventory.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
    {
        for (const auto &iter : ptrInventory)
        {
            if(setItem(item, iter))
                continue;

            auto items = inventoryChanges.items;

            if (find(items.begin(), items.end(), item) == items.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
        return;

    invChanged = false;

    sendInventory();
}

void LocalPlayer::updateAttackOrCast()
{
    const bool attackReady = attack.shouldSend && !MechanicsHelper::shouldDeferLocalAttack(attack);

    if (attackReady || cast.shouldSend)
        updatePosition(true);

    if (attackReady)
    {
        advanceCombatSequence();
        acceptCurrentCombatPacket();
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->Send();
        if (attack.isHit && attack.success)
            MechanicsHelper::syncLocalDynamicStatsForTarget(attack.target);

        attack.shouldSend = false;
    }
    if (cast.shouldSend)
    {
        const bool castReleased = !cast.pressed;
        const bool castSucceeded = cast.success;

        advanceCombatSequence();
        acceptCurrentCombatPacket();
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->Send();
        if (castReleased)
        {
            updateStatsDynamic(true);
            if (castSucceeded)
                MechanicsHelper::syncLocalDynamicStatsForTarget(cast.target);
        }

        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalPlayer::updateAnimFlags(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
    using namespace MWMechanics;

    static bool wasRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    static bool wasSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak);
    static bool wasForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    static bool wasForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    bool isRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak);
    bool isForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);
    
    isFlying = world->isFlying(ptrPlayer);
    isJumping = !world->isOnGround(ptrPlayer) && !isFlying;

    // We need to send a new packet at the end of jumping, flying and TCL-ing too,
    // so keep track of what we were doing last frame
    static bool wasJumping = false;
    static bool wasFlying = false;
    static bool hadTcl = false;

    drawState = static_cast<char>(ptrPlayer.getClass().getNpcStats(ptrPlayer).getDrawState());
    static char lastDrawState = drawState;

    if (wasRunning != isRunning ||
        wasSneaking != isSneaking || wasForceJumping != isForceJumping ||
        wasForceMoveJumping != isForceMoveJumping || lastDrawState != drawState ||
        wasJumping || isJumping || wasFlying != isFlying || hadTcl != hasTcl ||
        forceUpdate)
    {
        wasSneaking = isSneaking;
        wasRunning = isRunning;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = drawState;
        
        wasJumping = isJumping;
        wasFlying = isFlying;
        hadTcl = hasTcl;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping || isJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        if (isJumping)
            updatePosition(true); // fix position after jump;

        ++animFlagsSequence;
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->Send();
    }
}

void LocalPlayer::addItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWWorld::ESMStore &esmStore = MWBase::Environment::get().getWorld()->getStore();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        try
        {
            MWWorld::ManualRef itemRef(esmStore, stringRefId(item.refId), item.count);
            MWWorld::Ptr itemPtr = itemRef.getPtr();

            if (item.charge != -1)
                itemPtr.getCellRef().setCharge(item.charge);

            if (item.enchantmentCharge != -1)
                itemPtr.getCellRef().setEnchantmentCharge(item.enchantmentCharge);

            if (!item.soul.empty())
                itemPtr.getCellRef().setSoul(stringRefId(item.soul));

            LOG_APPEND(TimedLog::LOG_INFO, "- Adding inventory item %s with count %i", item.refId.c_str(), item.count);

            ptrStore.add(itemPtr, item.count, false);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid inventory item %s", item.refId.c_str());
        }
    }

    updateInventoryWindow();
}

void LocalPlayer::addSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto &spell : spellbookChanges.spells)
        // Only add spells that are ensured to exist
        if (MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(spell.mId))
            ptrSpells.add(spell.mId);
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid spell %s", spell.mId.serializeText().c_str());
}

void LocalPlayer::addSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MWWorld::Ptr caster = MechanicsHelper::getPlayerPtr(activeSpell.caster);
        if (caster.isEmpty())
            caster = ptrPlayer;

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        MWMechanics::ActiveSpells::ActiveSpellParams params(
            caster, stringRefId(activeSpell.id), activeSpell.params.mDisplayName, ESM::RefNum());
        params.setActiveSpellId(activeSpell.params.mActiveSpellId);
        params.getEffects() = activeSpell.params.mEffects;
        applyActiveSpellPacketFlags(params, activeSpell);
        activeSpells.addSpell(params);
    }
}

void LocalPlayer::addJournalItems()
{
    const bool showJournalMessage = !journalChangesAreLoad;

    if (journalChangesAreLoad && !mApplyingServerJournalLoad)
    {
        MWBase::Environment::get().getJournal()->clear();
        mApplyingServerJournalLoad = true;
    }
    else if (!journalChangesAreLoad)
        mApplyingServerJournalLoad = false;

    for (const auto &journalItem : journalChanges)
    {
        MWWorld::Ptr ptrFound;

        if (journalItem.type == JournalItem::ENTRY)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: ENTRY, quest: %s, index: %i, actorRefId: %s",
                journalItem.quest.c_str(), journalItem.index, journalItem.actorRefId.c_str());

            ptrFound = MWBase::Environment::get().getWorld()->searchPtr(stringRefId(journalItem.actorRefId), false);

            if (ptrFound.isEmpty())
                ptrFound = getPlayerPtr();
        }
        else if (journalItem.type == JournalItem::INDEX)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: INDEX, quest: %s, index: %i",
                journalItem.quest.c_str(), journalItem.index);
        }
        else if (journalItem.type == JournalItem::FINISHED)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: FINISHED, quest: %s, finished: %s",
                journalItem.quest.c_str(), journalItem.isFinished ? "true" : "false");
        }

        try
        {
            if (journalItem.type == JournalItem::ENTRY)
            {
                MWBase::Environment::get().getJournal()->addEntry(
                    stringRefId(journalItem.quest), journalItem.index, ptrFound, showJournalMessage);
            }
            else if (journalItem.type == JournalItem::INDEX)
                MWBase::Environment::get().getJournal()->setJournalIndex(stringRefId(journalItem.quest), journalItem.index);
            else if (journalItem.type == JournalItem::FINISHED)
                MWBase::Environment::get().getJournal()->getOrStartQuest(stringRefId(journalItem.quest)).setFinished(
                    journalItem.isFinished);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid journal quest %s", journalItem.quest.c_str());
        }
    }

    journalChangesAreLoad = false;
}

void LocalPlayer::addTopics()
{
    auto &env = MWBase::Environment::get();

    if (topicChangesAreLoad && !mApplyingServerTopicLoad)
    {
        env.getDialogueManager()->clear();
        mApplyingServerTopicLoad = true;
    }
    else if (!topicChangesAreLoad)
        mApplyingServerTopicLoad = false;

    struct ServerTopicChangeGuard
    {
        bool& mFlag;

        explicit ServerTopicChangeGuard(bool& flag)
            : mFlag(flag)
        {
            mFlag = true;
        }

        ~ServerTopicChangeGuard()
        {
            mFlag = false;
        }
    } guard(mApplyingServerTopicChanges);

    for (const auto &topic : topicChanges)
    {
        std::string topicId = topic.topicId;

        // If we're using a translated version of Morrowind, translate this topic from English into our language
        topicId = std::string(env.getWindowManager()->getTranslationDataStorage().topicKeyword(topicId));

        env.getDialogueManager()->addTopic(stringRefId(topicId));
    }

    topicChangesAreLoad = false;
}

void LocalPlayer::removeItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        ptrStore.remove(stringRefId(item.refId), item.count);

        LOG_APPEND(TimedLog::LOG_INFO, "- Removing inventory item %s with count %i", item.refId.c_str(), item.count);
    }
}

void LocalPlayer::removeSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();
    const MWWorld::Store<ESM::Spell>& spellStore = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>();

    MWBase::WindowManager *wm = MWBase::Environment::get().getWindowManager();
    for (const auto &spell : spellbookChanges.spells)
    {
        if (spell.mId.empty())
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored removal of empty spell id");
            continue;
        }

        const ESM::Spell* storeSpell = spellStore.search(spell.mId);
        if (!storeSpell)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored removal of invalid spell %s", spell.mId.serializeText().c_str());
            continue;
        }

        ptrSpells.remove(storeSpell);
        if (spell.mId == wm->getSelectedSpell())
            wm->unsetSelectedSpell();
    }
}

void LocalPlayer::removeSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
  
    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- removing %sstacking active spell %s", activeSpell.isStackingSpell ? "" : "non-", activeSpell.id.c_str());

        if (activeSpell.isStackingSpell)
        {
            activeSpells.removeEffectsByActiveSpellId(ptrPlayer, stringRefId(activeSpell.id));
        }
        else
        {
            activeSpells.removeEffectsBySourceSpellId(ptrPlayer, stringRefId(activeSpell.id));
        }
    }
}

void LocalPlayer::die()
{
    creatureStats.mDead = true;

    MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
    MWMechanics::DynamicStat<float> health = playerPtr.getClass().getCreatureStats(playerPtr).getHealth();
    health.setCurrent(0);
    playerPtr.getClass().getCreatureStats(playerPtr).setHealth(health);
    health.writeState(creatureStats.mDynamic[0]);
    acceptCurrentStatsDynamicPacket();

    updatePosition(true);
    advanceCombatSequence();
    acceptCurrentCombatPacket();
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();
}

void LocalPlayer::resurrect()
{
    creatureStats.mDead = false;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    bool markerRespawn = false;

    if (resurrectType == mwmp::RESURRECT_TYPE::IMPERIAL_SHRINE)
    {
        queueCellChangeReason(mwmp::CELL_CHANGE_REASON_RESPAWN);
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, stringRefId("divinemarker"));
        markerRespawn = true;
    }
    else if (resurrectType == mwmp::RESURRECT_TYPE::TRIBUNAL_TEMPLE)
    {
        queueCellChangeReason(mwmp::CELL_CHANGE_REASON_RESPAWN);
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, stringRefId("templemarker"));
        markerRespawn = true;
    }

    MWBase::Environment::get().getMechanicsManager()->resurrect(ptrPlayer);
    if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Ended)
        MWBase::Environment::get().getStateManager()->resumeGame();

    // The player could have died from a hand-to-hand attack, so reset their fatigue
    // as well
    if (creatureStats.mDynamic[2].mMod < 1)
        creatureStats.mDynamic[2].mMod = 1;

    creatureStats.mDynamic[2].mCurrent = creatureStats.mDynamic[2].mMod;
    MWMechanics::DynamicStat<float> fatigue;
    fatigue.readState(creatureStats.mDynamic[2]);
    ptrPlayer.getClass().getCreatureStats(ptrPlayer).setFatigue(fatigue);

    // If this player had a weapon or spell readied when dying, they will still have it
    // readied but be unable to use it unless we clear it here
    ptrPlayer.getClass().getNpcStats(ptrPlayer).setDrawState(MWMechanics::DrawState::Nothing);

    // Record that the player has died since the last attempt was made to arrest them,
    // used to make guards lenient enough to attempt an arrest again
    diedSinceArrestAttempt = true;

    deathTime = time(0);

    LOG_APPEND(TimedLog::LOG_INFO, "- diedSinceArrestAttempt is now true");

    // Record that we are no longer a known werewolf, to avoid being attacked infinitely
    MWBase::Environment::get().getWorld()->setGlobalInt(
        MWWorld::GlobalVariableName(std::string_view("pcknownwerewolf")), 0);

    // Ensure we unequip any items with constant effects that can put us into an infinite
    // death loop
    static const ESM::RefId damageEffects[5] = { ESM::MagicEffect::DrainHealth, ESM::MagicEffect::FireDamage,
        ESM::MagicEffect::FrostDamage, ESM::MagicEffect::ShockDamage, ESM::MagicEffect::SunDamage };

    for (const auto &damageEffect : damageEffects)
        MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, damageEffect);

    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->Send();

    updateStatsDynamic(true);
    if (markerRespawn)
        updateCell(true);
}

void LocalPlayer::closeInventoryWindows()
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Container) ||
        MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Inventory))
        MWBase::Environment::get().getWindowManager()->popGuiMode();

    MWBase::Environment::get().getWindowManager()->setDragDrop(false);
}

void LocalPlayer::updateInventoryWindow()
{
    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
}

void LocalPlayer::setCharacter()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    receivedCharacter = false;

    // Ignore invalid races
    if (world->getStore().get<ESM::Race>().search(npc.mRace) != 0)
    {
        receivedCharacter = true;

        if (charGenState.endStage <= 1)
        {
            charGenState.currentStage = charGenState.endStage;
            charGenState.isFinished = true;
            mPendingCharGenStage = -1;
            setOpenMwCharGenFinished(true);
        }

        MWBase::Environment::get().getWorld()->getPlayer().setBirthSign(stringRefId(birthsign));

        if (resetStats)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerRace(npc.mRace, npc.isMale(), npc.mHead, npc.mHair);
            setEquipment();
        }
        else
        {
            ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;

            player.mRace = npc.mRace;
            player.mHead = npc.mHead;
            player.mHair = npc.mHair;
            player.mModel = npc.mModel;
            player.setIsMale(npc.isMale());
            world->getStore().overrideRecord(player);

            MWBase::Environment::get().getMechanicsManager()->playerLoaded();

            // This is needed to update the player's model instantly if they're in 3rd person
            world->reattachPlayerCamera();
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->rebuildAvatar();
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Character update was ignored due to invalid race %s", npc.mRace.serializeText().c_str());
    }
}

void LocalPlayer::setDynamicStats()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> dynamicStat;

    if (creatureStats.mDead)
    {
        dynamicStat = ptrCreatureStats->getHealth();
        dynamicStat.setBase(creatureStats.mDynamic[0].mBase);
        dynamicStat.setCurrent(0);
        ptrCreatureStats->setHealth(dynamicStat);
        return;
    }

    auto applyDynamicStat = [&](int index) {
        if (index < 0 || index >= 3)
            return;

        dynamicStat = ptrCreatureStats->getDynamic(index);
        dynamicStat.setBase(creatureStats.mDynamic[index].mBase);
        dynamicStat.setCurrent(creatureStats.mDynamic[index].mCurrent);
        ptrCreatureStats->setDynamic(index, dynamicStat);
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

    if (ptrCreatureStats->isDead())
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptrPlayer);
}

void LocalPlayer::setAttributes()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::AttributeValue attributeValue;

    for (int attributeIndex = 0; attributeIndex < 8; ++attributeIndex)
    {
        const ESM::RefId attributeId = attributeRefId(attributeIndex);

        // If the server wants to clear our attribute's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (creatureStats.mAttributes[attributeIndex].mMod == 0 && ptrNpcStats->getAttribute(attributeId).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffect(ptrPlayer, ESM::MagicEffect::FortifyAttribute, attributeId);
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this attribute still higher than 0? If so, unequip items that
            // fortify the attribute
            if (ptrNpcStats->getAttribute(attributeId).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect,
                    ESM::MagicEffect::FortifyAttribute, attributeRefId(attributeIndex));
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        attributeValue.readState(creatureStats.mAttributes[attributeIndex]);
        ptrNpcStats->setAttribute(attributeId, attributeValue);

        ptrNpcStats->setSkillIncreasesForAttribute(
            attributeIdForStats(attributeIndex), npcStats.mSkillIncrease[attributeIndex]);
    }
}

void LocalPlayer::setSkills()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::SkillValue skillValue;

    for (int skillIndex = 0; skillIndex < 27; ++skillIndex)
    {
        const ESM::RefId skillId = skillRefId(skillIndex);

        // If the server wants to clear our skill's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (npcStats.mSkills[skillIndex].mMod == 0 && ptrNpcStats->getSkill(skillId).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffect(ptrPlayer, ESM::MagicEffect::FortifySkill, skillId);
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this skill still higher than 0? If so, unequip items that
            // fortify the skill
            if (ptrNpcStats->getSkill(skillId).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect,
                    ESM::MagicEffect::FortifySkill, {}, skillRefId(skillIndex));
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        skillValue.readState(npcStats.mSkills[skillIndex]);
        ptrNpcStats->setSkill(skillId, skillValue);
    }
}

void LocalPlayer::setLevel()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setLevel(creatureStats.mLevel);
    ptrNpcStats->setLevelProgress(npcStats.mLevelProgress);
}

void LocalPlayer::setBounty()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setBounty(npcStats.mBounty);
}

void LocalPlayer::setReputation()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setReputation(npcStats.mReputation);
}

void LocalPlayer::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    if (mHasPendingCellChangePositionSequence
        && !isPlayerPositionSequenceAtLeast(positionSequence, mPendingCellChangePositionSequence))
    {
        LOG_APPEND(TimedLog::LOG_INFO,
            "- Ignored stale cross-cell server position sequence %u from before local cell change sequence %u",
            positionSequence, mPendingCellChangePositionSequence);

        // PacketPlayerPosition has already decoded into this object, so restore the local
        // world transform snapshot without echoing a new packet.
        updatePosition(true, false, false);
        return;
    }

    if (mHasPendingCellChangePositionSequence)
        mHasPendingCellChangePositionSequence = false;

    // If we're ignoring this position packet because of an invalid cell change,
    // don't make the next one get ignored as well
    if (ignorePosPacket)
        ignorePosPacket = false;
    else
    {
        world->getPlayer().setTeleported(true);

        world->moveObject(ptrPlayer, position.asVec3());
        world->rotateObject(ptrPlayer, position.asRotationVec3());
    }

    // This transform came from the server. Update the local movement baseline,
    // but do not echo it back as a fresh client movement packet.
    updatePosition(true, false, false);
}

void LocalPlayer::setMomentum()
{
    static_cast<void>(momentum);
}

void LocalPlayer::setCell()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
    ESM::Position pos;
    bool cellApplied = true;
    const bool hasServerPosition = hasFinitePositionPacket();

    // To avoid crashes, close container windows this player may be in
    closeInventoryWindows();

    world->getPlayer().setTeleported(true);

    int x = cell.mData.mX;
    int y = cell.mData.mY;

    if (cell.isExterior())
    {
        if (hasServerPosition)
        {
            pos = position;
        }
        else
        {
            const osg::Vec2f cellPosition
                = ESM::indexToPosition(ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId), true);
            pos.pos[0] = cellPosition.x();
            pos.pos[1] = cellPosition.y();
            pos.pos[2] = 0;

            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;
        }

        world->changeToCell(ESM::RefId::esm3ExteriorCell(x, y), pos, true);
        if (!hasServerPosition)
            world->fixPosition();
    }
    else if (ESM::RefId exteriorCellId = world->findExteriorPosition(cell.mName, pos); !exteriorCellId.empty())
    {
        if (hasServerPosition)
            pos = position;

        world->changeToCell(exteriorCellId, pos, true);
        if (!hasServerPosition)
            world->fixPosition();
    }
    else
    {
        try
        {
            if (!hasServerPosition && world->findInteriorPosition(cell.mName, pos).empty())
                throw std::runtime_error("Cell doesn't exist on this client");

            if (hasServerPosition)
                pos = position;

            world->changeToInteriorCell(cell.mName, pos, true);
        }
        // If we've been sent to an invalid interior, ignore the incoming
        // packet about our position in that cell
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "%s", "- Cell doesn't exist on this client");
            ignorePosPacket = true;
            cellApplied = false;
        }
    }

    updateCell(true, false);
    if (cellApplied)
        updatePosition(true, false, false);
    receivedCell = cellApplied;
}

void LocalPlayer::setClass()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_CLASS from server");

    if (charClass.mId.empty()) // custom class
    {
        charClass.mData.mIsPlayable = 0x1;
        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass);
    }
    else
    {
        const ESM::Class *existingCharClass = MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().search(charClass.mId);

        if (existingCharClass)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass.mId);
        }
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid default class %s", charClass.mId.serializeText().c_str());
    }
}

void LocalPlayer::setEquipment()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

    const auto applySlot = [&](int slot) {
        if (slot < 0 || slot >= MWWorld::InventoryStore::Slots)
            return;

        mwmp::Item &currentItem = equipmentItems[slot];

        if (!currentItem.refId.empty())
        {
            const ESM::RefId currentItemId = stringRefId(currentItem.refId);
            auto it = std::find_if(ptrInventory.begin(), ptrInventory.end(), [&currentItem](const MWWorld::Ptr &itemPtr) {
                return itemPtr.getCellRef().getRefId() == stringRefId(currentItem.refId);
            });

            // If the item is not in our inventory, add it.
            if (it == ptrInventory.end())
            {
                try
                {
                    auto addIter = ptrInventory.ContainerStore::add(currentItemId, currentItem.count, false);

                    ptrInventory.equip(slot, addIter);
                }
                catch (std::exception&)
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid equipment item %s", currentItem.refId.c_str());
                }
            }
            else
            {
                // Don't try to equip an item that is already equipped
                if (ptrInventory.getSlot(slot) != it)
                    ptrInventory.equip(slot, it);
            }
        }
        else
            ptrInventory.unequipSlot(slot);
    };

    if (exchangeFullInfo)
    {
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
            applySlot(slot);
    }
    else
    {
        for (const int slot : equipmentIndexChanges)
            applySlot(slot);
    }

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updatePlayer();
}

void LocalPlayer::setInventory()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    // Ensure no item is being drag and dropped
    MWBase::Environment::get().getWindowManager()->setDragDrop(false);

    // Clear items in inventory
    ptrStore.clear();

    // Proceed by adding items
    addItems();

    // Don't automatically setEquipment() here, or the player could end
    // up getting a new set of their starting clothes, or other items
    // supposed to no longer exist
    //
    // Instead, expect server scripts to do that manually
}

void LocalPlayer::restoreEquipmentFromInventory()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore& ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        const mwmp::Item& currentItem = equipmentItems[slot];

        if (currentItem.refId.empty())
        {
            ptrInventory.unequipSlot(slot);
            continue;
        }

        auto it = std::find_if(ptrInventory.begin(), ptrInventory.end(), [&currentItem](const MWWorld::Ptr& itemPtr) {
            return itemPtr.getCellRef().getRefId() == stringRefId(currentItem.refId);
        });

        if (it != ptrInventory.end() && ptrInventory.getSlot(slot) != it)
            ptrInventory.equip(slot, it);
    }

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updatePlayer();
}

void LocalPlayer::setSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    // Clear spells in spellbook, while ignoring abilities, powers, etc.
    while (true)
    {
        auto iter = ptrSpells.begin();
        for (; iter != ptrSpells.end(); iter++)
        {
            const ESM::Spell *spell = *iter;
            if (spell->mData.mType == ESM::Spell::ST_Spell)
            {
                ptrSpells.remove(spell);
                break;
            }
        }
        if (iter == ptrSpells.end())
            break;
    }

    // Proceed by adding spells
    addSpells();
}

void LocalPlayer::setSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
    activeSpells.clear(ptrPlayer);

    // Proceed by adding spells active
    addSpellsActive();
}

void LocalPlayer::setCooldowns()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells& ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto& cooldown : cooldownChanges)
    {
        const ESM::RefId cooldownId = stringRefId(cooldown.id);
        if (world->getStore().get<ESM::Spell>().search(cooldownId))
        {
            const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(cooldownId);

            ptrSpells.usePower(spell);
        }
    }
}

void LocalPlayer::setQuickKeys()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_QUICKKEYS from server");

    for (const auto &quickKey : quickKeyChanges)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

        if (quickKey.type == QuickKey::ITEM || quickKey.type == QuickKey::ITEM_MAGIC)
        {
            MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

            auto it = std::find_if(ptrInventory.begin(), ptrInventory.end(), [&quickKey](const MWWorld::Ptr &inventoryItem) {
                return inventoryItem.getCellRef().getRefId() == stringRefId(quickKey.itemId);
            });

            static_cast<void>(it);
        }
        else if (quickKey.type == QuickKey::MAGIC)
        {
            MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();
            bool hasSpell = false;

            auto iter = ptrSpells.begin();
            for (; iter != ptrSpells.end(); iter++)
            {
                const ESM::Spell *spell = *iter;
                if (spell->mId == stringRefId(quickKey.itemId))
                {
                    hasSpell = true;
                    break;
                }
            }

            static_cast<void>(hasSpell);
        }
    }
}

void LocalPlayer::setFactions()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_FACTION from server - action: %i", factionChanges.action);

    for (const auto &faction : factionChanges.factions)
    {
        const ESM::RefId factionId = stringRefId(faction.factionId);
        LOG_APPEND(TimedLog::LOG_VERBOSE, " - processing faction: %s", faction.factionId.c_str());
        const ESM::Faction *esmFaction = MWBase::Environment::get().getWorld()->getStore().get<ESM::Faction>().search(factionId);

        if (!esmFaction)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid faction %s", faction.factionId.c_str());
            continue;
        }

        if (factionChanges.action == mwmp::FactionChanges::RANK)
        {

            if (!ptrNpcStats.isInFaction(factionId))
            {
                // If the player isn't in this faction, make them join it
                ptrNpcStats.joinFaction(factionId);
                LOG_APPEND(TimedLog::LOG_VERBOSE, "\t>JOINED FACTION: %s on rank change to: %d.",
                    faction.factionId.c_str(), faction.rank);
            }

            ptrNpcStats.setFactionRank(factionId, faction.rank);
        }
        else if (factionChanges.action == mwmp::FactionChanges::EXPULSION)
        {
            // If the expelled state is different in the packet than in the NpcStats,
            // adjust the NpcStats accordingly
            if (faction.isExpelled != ptrNpcStats.getExpelled(factionId))
            {
                if (faction.isExpelled)
                    ptrNpcStats.expell(factionId, false);
                else
                    ptrNpcStats.clearExpelled(factionId);
            }
        }

        else if (factionChanges.action == mwmp::FactionChanges::REPUTATION)
            ptrNpcStats.setFactionReputation(factionId, faction.reputation);
    }
}

void LocalPlayer::setBooks()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (bookChangesAreLoad && !mApplyingServerBookLoad)
    {
        ptrNpcStats.clearUsedIds();
        mApplyingServerBookLoad = true;
    }
    else if (!bookChangesAreLoad)
        mApplyingServerBookLoad = false;

    for (const auto &book : bookChanges)
        ptrNpcStats.flagAsUsed(stringRefId(book.bookId));

    bookChangesAreLoad = false;
}

void LocalPlayer::setShapeshift()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWBase::Environment::get().getWorld()->scaleObject(ptrPlayer, scale);
    MWBase::Environment::get().getMechanicsManager()->setWerewolf(ptrPlayer, isWerewolf);
}

void LocalPlayer::setMarkLocation()
{
    MWWorld::CellStore *ptrCellStore = Main::get().getCellController()->getCellStore(markCell);

    if (ptrCellStore)
        MWBase::Environment::get().getWorld()->getPlayer().markPosition(ptrCellStore, markPosition);
}

void LocalPlayer::setSelectedSpell()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats& stats = ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::Spells& spells = stats.getSpells();
    MWWorld::ContainerStore& store = ptrPlayer.getClass().getContainerStore(ptrPlayer);
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

    const ESM::RefId spellId = stringRefId(selectedSpellId);

    if (spellId.empty())
    {
        store.setSelectedEnchantItem(store.end());
        windowManager->unsetSelectedSpell();
        return;
    }

    const ESM::Spell* spell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(spellId);
    if (!spell)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored selection of invalid spell %s", selectedSpellId.c_str());
        return;
    }

    if (!spells.hasSpell(spell))
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored selection of unavailable spell %s", selectedSpellId.c_str());
        return;
    }
 
    store.setSelectedEnchantItem(store.end());
    windowManager->setSelectedSpell(spellId, int(MWMechanics::getSpellSuccessChance(spellId, ptrPlayer)));
}

void LocalPlayer::setSelectedEnchantedItem()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::ContainerStore& store = ptrPlayer.getClass().getContainerStore(ptrPlayer);
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

    if (selectedEnchantedItem.refId.empty() || selectedEnchantedItem.count <= 0)
    {
        store.setSelectedEnchantItem(store.end());
        return;
    }

    MWWorld::Ptr itemPtr = MechanicsHelper::getItemPtrFromStore(selectedEnchantedItem, store);
    if (itemPtr.isEmpty() || itemPtr.getClass().getEnchantment(itemPtr).empty())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored selection of unavailable enchanted item %s",
            selectedEnchantedItem.refId.c_str());
        return;
    }

    MWWorld::ContainerStoreIterator it = std::find(store.begin(), store.end(), itemPtr);
    if (it == store.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored selection of missing enchanted item %s",
            selectedEnchantedItem.refId.c_str());
        return;
    }

    store.setSelectedEnchantItem(it);
    windowManager->setSelectedEnchantItem(*it);
}

void LocalPlayer::sendDeath(char newDeathState)
{
    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(getPlayerPtr());

    updatePosition(true);
    deathState = newDeathState;
    updateStatsDynamic(true);
    advanceCombatSequence();
    acceptCurrentCombatPacket();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_DEATH about myself to server\n- deathState: %d", deathState);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

void LocalPlayer::sendClass()
{
    if (mHasCharGenClass)
    {
        charClass = mCharGenClass;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Using cached OpenMW CharGen class data for TES3MP class info: id=%s, name=%s",
            charClass.mId.serializeText().c_str(), charClass.mName.c_str());
    }
    else
    {
        MWBase::World *world = MWBase::Environment::get().getWorld();
        const ESM::NPC *npcBase = world->getPlayerPtr().get<ESM::NPC>()->mBase;
        const ESM::Class *esmClass = world->getStore().get<ESM::Class>().find(npcBase->mClass);

        if (npcBase->mClass.serializeText().find("$dynamic") != std::string::npos) // custom class
        {
            charClass.mId = ESM::RefId();
            charClass.mName = esmClass->mName;
            charClass.mDescription = esmClass->mDescription;
            charClass.mData = esmClass->mData;
        }
        else
            charClass.mId = esmClass->mId;
    }

    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->Send();
}

void LocalPlayer::sendInventory()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending entire inventory to server");

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    inventoryChanges.items.clear();

    for (const auto &iter : ptrInventory)
    {
        item.refId = refIdToString(iter.getCellRef().getRefId());

        // Skip any items that somehow have clientside-only dynamic IDs
        if (item.refId.find("$dynamic") != std::string::npos)
            continue;

        // Skip bound items
        if (MWBase::Environment::get().getMechanicsManager()->isBoundItem(iter))
            continue;

        item.count = iter.getCellRef().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = refIdToString(iter.getCellRef().getSoul());

        inventoryChanges.items.push_back(item);
    }

    inventoryChanges.action = InventoryChanges::SET;
    ++inventorySequence;
    acceptCurrentInventoryPacket();
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const mwmp::Item& item, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        item.refId.c_str(), action, item.count);

    inventoryChanges.items.clear();
    inventoryChanges.items.push_back(item);
    inventoryChanges.action = action;

    ++inventorySequence;
    acceptCurrentInventoryPacket();
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const MWWorld::Ptr& itemPtr, int count, unsigned int action)
{
    mwmp::Item item = MechanicsHelper::getItem(itemPtr, count);
    sendItemChange(item, action);
}

void LocalPlayer::sendItemChange(const std::string& refId, int count, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        refId.c_str(), action, count);

    inventoryChanges.items.clear();
    
    mwmp::Item item;
    item.refId = refId;
    item.count = count;
    item.charge = -1;
    item.enchantmentCharge = -1;
    item.soul = "";

    inventoryChanges.items.push_back(item);

    inventoryChanges.action = action;
    ++inventorySequence;
    acceptCurrentInventoryPacket();
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendStoredItemRemovals()
{
    inventoryChanges.items.clear();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending stored item removals for LocalPlayer:");

    for (auto storedItemRemoval : storedItemRemovals)
    {
        mwmp::Item item;
        item.refId = storedItemRemoval.first;
        item.count = storedItemRemoval.second;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        inventoryChanges.items.push_back(item);

        LOG_APPEND(TimedLog::LOG_INFO, "- %s with count %i", item.refId.c_str(), item.count);
    }

    inventoryChanges.action = mwmp::InventoryChanges::ACTION_TYPE::REMOVE;
    ++inventorySequence;
    acceptCurrentInventoryPacket();
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();

    storedItemRemovals.clear();
}

void LocalPlayer::sendSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    spellbookChanges.spells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto &spell : ptrSpells)
    {
        if (spell->mData.mType == ESM::Spell::ST_Spell)
            spellbookChanges.spells.push_back(*spell);
    }

    spellbookChanges.action = SpellbookChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellChange(std::string id, unsigned int action)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellbookChanges.spells.clear();

    ESM::Spell spell;
    spell.mId = stringRefId(id);
    spellbookChanges.spells.push_back(spell);

    spellbookChanges.action = action;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    spellsActiveChanges.activeSpells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto& ptrSpell : activeSpells)
    {
        mwmp::ActiveSpell packetSpell;
        packetSpell.id = refIdToString(ptrSpell.getSourceSpellId());
        packetSpell.isStackingSpell = ptrSpell.hasFlag(ESM::ActiveSpells::Flag_Stackable);
        packetSpell.caster = activeSpellCasterTarget(ptrSpell, ptrPlayer);
        packetSpell.params.mActiveSpellId = ptrSpell.getActiveSpellId();
        packetSpell.params.mSourceSpellId = ptrSpell.getSourceSpellId();
        packetSpell.params.mDisplayName = ptrSpell.getDisplayName();
        packetSpell.params.mEffects = ptrSpell.getEffects();
        spellsActiveChanges.activeSpells.push_back(packetSpell);
    }

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = activeSpellCasterTarget(params, getPlayerPtr());
    spell.timestampDay = 0;
    spell.timestampHour = 0;
    spell.params.mActiveSpellId = params.getActiveSpellId();
    spell.params.mSourceSpellId = params.getSourceSpellId();
    spell.params.mEffects = params.getEffects();
    spell.params.mDisplayName = params.getDisplayName();
    spellsActiveChanges.activeSpells.push_back(spell);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell addition with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
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

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell removal with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendCooldownChange(std::string id, int startTimestampDay, float startTimestampHour)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    cooldownChanges.clear();

    SpellCooldown spellCooldown;
    spellCooldown.id = id;
    spellCooldown.startTimestampDay = startTimestampDay;
    spellCooldown.startTimestampHour = startTimestampHour;

    cooldownChanges.push_back(spellCooldown);
;
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->Send();
}

void LocalPlayer::sendQuickKey(unsigned short slot, int type, const std::string& itemId)
{
    quickKeyChanges.clear();

    mwmp::QuickKey quickKey;
    quickKey.slot = slot;
    quickKey.type = type;
    quickKey.itemId = itemId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_QUICKKEYS", itemId.c_str());
    LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

    quickKeyChanges.push_back(quickKey);

    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->Send();
}

void LocalPlayer::sendJournalEntry(const std::string& quest, int index, const MWWorld::Ptr& actor)
{
    journalChanges.clear();
    journalChangesAreLoad = false;

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::ENTRY;
    journalItem.quest = quest;
    journalItem.index = index;
    journalItem.actorRefId = actor.isEmpty() ? "" : refIdToString(actor.getCellRef().getRefId());
    journalItem.hasTimestamp = true;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    journalItem.timestamp.daysPassed = world->getGlobalInt(MWWorld::Globals::sDaysPassed);
    journalItem.timestamp.month = world->getGlobalInt(MWWorld::Globals::sMonth);
    journalItem.timestamp.day = world->getGlobalInt(MWWorld::Globals::sDay);

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendJournalIndex(const std::string& quest, int index)
{
    journalChanges.clear();
    journalChangesAreLoad = false;

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::INDEX;
    journalItem.quest = quest;
    journalItem.index = index;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendJournalFinished(const std::string& quest, bool isFinished)
{
    journalChanges.clear();
    journalChangesAreLoad = false;

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::FINISHED;
    journalItem.quest = quest;
    journalItem.isFinished = isFinished;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendFactionRank(const std::string& factionId, int rank)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::RANK;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.rank = rank;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionExpulsionState(const std::string& factionId, bool isExpelled)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::EXPULSION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.isExpelled = isExpelled;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionReputation(const std::string& factionId, int reputation)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::REPUTATION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.reputation = reputation;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendTopic(const std::string& topicId)
{
    topicChanges.clear();
    topicChangesAreLoad = false;

    mwmp::Topic topic;

    // For translated versions of the game, make sure we translate the topic back into English first
    topic.topicId = std::string(MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().topicStandardForm(topicId));

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_TOPIC with topic %s", topic.topicId.c_str());

    topicChanges.push_back(topic);

    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->Send();
}

void LocalPlayer::sendBook(const std::string& bookId)
{
    bookChanges.clear();
    bookChangesAreLoad = false;

    mwmp::Book book;
    book.bookId = bookId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BOOK with book %s", book.bookId.c_str());

    bookChanges.push_back(book);

    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->Send();
}

void LocalPlayer::sendWerewolfState(bool werewolfState)
{
    isWerewolf = werewolfState;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_SHAPESHIFT with isWerewolf of %s", isWerewolf ? "true" : "false");

    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->Send();
}

void LocalPlayer::sendMarkLocation(const ESM::Cell& newMarkCell, const ESM::Position& newMarkPosition)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION;
    markCell = newMarkCell;
    markPosition = newMarkPosition;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendSelectedSpell(const std::string& newSelectedSpellId)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL;
    selectedSpellId = newSelectedSpellId;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendSelectedEnchantedItem(const mwmp::Item& newSelectedEnchantedItem)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_ENCHANTED_ITEM;
    selectedEnchantedItem = newSelectedEnchantedItem;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendItemUse(const MWWorld::Ptr& itemPtr, bool itemMagicState, char currentDrawState)
{
    usedItem.refId = refIdToString(itemPtr.getCellRef().getRefId());
    usedItem.count = itemPtr.getCellRef().getCount();
    usedItem.charge = itemPtr.getCellRef().getCharge();
    usedItem.enchantmentCharge = itemPtr.getCellRef().getEnchantmentCharge();
    usedItem.soul = refIdToString(itemPtr.getCellRef().getSoul());

    usingItemMagic = itemMagicState;
    itemUseDrawState = currentDrawState;

    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->Send();
}

void LocalPlayer::sendCellStates()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_STATE to server");
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->Send();
}

void LocalPlayer::queueCellChangeReason(unsigned int reason)
{
    if (mwmp::isValidCellChangeReason(reason))
        cellChangeReason = reason;
}

void LocalPlayer::clearCellStates()
{
    cellStateChanges.clear();
}

void LocalPlayer::clearCurrentContainer()
{
    currentContainer.refId = "";
    currentContainer.refNum = 0;
    currentContainer.mpNum = 0;
    currentContainer.loot = false;
}

void LocalPlayer::storeCellState(const ESM::Cell& storedCell, int stateType)
{
    std::vector<CellState>::iterator iter;

    for (iter = cellStateChanges.begin(); iter != cellStateChanges.end(); )
    {
        // If there's already a cell state recorded for this particular cell,
        // remove it
        if (storedCell.getDescription() == (*iter).cell.getDescription())
            iter = cellStateChanges.erase(iter);
        else
            ++iter;
    }

    CellState cellState;
    cellState.cell = storedCell;
    cellState.type = stateType;

    cellStateChanges.push_back(cellState);
}

void LocalPlayer::storeCurrentContainer(const MWWorld::Ptr &container)
{
    currentContainer.refId = refIdToString(container.getCellRef().getRefId());
    currentContainer.refNum = container.getCellRef().getRefNum().mIndex;
    currentContainer.mpNum = getNetworking()->getObjectList()->getServerMpNum(container);
}

void LocalPlayer::storeItemRemoval(const std::string& refId, int count)
{
    storedItemRemovals[refId] = storedItemRemovals[refId] + count;
}

void LocalPlayer::storeLastEnchantmentQuantity(unsigned int quantity)
{
    lastEnchantmentQuantity = quantity;
}

void LocalPlayer::playAnimation()
{
    MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(getPlayerPtr(),
        animation.groupname, animation.mode, animation.count, animation.persist);

    isPlayingAnimation = true;
}

void LocalPlayer::playSpeech()
{
    MWBase::Environment::get().getSoundManager()->say(getPlayerPtr(), VFS::Path::Normalized(sound));
}


