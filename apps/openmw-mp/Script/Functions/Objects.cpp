#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include <apps/openmw-mp/ServerNetworking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Utils.hpp>
#include <apps/openmw-mp/Cell.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <apps/openmw-mp/processors/ObjectProcessor.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

#include "Objects.hpp"

using namespace mwmp;

BaseObjectList *readObjectList;
BaseObjectList writeObjectList;

BaseObject tempObject;
const BaseObject emptyObject = {};
static std::string tempDoorDestinationCellDescription;

ContainerItem tempContainerItem;
const ContainerItem emptyContainerItem = {};

namespace
{
    const char* emptyString() noexcept
    {
        return "";
    }

    const BaseObjectList* getReadObjectList(const char* functionName) noexcept
    {
        if (readObjectList == nullptr)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Ignoring %s without a read object list", functionName);
            return nullptr;
        }

        return readObjectList;
    }

    const BaseObject* getReadObject(unsigned int index, const char* functionName) noexcept
    {
        const BaseObjectList* objectList = getReadObjectList(functionName);
        if (objectList == nullptr)
            return nullptr;

        if (index >= objectList->baseObjects.size())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Ignoring %s with invalid object index %u (object count %u)", functionName, index,
                static_cast<unsigned int>(objectList->baseObjects.size()));
            return nullptr;
        }

        return &objectList->baseObjects[index];
    }

    const ClientVariable* getReadClientLocal(
        unsigned int objectIndex, unsigned int variableIndex, const char* functionName) noexcept
    {
        const BaseObject* object = getReadObject(objectIndex, functionName);
        if (object == nullptr)
            return nullptr;

        if (variableIndex >= object->clientLocals.size())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Ignoring %s with invalid client local index %u for object index %u (client local count %u)",
                functionName, variableIndex, objectIndex, static_cast<unsigned int>(object->clientLocals.size()));
            return nullptr;
        }

        return &object->clientLocals[variableIndex];
    }

    const ContainerItem* getReadContainerItem(
        unsigned int objectIndex, unsigned int itemIndex, const char* functionName) noexcept
    {
        const BaseObject* object = getReadObject(objectIndex, functionName);
        if (object == nullptr)
            return nullptr;

        if (itemIndex >= object->containerItems.size())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Ignoring %s with invalid container item index %u for object index %u (container item count %u)",
                functionName, itemIndex, objectIndex, static_cast<unsigned int>(object->containerItems.size()));
            return nullptr;
        }

        return &object->containerItems[itemIndex];
    }

    void syncObjectListCounts(BaseObjectList& objectList) noexcept
    {
        objectList.baseObjectCount = static_cast<unsigned int>(objectList.baseObjects.size());

        for (BaseObject& object : objectList.baseObjects)
            object.containerItemCount = static_cast<unsigned int>(object.containerItems.size());
    }

    bool shouldCacheServerObjectSend(mwmp::PacketId packetId, bool sendToOtherPlayers)
    {
        switch (packetId)
        {
            case ID_DOOR_DESTINATION:
            case ID_DOOR_STATE:
            case ID_OBJECT_DELETE:
            case ID_OBJECT_LOCK:
            case ID_OBJECT_MISCELLANEOUS:
            case ID_OBJECT_MOVE:
            case ID_OBJECT_PLACE:
            case ID_OBJECT_ROTATE:
            case ID_OBJECT_SCALE:
            case ID_OBJECT_SPAWN:
            case ID_OBJECT_STATE:
            case ID_OBJECT_TRAP:
                return true;
            case ID_CONTAINER:
                // Private/player-scoped container packets are often sent only to the attached
                // player. Cache only shared container results that the server broadcasts.
                return sendToOtherPlayers;
            default:
                return false;
        }
    }

    void updateServerObjectCacheFromSend(mwmp::PacketId packetId, BaseObjectList& objectList, bool sendToOtherPlayers)
    {
        if (!shouldCacheServerObjectSend(packetId, sendToOtherPlayers))
            return;

        mwmp::ObjectPacket* packet = mwmp::ServerNetworking::get().getObjectPacketController()->GetPacket(packetId);
        if (packet == nullptr || !packet->carriesCellData())
            return;

        if (Cell* serverCell = CellController::get()->getCell(&objectList.cell))
            serverCell->readObjectList(static_cast<unsigned char>(packetId), &objectList);
    }

    void SendObjectPacket(mwmp::PacketId packetId, bool sendToOtherPlayers, bool skipAttachedPlayer)
    {
        mwmp::ObjectPacket* packet = mwmp::ServerNetworking::get().getObjectPacketController()->GetPacket(packetId);
        syncObjectListCounts(writeObjectList);
        packet->setObjectList(&writeObjectList);
        updateServerObjectCacheFromSend(packetId, writeObjectList, sendToOtherPlayers);

        if (!skipAttachedPlayer)
            packet->Send(false);
        if (sendToOtherPlayers)
            mwmp::ObjectProcessor::sendToLoadedOrBroadcast(*packet, writeObjectList);
    }
}

