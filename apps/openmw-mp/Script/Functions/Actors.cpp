#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Utils.hpp>
#include <apps/openmw-mp/processors/ActorProcessor.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

#include <components/esm3/creaturestats.hpp>

#include <algorithm>

#include "Actors.hpp"

using namespace mwmp;

BaseActorList *readActorList;
BaseActorList writeActorList;

BaseActor tempActor;
const BaseActor emptyActor = {};
std::vector<ESM::ActiveEffect> storedActorActiveEffects;

static std::string tempCellDescription;

namespace
{
    bool hasSameActorIdentity(const BaseActor& left, const BaseActor& right)
    {
        return left.refNum == right.refNum && left.mpNum == right.mpNum
            && (left.refId.empty() || right.refId.empty() || left.refId == right.refId);
    }

    BaseActor* findStoredActor(Cell* serverCell, const BaseActor& actor)
    {
        if (serverCell == nullptr)
            return nullptr;

        BaseActorList* actorList = serverCell->getActorList();
        if (actorList == nullptr)
            return nullptr;

        for (BaseActor& storedActor : actorList->baseActors)
        {
            if (hasSameActorIdentity(storedActor, actor))
                return &storedActor;
        }

        return nullptr;
    }

    void syncActorListCount(BaseActorList& actorList)
    {
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
    }

    void advanceActorStatsDynamicSequences(Cell* serverCell, BaseActorList& actorList)
    {
        syncActorListCount(actorList);

        for (BaseActor& actor : actorList.baseActors)
        {
            if (BaseActor* storedActor = findStoredActor(serverCell, actor))
                actor.statsDynamicSequence = storedActor->statsDynamicSequence + 1;
            else
                ++actor.statsDynamicSequence;
        }
    }

    void advanceActorEquipmentSequences(Cell* serverCell, BaseActorList& actorList)
    {
        syncActorListCount(actorList);

        for (BaseActor& actor : actorList.baseActors)
        {
            if (BaseActor* storedActor = findStoredActor(serverCell, actor))
                actor.equipmentSequence = storedActor->equipmentSequence + 1;
            else
                ++actor.equipmentSequence;

            actor.hasEquipmentData = true;
        }
    }

    void advanceActorPositionSequences(Cell* serverCell, BaseActorList& actorList)
    {
        syncActorListCount(actorList);

        for (BaseActor& actor : actorList.baseActors)
        {
            if (BaseActor* storedActor = findStoredActor(serverCell, actor))
                actor.positionSequence = storedActor->positionSequence + 1;
            else
                ++actor.positionSequence;
        }
    }

    bool playerHasLoadedCell(Player* player, Cell* serverCell)
    {
        if (player == nullptr || serverCell == nullptr)
            return false;

        CellController::TContainer* loadedCells = player->getCells();
        return loadedCells != nullptr && std::find(loadedCells->begin(), loadedCells->end(), serverCell) != loadedCells->end();
    }

    bool canAssignActorAuthority(Cell* serverCell, PacketGuid guid)
    {
        return playerHasLoadedCell(Players::getPlayer(guid), serverCell);
    }
}

void ActorFunctions::ReadReceivedActorList() noexcept
{
    readActorList = mwmp::Networking::getPtr()->getReceivedActorList();
}

void ActorFunctions::ReadCellActorList(const char* cellDescription) noexcept
{
    ESM::Cell esmCell = Utils::getCellFromDescription(cellDescription);
    Cell *serverCell = CellController::get()->getCell(&esmCell);

    if (serverCell != nullptr)
        readActorList = serverCell->getActorList();
    else
        readActorList = {};
}

void ActorFunctions::ClearActorList() noexcept
{
    writeActorList.cell.blank();
    writeActorList.baseActors.clear();
    syncActorListCount(writeActorList);
}

void ActorFunctions::SetActorListPid(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    writeActorList.guid = player->guid;
}

void ActorFunctions::CopyReceivedActorListToStore() noexcept
{
    if (readActorList == nullptr)
    {
        writeActorList.cell.blank();
        writeActorList.baseActors.clear();
        syncActorListCount(writeActorList);
        return;
    }

    writeActorList = *readActorList;
    syncActorListCount(writeActorList);
}

unsigned int ActorFunctions::GetActorListSize() noexcept
{
    if (readActorList == nullptr)
        return 0;

    return static_cast<unsigned int>(readActorList->baseActors.size());
}

unsigned char ActorFunctions::GetActorListAction() noexcept
{
    return readActorList->action;
}

const char *ActorFunctions::GetActorCell(unsigned int index) noexcept
{
    tempCellDescription = readActorList->baseActors.at(index).cell.getDescription();
    return tempCellDescription.c_str();
}

const char *ActorFunctions::GetActorRefId(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).refId.c_str();
}

unsigned int ActorFunctions::GetActorRefNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).refNum;
}

unsigned int ActorFunctions::GetActorMpNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).mpNum;
}

double ActorFunctions::GetActorPosX(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.pos[0];
}

double ActorFunctions::GetActorPosY(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.pos[1];
}

double ActorFunctions::GetActorPosZ(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.pos[2];
}

double ActorFunctions::GetActorRotX(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.rot[0];
}

double ActorFunctions::GetActorRotY(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.rot[1];
}

double ActorFunctions::GetActorRotZ(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).position.rot[2];
}

double ActorFunctions::GetActorHealthBase(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[0].mBase;
}

double ActorFunctions::GetActorHealthCurrent(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[0].mCurrent;
}

double ActorFunctions::GetActorHealthModified(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[0].mMod;
}

double ActorFunctions::GetActorMagickaBase(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[1].mBase;
}

double ActorFunctions::GetActorMagickaCurrent(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[1].mCurrent;
}

double ActorFunctions::GetActorMagickaModified(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[1].mMod;
}

double ActorFunctions::GetActorFatigueBase(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[2].mBase;
}

double ActorFunctions::GetActorFatigueCurrent(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[2].mCurrent;
}

double ActorFunctions::GetActorFatigueModified(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).creatureStats.mDynamic[2].mMod;
}

const char *ActorFunctions::GetActorEquipmentItemRefId(unsigned int index, unsigned short slot) noexcept
{
    return readActorList->baseActors.at(index).equipmentItems[slot].refId.c_str();
}

int ActorFunctions::GetActorEquipmentItemCount(unsigned int index, unsigned short slot) noexcept
{
    return readActorList->baseActors.at(index).equipmentItems[slot].count;
}

int ActorFunctions::GetActorEquipmentItemCharge(unsigned int index, unsigned short slot) noexcept
{
    return readActorList->baseActors.at(index).equipmentItems[slot].charge;
}

double ActorFunctions::GetActorEquipmentItemEnchantmentCharge(unsigned int index, unsigned short slot) noexcept
{
    return readActorList->baseActors.at(index).equipmentItems[slot].enchantmentCharge;
}

bool ActorFunctions::DoesActorHavePlayerKiller(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).killer.isPlayer;
}

int ActorFunctions::GetActorKillerPid(unsigned int index) noexcept
{
    Player *player = Players::getPlayer(readActorList->baseActors.at(index).killer.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ActorFunctions::GetActorKillerRefId(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).killer.refId.c_str();
}

unsigned int ActorFunctions::GetActorKillerRefNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).killer.refNum;
}

unsigned int ActorFunctions::GetActorKillerMpNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).killer.mpNum;
}

const char *ActorFunctions::GetActorKillerName(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).killer.name.c_str();
}

unsigned int ActorFunctions::GetActorDeathState(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).deathState;
}

unsigned int ActorFunctions::GetActorSpellsActiveChangesSize(unsigned int actorIndex) noexcept
{
    return static_cast<unsigned int>(readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.size());
}

unsigned int ActorFunctions::GetActorSpellsActiveChangesAction(unsigned int actorIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.action;
}