void ObjectFunctions::ReadReceivedObjectList() noexcept
{
    readObjectList = mwmp::ServerNetworking::getPtr()->getReceivedObjectList();
}

void ObjectFunctions::ClearObjectList() noexcept
{
    writeObjectList.cell.blank();
    writeObjectList.baseObjects.clear();
    writeObjectList.baseObjectCount = 0;
    writeObjectList.consoleCommand.clear();
    writeObjectList.packetOrigin = mwmp::PACKET_ORIGIN::SERVER_SCRIPT;
    writeObjectList.originClientScript.clear();
    writeObjectList.action = mwmp::BaseObjectList::SET;
    writeObjectList.containerSubAction = mwmp::BaseObjectList::NONE;
    writeObjectList.isValid = true;
    tempObject = emptyObject;
    tempContainerItem = emptyContainerItem;
}

void ObjectFunctions::SetObjectListPid(unsigned short pid) noexcept
{
    Player* player;
    GET_PLAYER(pid, player, );

    writeObjectList.guid = player->guid;
}

void ObjectFunctions::CopyReceivedObjectListToStore() noexcept
{
    if (readObjectList == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Ignoring CopyReceivedObjectListToStore without a read object list");
        ClearObjectList();
        return;
    }

    writeObjectList = *readObjectList;
    syncObjectListCounts(writeObjectList);
}

unsigned int ObjectFunctions::GetObjectListSize() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? static_cast<unsigned int>(objectList->baseObjects.size()) : 0;
}

unsigned char ObjectFunctions::GetObjectListOrigin() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? objectList->packetOrigin : mwmp::PACKET_ORIGIN::SERVER_SCRIPT;
}

const char *ObjectFunctions::GetObjectListClientScript() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? objectList->originClientScript.c_str() : emptyString();
}

unsigned char ObjectFunctions::GetObjectListAction() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? objectList->action : mwmp::BaseObjectList::SET;
}

const char *ObjectFunctions::GetObjectListConsoleCommand() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? objectList->consoleCommand.c_str() : emptyString();
}

unsigned char ObjectFunctions::GetObjectListContainerSubAction() noexcept
{
    const BaseObjectList* objectList = getReadObjectList(__func__);
    return objectList != nullptr ? objectList->containerSubAction : mwmp::BaseObjectList::NONE;
}

bool ObjectFunctions::IsObjectPlayer(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->isPlayer;
}

int ObjectFunctions::GetObjectPid(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    if (object == nullptr)
        return -1;

    Player *player = Players::getPlayer(object->guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectRefId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->refId.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetObjectRefNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->refNum : 0;
}

unsigned int ObjectFunctions::GetObjectMpNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->mpNum : 0;
}

int ObjectFunctions::GetObjectCount(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->count : 0;
}

int ObjectFunctions::GetObjectCharge(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->charge : -1;
}

double ObjectFunctions::GetObjectEnchantmentCharge(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->enchantmentCharge : -1;
}

const char *ObjectFunctions::GetObjectSoul(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->soul.c_str() : emptyString();
}

int ObjectFunctions::GetObjectGoldValue(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->goldValue : 0;
}

double ObjectFunctions::GetObjectScale(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->scale : 0;
}

const char *ObjectFunctions::GetObjectSoundId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->soundId.c_str() : emptyString();
}

bool ObjectFunctions::GetObjectState(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->objectState;
}

int ObjectFunctions::GetObjectDoorState(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->doorState : 0;
}

bool ObjectFunctions::GetObjectDoorTeleportState(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->teleportState;
}

const char* ObjectFunctions::GetObjectDoorDestinationCell(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    if (object == nullptr)
        return emptyString();

    tempDoorDestinationCellDescription = object->destinationCell.getDescription();
    return tempDoorDestinationCellDescription.c_str();
}

double ObjectFunctions::GetObjectDoorDestinationPosX(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->destinationPosition.pos[0] : 0;
}

double ObjectFunctions::GetObjectDoorDestinationPosY(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->destinationPosition.pos[1] : 0;
}

double ObjectFunctions::GetObjectDoorDestinationPosZ(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->destinationPosition.pos[2] : 0;
}

double ObjectFunctions::GetObjectDoorDestinationRotX(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->destinationPosition.rot[0] : 0;
}

double ObjectFunctions::GetObjectDoorDestinationRotZ(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->destinationPosition.rot[2] : 0;
}

int ObjectFunctions::GetObjectLockLevel(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->lockLevel : 0;
}

unsigned int ObjectFunctions::GetObjectDialogueChoiceType(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->dialogueChoiceType : 0;
}

const char* ObjectFunctions::GetObjectDialogueChoiceTopic(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->topicId.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetObjectGoldPool(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->goldPool : 0;
}

double ObjectFunctions::GetObjectLastGoldRestockHour(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->lastGoldRestockHour : 0;
}

int ObjectFunctions::GetObjectLastGoldRestockDay(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->lastGoldRestockDay : 0;
}

bool ObjectFunctions::DoesObjectHavePlayerActivating(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->activatingActor.isPlayer;
}

int ObjectFunctions::GetObjectActivatingPid(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    if (object == nullptr)
        return -1;

    Player *player = Players::getPlayer(object->activatingActor.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectActivatingRefId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->activatingActor.refId.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetObjectActivatingRefNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->activatingActor.refNum : 0;
}

unsigned int ObjectFunctions::GetObjectActivatingMpNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->activatingActor.mpNum : 0;
}

const char *ObjectFunctions::GetObjectActivatingName(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->activatingActor.name.c_str() : emptyString();
}

bool ObjectFunctions::GetObjectHitSuccess(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->hitAttack.success;
}

double ObjectFunctions::GetObjectHitDamage(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->hitAttack.damage : 0;
}

bool ObjectFunctions::GetObjectHitBlock(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->hitAttack.block;
}

bool ObjectFunctions::GetObjectHitKnockdown(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->hitAttack.knockdown;
}

bool ObjectFunctions::DoesObjectHavePlayerHitting(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->hittingActor.isPlayer;
}

int ObjectFunctions::GetObjectHittingPid(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    if (object == nullptr)
        return -1;

    Player *player = Players::getPlayer(object->hittingActor.guid);

    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectHittingRefId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->hittingActor.refId.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetObjectHittingRefNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->hittingActor.refNum : 0;
}

unsigned int ObjectFunctions::GetObjectHittingMpNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->hittingActor.mpNum : 0;
}

const char *ObjectFunctions::GetObjectHittingName(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->hittingActor.name.c_str() : emptyString();
}

bool ObjectFunctions::GetObjectSummonState(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->isSummon;
}

double ObjectFunctions::GetObjectSummonDuration(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->summonDuration : 0;
}

double ObjectFunctions::GetObjectSummonEffectId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->summonEffectId : 0;
}

const char *ObjectFunctions::GetObjectSummonSpellId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->summonSpellId.c_str() : emptyString();
}

bool ObjectFunctions::DoesObjectHavePlayerSummoner(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->master.isPlayer;
}

int ObjectFunctions::GetObjectSummonerPid(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    if (object == nullptr)
        return -1;

    Player *player = Players::getPlayer(object->master.guid);
    
    if (player != nullptr)
        return player->getId();

    return -1;
}