const char* ActorFunctions::GetActorSpellsActiveId(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).id.c_str();
}

const char* ActorFunctions::GetActorSpellsActiveDisplayName(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).params.mDisplayName.c_str();
}

bool ActorFunctions::GetActorSpellsActiveStackingState(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).isStackingSpell;
}

unsigned int ActorFunctions::GetActorSpellsActiveEffectCount(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return static_cast<unsigned int>(readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).params.mEffects.size());
}

unsigned int ActorFunctions::GetActorSpellsActiveEffectId(unsigned int actorIndex, unsigned int spellIndex, unsigned int effectIndex) noexcept
{
    const int effectId = Utils::getLegacyIndexFromActiveEffectId(readActorList->baseActors.at(actorIndex)
                                                                     .spellsActiveChanges.activeSpells.at(spellIndex)
                                                                     .params.mEffects.at(effectIndex)
                                                                     .mEffectId);
    return effectId >= 0 ? static_cast<unsigned int>(effectId) : 0;
}

int ActorFunctions::GetActorSpellsActiveEffectArg(unsigned int actorIndex, unsigned int spellIndex, unsigned int effectIndex) noexcept
{
    return Utils::getLegacyIndexFromActiveEffectArg(readActorList->baseActors.at(actorIndex)
                                                        .spellsActiveChanges.activeSpells.at(spellIndex)
                                                        .params.mEffects.at(effectIndex));
}

double ActorFunctions::GetActorSpellsActiveEffectMagnitude(unsigned int actorIndex, unsigned int spellIndex, unsigned int effectIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).params.mEffects.at(effectIndex).mMagnitude;
}

double ActorFunctions::GetActorSpellsActiveEffectDuration(unsigned int actorIndex, unsigned int spellIndex, unsigned int effectIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).params.mEffects.at(effectIndex).mDuration;
}

double ActorFunctions::GetActorSpellsActiveEffectTimeLeft(unsigned int actorIndex, unsigned int spellIndex, unsigned int effectIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).params.mEffects.at(effectIndex).mTimeLeft;
}

bool ActorFunctions::DoesActorSpellsActiveHavePlayerCaster(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).caster.isPlayer;
}

int ActorFunctions::GetActorSpellsActiveCasterPid(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    Player* caster = Players::getPlayer(readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).caster.guid);

    if (caster != nullptr)
        return caster->getId();

    return -1;
}

const char* ActorFunctions::GetActorSpellsActiveCasterRefId(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).caster.refId.c_str();
}

unsigned int ActorFunctions::GetActorSpellsActiveCasterRefNum(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).caster.refNum;
}

unsigned int ActorFunctions::GetActorSpellsActiveCasterMpNum(unsigned int actorIndex, unsigned int spellIndex) noexcept
{
    return readActorList->baseActors.at(actorIndex).spellsActiveChanges.activeSpells.at(spellIndex).caster.mpNum;
}

bool ActorFunctions::DoesActorHavePosition(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).hasPositionData;
}

bool ActorFunctions::DoesActorHaveStatsDynamic(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).hasStatsDynamicData;
}

bool ActorFunctions::DoesActorHaveAI(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).hasAiData;
}

bool ActorFunctions::DoesActorHaveAITarget(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).hasAiTarget;
}

bool ActorFunctions::DoesActorAITargetPlayer(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiTarget.isPlayer;
}

unsigned int ActorFunctions::GetActorAIAction(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiAction;
}

unsigned int ActorFunctions::GetActorAIDistance(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiDistance;
}

unsigned int ActorFunctions::GetActorAIDuration(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiDuration;
}

bool ActorFunctions::GetActorAIRepetition(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiShouldRepeat;
}

double ActorFunctions::GetActorAIPosX(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiCoordinates.pos[0];
}

double ActorFunctions::GetActorAIPosY(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiCoordinates.pos[1];
}

double ActorFunctions::GetActorAIPosZ(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiCoordinates.pos[2];
}