const char *ObjectFunctions::GetObjectSummonerRefId(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->master.refId.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetObjectSummonerRefNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->master.refNum : 0;
}

unsigned int ObjectFunctions::GetObjectSummonerMpNum(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->master.mpNum : 0;
}

double ObjectFunctions::GetObjectPosX(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.pos[0] : 0;
}

double ObjectFunctions::GetObjectPosY(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.pos[1] : 0;
}

double ObjectFunctions::GetObjectPosZ(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.pos[2] : 0;
}

double ObjectFunctions::GetObjectRotX(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.rot[0] : 0;
}

double ObjectFunctions::GetObjectRotY(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.rot[1] : 0;
}

double ObjectFunctions::GetObjectRotZ(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->position.rot[2] : 0;
}

const char *ObjectFunctions::GetVideoFilename(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr ? object->videoFilename.c_str() : emptyString();
}

unsigned int ObjectFunctions::GetClientLocalsSize(unsigned int objectIndex) noexcept
{
    const BaseObject* object = getReadObject(objectIndex, __func__);
    return object != nullptr ? static_cast<unsigned int>(object->clientLocals.size()) : 0;
}

unsigned int ObjectFunctions::GetClientLocalInternalIndex(unsigned int objectIndex, unsigned int variableIndex) noexcept
{
    const ClientVariable* clientLocal = getReadClientLocal(objectIndex, variableIndex, __func__);
    return clientLocal != nullptr ? clientLocal->internalIndex : 0;
}

unsigned short ObjectFunctions::GetClientLocalVariableType(unsigned int objectIndex, unsigned int variableIndex) noexcept
{
    const ClientVariable* clientLocal = getReadClientLocal(objectIndex, variableIndex, __func__);
    return clientLocal != nullptr ? clientLocal->variableType : 0;
}

int ObjectFunctions::GetClientLocalIntValue(unsigned int objectIndex, unsigned int variableIndex) noexcept
{
    const ClientVariable* clientLocal = getReadClientLocal(objectIndex, variableIndex, __func__);
    return clientLocal != nullptr ? clientLocal->intValue : 0;
}

double ObjectFunctions::GetClientLocalFloatValue(unsigned int objectIndex, unsigned int variableIndex) noexcept
{
    const ClientVariable* clientLocal = getReadClientLocal(objectIndex, variableIndex, __func__);
    return clientLocal != nullptr ? clientLocal->floatValue : 0;
}

unsigned int ObjectFunctions::GetContainerChangesSize(unsigned int objectIndex) noexcept
{
    const BaseObject* object = getReadObject(objectIndex, __func__);
    return object != nullptr ? static_cast<unsigned int>(object->containerItems.size()) : 0;
}

const char *ObjectFunctions::GetContainerItemRefId(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->refId.c_str() : emptyString();
}

int ObjectFunctions::GetContainerItemCount(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->count : 0;
}

int ObjectFunctions::GetContainerItemCharge(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->charge : -1;
}

double ObjectFunctions::GetContainerItemEnchantmentCharge(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->enchantmentCharge : -1;
}

const char *ObjectFunctions::GetContainerItemSoul(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->soul.c_str() : emptyString();
}

int ObjectFunctions::GetContainerItemActionCount(unsigned int objectIndex, unsigned int itemIndex) noexcept
{
    const ContainerItem* item = getReadContainerItem(objectIndex, itemIndex, __func__);
    return item != nullptr ? item->actionCount : 0;
}

bool ObjectFunctions::DoesObjectHaveContainer(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->hasContainer;
}

bool ObjectFunctions::IsObjectDroppedByPlayer(unsigned int index) noexcept
{
    const BaseObject* object = getReadObject(index, __func__);
    return object != nullptr && object->droppedByPlayer;
}

void ObjectFunctions::SetObjectListCell(const char* cellDescription) noexcept
{
    writeObjectList.cell = Utils::getCellFromDescription(cellDescription);
}

void ObjectFunctions::SetObjectListAction(unsigned char action) noexcept
{
    writeObjectList.action = action;
}

void ObjectFunctions::SetObjectListContainerSubAction(unsigned char containerSubAction) noexcept
{
    writeObjectList.containerSubAction = containerSubAction;
}

void ObjectFunctions::SetObjectListConsoleCommand(const char* consoleCommand) noexcept
{
    writeObjectList.consoleCommand = consoleCommand;
}

void ObjectFunctions::SetObjectRefId(const char* refId) noexcept
{
    tempObject.refId = refId;
}

void ObjectFunctions::SetObjectRefNum(int refNum) noexcept
{
    tempObject.refNum = refNum;
}

void ObjectFunctions::SetObjectMpNum(int mpNum) noexcept
{
    tempObject.mpNum = mpNum;
}

void ObjectFunctions::SetObjectCount(int count) noexcept
{
    tempObject.count = count;
}

void ObjectFunctions::SetObjectCharge(int charge) noexcept
{
    tempObject.charge = charge;
}

void ObjectFunctions::SetObjectEnchantmentCharge(double enchantmentCharge) noexcept
{
    tempObject.enchantmentCharge = enchantmentCharge;
}

void ObjectFunctions::SetObjectSoul(const char* soul) noexcept
{
    tempObject.soul = soul;
}

void ObjectFunctions::SetObjectGoldValue(int goldValue) noexcept
{
    tempObject.goldValue = goldValue;
}

void ObjectFunctions::SetObjectScale(double scale) noexcept
{
    tempObject.scale = static_cast<float>(scale);
}

void ObjectFunctions::SetObjectState(bool objectState) noexcept
{
    tempObject.objectState = objectState;
}

void ObjectFunctions::SetObjectLockLevel(int lockLevel) noexcept
{
    tempObject.lockLevel = lockLevel;
}

void ObjectFunctions::SetObjectDialogueChoiceType(unsigned int dialogueChoiceType) noexcept
{
    tempObject.dialogueChoiceType = static_cast<unsigned char>(dialogueChoiceType);
}

void ObjectFunctions::SetObjectDialogueChoiceTopic(const char* topic) noexcept
{
    tempObject.topicId = topic;
}

void ObjectFunctions::SetObjectGoldPool(unsigned int goldPool) noexcept
{
    tempObject.goldPool = goldPool;
}

void ObjectFunctions::SetObjectLastGoldRestockHour(double lastGoldRestockHour) noexcept
{
    tempObject.lastGoldRestockHour = static_cast<float>(lastGoldRestockHour);
}

void ObjectFunctions::SetObjectLastGoldRestockDay(int lastGoldRestockDay) noexcept
{
    tempObject.lastGoldRestockDay = lastGoldRestockDay;
}

void ObjectFunctions::SetObjectDisarmState(bool disarmState) noexcept
{
    tempObject.isDisarmed = disarmState;
}

void ObjectFunctions::SetObjectDroppedByPlayerState(bool droppedByPlayer) noexcept
{
    tempObject.droppedByPlayer = droppedByPlayer;
}

void ObjectFunctions::SetObjectPosition(double x, double y, double z) noexcept
{
    tempObject.position.pos[0] = static_cast<float>(x);
    tempObject.position.pos[1] = static_cast<float>(y);
    tempObject.position.pos[2] = static_cast<float>(z);
}

void ObjectFunctions::SetObjectRotation(double x, double y, double z) noexcept
{
    tempObject.position.rot[0] = static_cast<float>(x);
    tempObject.position.rot[1] = static_cast<float>(y);
    tempObject.position.rot[2] = static_cast<float>(z);
}

void ObjectFunctions::SetObjectSound(const char* soundId, double volume, double pitch) noexcept
{
    tempObject.soundId = soundId;
    tempObject.volume = static_cast<float>(volume);
    tempObject.pitch = static_cast<float>(pitch);
}

void ObjectFunctions::SetObjectSummonState(bool summonState) noexcept
{
    tempObject.isSummon = summonState;
}

void ObjectFunctions::SetObjectSummonEffectId(int summonEffectId) noexcept
{
    tempObject.summonEffectId = summonEffectId;
}