int ActorFunctions::GetActorAITargetPid(unsigned int index) noexcept
{
    Player* player = Players::getPlayer(readActorList->baseActors.at(index).aiTarget.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ActorFunctions::GetActorAITargetRefId(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiTarget.refId.c_str();
}

unsigned int ActorFunctions::GetActorAITargetRefNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiTarget.refNum;
}

unsigned int ActorFunctions::GetActorAITargetMpNum(unsigned int index) noexcept
{
    return readActorList->baseActors.at(index).aiTarget.mpNum;
}

void ActorFunctions::SetActorListCell(const char* cellDescription) noexcept
{
    writeActorList.cell = Utils::getCellFromDescription(cellDescription);
}

void ActorFunctions::SetActorListAction(unsigned char action) noexcept
{
    writeActorList.action = action;
}

void ActorFunctions::SetActorCell(const char* cellDescription) noexcept
{
    tempActor.cell = Utils::getCellFromDescription(cellDescription);
}

void ActorFunctions::SetActorRefId(const char* refId) noexcept
{
    tempActor.refId = refId;
}

void ActorFunctions::SetActorRefNum(int refNum) noexcept
{
    tempActor.refNum = refNum;
}

void ActorFunctions::SetActorMpNum(int mpNum) noexcept
{
    tempActor.mpNum = mpNum;
}

void ActorFunctions::SetActorPosition(double x, double y, double z) noexcept
{
    tempActor.position.pos[0] = static_cast<float>(x);
    tempActor.position.pos[1] = static_cast<float>(y);
    tempActor.position.pos[2] = static_cast<float>(z);
}

void ActorFunctions::SetActorRotation(double x, double y, double z) noexcept
{
    tempActor.position.rot[0] = static_cast<float>(x);
    tempActor.position.rot[1] = static_cast<float>(y);
    tempActor.position.rot[2] = static_cast<float>(z);
}

void ActorFunctions::SetActorHealthBase(double value) noexcept
{
    tempActor.creatureStats.mDynamic[0].mBase = static_cast<float>(value);
}

void ActorFunctions::SetActorHealthCurrent(double value) noexcept
{
    tempActor.creatureStats.mDynamic[0].mCurrent = static_cast<float>(value);
}

void ActorFunctions::SetActorHealthModified(double value) noexcept
{
    tempActor.creatureStats.mDynamic[0].mMod = static_cast<float>(value);
}

void ActorFunctions::SetActorMagickaBase(double value) noexcept
{
    tempActor.creatureStats.mDynamic[1].mBase = static_cast<float>(value);
}

void ActorFunctions::SetActorMagickaCurrent(double value) noexcept
{
    tempActor.creatureStats.mDynamic[1].mCurrent = static_cast<float>(value);
}

void ActorFunctions::SetActorMagickaModified(double value) noexcept
{
    tempActor.creatureStats.mDynamic[1].mMod = static_cast<float>(value);
}

void ActorFunctions::SetActorFatigueBase(double value) noexcept
{
    tempActor.creatureStats.mDynamic[2].mBase = static_cast<float>(value);
}

void ActorFunctions::SetActorFatigueCurrent(double value) noexcept
{
    tempActor.creatureStats.mDynamic[2].mCurrent = static_cast<float>(value);
}

void ActorFunctions::SetActorFatigueModified(double value) noexcept
{
    tempActor.creatureStats.mDynamic[2].mMod = static_cast<float>(value);
}

void ActorFunctions::SetActorSound(const char* sound) noexcept
{
    tempActor.sound = sound;
}

void ActorFunctions::SetActorDeathState(unsigned int deathState) noexcept
{
    tempActor.deathState = static_cast<char>(deathState);
}

void ActorFunctions::SetActorDeathInstant(bool isInstant) noexcept
{
    tempActor.isInstantDeath = isInstant;
}

void ActorFunctions::SetActorSpellsActiveAction(unsigned char action) noexcept
{
    tempActor.spellsActiveChanges.action = action;
}

void ActorFunctions::SetActorAIAction(unsigned int action) noexcept
{
    tempActor.hasAiData = true;
    tempActor.aiAction = action;
}

void ActorFunctions::SetActorAITargetToPlayer(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempActor.hasAiTarget = true;
    tempActor.aiTarget.isPlayer = true;

    tempActor.aiTarget.guid = player->guid;
}

void ActorFunctions::SetActorAITargetToObject(int refNum, int mpNum) noexcept
{
    tempActor.hasAiTarget = true;
    tempActor.aiTarget.isPlayer = false;

    tempActor.aiTarget.refNum = refNum;
    tempActor.aiTarget.mpNum = mpNum;
}

void ActorFunctions::SetActorAICoordinates(double x, double y, double z) noexcept
{
    tempActor.aiCoordinates.pos[0] = static_cast<float>(x);
    tempActor.aiCoordinates.pos[1] = static_cast<float>(y);
    tempActor.aiCoordinates.pos[2] = static_cast<float>(z);
}

void ActorFunctions::SetActorAIDistance(unsigned int distance) noexcept
{
    tempActor.aiDistance = distance;
}

void ActorFunctions::SetActorAIDuration(unsigned int duration) noexcept
{
    tempActor.aiDuration = duration;
}

void ActorFunctions::SetActorAIRepetition(bool shouldRepeat) noexcept
{
    tempActor.aiShouldRepeat = shouldRepeat;
}

void ActorFunctions::EquipActorItem(unsigned short slot, const char *refId, unsigned int count, int charge, double enchantmentCharge) noexcept
{
    tempActor.equipmentItems[slot].refId = refId;
    tempActor.equipmentItems[slot].count = count;
    tempActor.equipmentItems[slot].charge = charge;
    tempActor.equipmentItems[slot].enchantmentCharge = static_cast<float>(enchantmentCharge);
}

void ActorFunctions::UnequipActorItem(unsigned short slot) noexcept
{
    ActorFunctions::EquipActorItem(slot, "", 0, -1, -1);
}

void ActorFunctions::AddActorSpellActive(const char* spellId, const char* displayName, bool stackingState) noexcept
{
    mwmp::ActiveSpell spell;
    spell.id = spellId;
    spell.isStackingSpell = stackingState;
    spell.params.mDisplayName = displayName;
    spell.params.mEffects = storedActorActiveEffects;

    tempActor.spellsActiveChanges.activeSpells.push_back(spell);

    storedActorActiveEffects.clear();
}

void ActorFunctions::AddActorSpellActiveEffect(int effectId, double magnitude, double duration, double timeLeft, int arg) noexcept
{
    ESM::ActiveEffect effect;
    effect.mEffectId = Utils::getActiveEffectIdFromLegacyIndex(effectId);
    effect.mMagnitude = static_cast<float>(magnitude);
    effect.mMinMagnitude = effect.mMagnitude;
    effect.mMaxMagnitude = effect.mMagnitude;
    effect.mDuration = static_cast<float>(duration);
    effect.mTimeLeft = static_cast<float>(timeLeft);
    effect.mEffectIndex = -1;
    effect.mFlags = ESM::ActiveEffect::Flag_None;
    effect.mArg = Utils::getActiveEffectArgFromLegacyIndex(effect.mEffectId, arg);

    storedActorActiveEffects.push_back(effect);
}

void ActorFunctions::AddActor() noexcept
{
    writeActorList.baseActors.push_back(tempActor);
    syncActorListCount(writeActorList);

    tempActor = emptyActor;
}

void ActorFunctions::SendActorList() noexcept
{
    syncActorListCount(writeActorList);

    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);
    if (serverCell != nullptr)
    {
        if (writeActorList.action == mwmp::BaseActorList::REQUEST)
            serverCell->requestActorListFrom(writeActorList.guid);
        else
            serverCell->readActorList(ID_ACTOR_LIST, &writeActorList);
    }

    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_LIST);
    actorPacket->setActorList(&writeActorList);
    actorPacket->Send(writeActorList.guid);
}

void ActorFunctions::SendActorAuthority() noexcept
{
    syncActorListCount(writeActorList);
    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);

    if (serverCell != nullptr)
    {
        if (!canAssignActorAuthority(serverCell, writeActorList.guid))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Refused actor authority for cell %s from player guid %s "
                "because that player does not have the cell loaded",
                serverCell->getShortDescription().c_str(), mwmp::packetGuidToString(writeActorList.guid).c_str());
            return;
        }

        serverCell->setAuthority(writeActorList.guid);

        mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_AUTHORITY);
        actorPacket->setActorList(&writeActorList);

        // Always send the packet to everyone on the server, to reduce bugs caused by late-arriving packets
        actorPacket->Send(false);
        actorPacket->Send(true);
    }
}

void ActorFunctions::SendActorPosition(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);
    advanceActorPositionSequences(serverCell, writeActorList);
    if (serverCell != nullptr)
        serverCell->readActorList(ID_ACTOR_POSITION, &writeActorList);

    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_POSITION);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorStatsDynamic(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);
    advanceActorStatsDynamicSequences(serverCell, writeActorList);
    if (serverCell != nullptr)
        serverCell->readActorList(ID_ACTOR_STATS_DYNAMIC, &writeActorList);

    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_STATS_DYNAMIC);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorEquipment(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);
    advanceActorEquipmentSequences(serverCell, writeActorList);
    if (serverCell != nullptr)
        serverCell->readActorList(ID_ACTOR_EQUIPMENT, &writeActorList);

    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_EQUIPMENT);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorSpellsActiveChanges(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    mwmp::ActorPacket* actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_SPELLS_ACTIVE);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        Cell* serverCell = CellController::get()->getCell(&writeActorList.cell);

        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorSpeech(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_SPEECH);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);

        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorDeath(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_DEATH);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);

        if (serverCell != nullptr)
        {
            serverCell->sendToLoaded(actorPacket, &writeActorList);
        }
    }
}