void ObjectFunctions::SetObjectSummonSpellId(const char* summonSpellId) noexcept
{
    tempObject.summonSpellId = summonSpellId;
}

void ObjectFunctions::SetObjectSummonDuration(double summonDuration) noexcept
{
    tempObject.summonDuration = static_cast<float>(summonDuration);
}

void ObjectFunctions::SetObjectSummonerPid(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.master.isPlayer = true;
    tempObject.master.guid = player->guid;
}

void ObjectFunctions::SetObjectSummonerRefNum(int refNum) noexcept
{
    tempObject.master.isPlayer = false;
    tempObject.master.refNum = refNum;
}

void ObjectFunctions::SetObjectSummonerMpNum(int mpNum) noexcept
{
    tempObject.master.isPlayer = false;
    tempObject.master.mpNum = mpNum;
}

void ObjectFunctions::SetObjectActivatingPid(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.activatingActor.isPlayer = true;
    tempObject.activatingActor.guid = player->guid;
}

void ObjectFunctions::SetObjectDoorState(int doorState) noexcept
{
    tempObject.doorState = doorState;
}

void ObjectFunctions::SetObjectDoorTeleportState(bool teleportState) noexcept
{
    tempObject.teleportState = teleportState;
}

void ObjectFunctions::SetObjectDoorDestinationCell(const char* cellDescription) noexcept
{
    tempObject.destinationCell = Utils::getCellFromDescription(cellDescription);
}

void ObjectFunctions::SetObjectDoorDestinationPosition(double x, double y, double z) noexcept
{
    tempObject.destinationPosition.pos[0] = static_cast<float>(x);
    tempObject.destinationPosition.pos[1] = static_cast<float>(y);
    tempObject.destinationPosition.pos[2] = static_cast<float>(z);
}

void ObjectFunctions::SetObjectDoorDestinationRotation(double x, double z) noexcept
{
    tempObject.destinationPosition.rot[0] = static_cast<float>(x);
    tempObject.destinationPosition.rot[2] = static_cast<float>(z);
}

void ObjectFunctions::SetPlayerAsObject(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    tempObject.guid = player->guid;
    tempObject.isPlayer = true;
}

void ObjectFunctions::SetContainerItemRefId(const char* refId) noexcept
{
    tempContainerItem.refId = refId;
}

void ObjectFunctions::SetContainerItemCount(int count) noexcept
{
    tempContainerItem.count = count;
}

void ObjectFunctions::SetContainerItemActionCount(int actionCount) noexcept
{
    tempContainerItem.actionCount = actionCount;
}

void ObjectFunctions::SetContainerItemCharge(int charge) noexcept
{
    tempContainerItem.charge = charge;
}

void ObjectFunctions::SetContainerItemEnchantmentCharge(double enchantmentCharge) noexcept
{
    tempContainerItem.enchantmentCharge = enchantmentCharge;
}

void ObjectFunctions::SetContainerItemSoul(const char* soul) noexcept
{
    tempContainerItem.soul = soul;
}

void ObjectFunctions::SetContainerItemActionCountByIndex(unsigned int objectIndex, unsigned int itemIndex, int actionCount) noexcept
{
    if (objectIndex >= writeObjectList.baseObjects.size())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Ignoring SetContainerItemActionCountByIndex with invalid object index %u (object count %zu)",
            objectIndex, writeObjectList.baseObjects.size());
        return;
    }

    auto& containerItems = writeObjectList.baseObjects[objectIndex].containerItems;
    if (itemIndex >= containerItems.size())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Ignoring SetContainerItemActionCountByIndex with invalid item index %u (item count %zu)",
            itemIndex, containerItems.size());
        return;
    }

    containerItems[itemIndex].actionCount = actionCount;
}

void ObjectFunctions::AddObject() noexcept
{
    tempObject.containerItemCount = static_cast<unsigned int>(tempObject.containerItems.size());
    writeObjectList.baseObjects.push_back(tempObject);
    writeObjectList.baseObjectCount = static_cast<unsigned int>(writeObjectList.baseObjects.size());

    tempObject = emptyObject;
}

void ObjectFunctions::AddClientLocalInteger(int internalIndex, int intValue, unsigned int variableType) noexcept
{
    ClientVariable clientLocal;
    clientLocal.internalIndex = internalIndex;
    clientLocal.intValue = intValue;
    clientLocal.variableType = static_cast<char>(variableType);

    tempObject.clientLocals.push_back(clientLocal);
}

void ObjectFunctions::AddClientLocalFloat(int internalIndex, double floatValue) noexcept
{
    ClientVariable clientLocal;
    clientLocal.internalIndex = internalIndex;
    clientLocal.floatValue = static_cast<float>(floatValue);
    clientLocal.variableType = mwmp::VARIABLE_TYPE::FLOAT;

    tempObject.clientLocals.push_back(clientLocal);
}

void ObjectFunctions::AddContainerItem() noexcept
{
    tempObject.containerItems.push_back(tempContainerItem);
    tempObject.containerItemCount = static_cast<unsigned int>(tempObject.containerItems.size());

    tempContainerItem = emptyContainerItem;
}

void ObjectFunctions::SendObjectActivate(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_ACTIVATE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectPlace(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_PLACE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectSpawn(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_SPAWN, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectDelete(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_DELETE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectLock(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_LOCK, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectDialogueChoice(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_DIALOGUE_CHOICE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectMiscellaneous(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_MISCELLANEOUS, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectRestock(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_RESTOCK, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectTrap(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_TRAP, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectScale(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_SCALE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectSound(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_SOUND, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectState(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_STATE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectMove(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_MOVE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendObjectRotate(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_OBJECT_ROTATE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendDoorState(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_DOOR_STATE, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendDoorDestination(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_DOOR_DESTINATION, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendContainer(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_CONTAINER, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendVideoPlay(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_VIDEO_PLAY, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendClientScriptLocal(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_CLIENT_SCRIPT_LOCAL, sendToOtherPlayers, skipAttachedPlayer);
}

void ObjectFunctions::SendConsoleCommand(bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    SendObjectPacket(ID_CONSOLE_COMMAND, sendToOtherPlayers, skipAttachedPlayer);
}


// All methods below are deprecated versions of methods from above

void ObjectFunctions::ReadLastObjectList() noexcept
{
    ReadReceivedObjectList();
}

void ObjectFunctions::ReadLastEvent() noexcept
{
    ReadReceivedObjectList();
}

void ObjectFunctions::InitializeObjectList(unsigned short pid) noexcept
{
    ClearObjectList();
    SetObjectListPid(pid);
}

void ObjectFunctions::InitializeEvent(unsigned short pid) noexcept
{
    InitializeObjectList(pid);
}

void ObjectFunctions::CopyLastObjectListToStore() noexcept
{
    CopyReceivedObjectListToStore();
}

unsigned int ObjectFunctions::GetObjectChangesSize() noexcept
{
    return GetObjectListSize();
}

unsigned char ObjectFunctions::GetEventAction() noexcept
{
    return GetObjectListAction();
}

unsigned char ObjectFunctions::GetEventContainerSubAction() noexcept
{
    return GetObjectListContainerSubAction();
}

unsigned int ObjectFunctions::GetObjectRefNumIndex(unsigned int index) noexcept
{
    return GetObjectRefNum(index);
}

unsigned int ObjectFunctions::GetObjectSummonerRefNumIndex(unsigned int index) noexcept
{
    return GetObjectSummonerRefNum(index);
}

void ObjectFunctions::SetEventCell(const char* cellDescription) noexcept
{
    SetObjectListCell(cellDescription);
}

void ObjectFunctions::SetEventAction(unsigned char action) noexcept
{
    SetObjectListAction(action);
}

void ObjectFunctions::SetEventConsoleCommand(const char* consoleCommand) noexcept
{
    SetObjectListConsoleCommand(consoleCommand);
}

void ObjectFunctions::SetObjectRefNumIndex(int refNum) noexcept
{
    SetObjectRefNum(refNum);
}

void ObjectFunctions::AddWorldObject() noexcept
{
    AddObject();
}