void ActorFunctions::SendActorAI(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_AI);
    actorPacket->setActorList(&writeActorList);

    Cell *serverCell = CellController::get()->getCell(&writeActorList.cell);
    if (serverCell != nullptr)
        serverCell->readActorList(ID_ACTOR_AI, &writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors && serverCell != nullptr)
    {
        serverCell->sendToLoaded(actorPacket, &writeActorList);
    }
}

void ActorFunctions::SendActorCellChange(bool sendToOtherVisitors, bool skipAttachedPlayer) noexcept
{
    syncActorListCount(writeActorList);
    mwmp::ActorProcessor::cacheCellChange(writeActorList);

    mwmp::ActorPacket *actorPacket = mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_CELL_CHANGE);
    actorPacket->setActorList(&writeActorList);

    if (!skipAttachedPlayer)
        actorPacket->Send(writeActorList.guid);

    if (sendToOtherVisitors)
    {
        mwmp::ActorProcessor::sendCellChangeToLoaded(*actorPacket, writeActorList);
    }
}


// All methods below are deprecated versions of methods from above

void ActorFunctions::ReadLastActorList() noexcept
{
    ReadReceivedActorList();
}

void ActorFunctions::InitializeActorList(unsigned short pid) noexcept
{
    ClearActorList();
    SetActorListPid(pid);
}

void ActorFunctions::CopyLastActorListToStore() noexcept
{
    CopyReceivedActorListToStore();
}

unsigned int ActorFunctions::GetActorRefNumIndex(unsigned int index) noexcept
{
    return GetActorRefNum(index);
}

unsigned int ActorFunctions::GetActorKillerRefNumIndex(unsigned int index) noexcept
{
    return GetActorKillerRefNum(index);
}

void ActorFunctions::SetActorRefNumIndex(int refNum) noexcept
{
    tempActor.refNum = refNum;
}
