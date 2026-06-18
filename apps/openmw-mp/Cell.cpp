#include "Cell.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string_view>
#include <utility>
#include "Player.hpp"
#include "ServerEventDispatcher.hpp"
#include "ServerNetworking.hpp"
#include "Utils.hpp"
#include "WorldDatabaseStore.hpp"

namespace
{
    bool isFiniteActorPosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2])
            && std::isfinite(position.rot[0]) && std::isfinite(position.rot[1]) && std::isfinite(position.rot[2]);
    }

    bool isFiniteActorMovementSnapshot(const mwmp::BaseActor& actor)
    {
        return isFiniteActorPosition(actor.position) && isFiniteActorPosition(actor.direction);
    }

    float estimateOneWayLatencySeconds(mwmp::PacketGuid guid)
    {
        // Keep actor snapshots conservative while actors are still simulated
        // by the cell authority client. Server-injected route latency made
        // remote actor movement overshoot and rubber-band.
        static_cast<void>(guid);
        return 0.f;
    }

    float estimateRouteLatencySeconds(mwmp::PacketGuid sourceGuid, mwmp::PacketGuid destinationGuid)
    {
        return mwmp::sanitizeMovementLatencySeconds(
            estimateOneWayLatencySeconds(sourceGuid) + estimateOneWayLatencySeconds(destinationGuid));
    }

    std::vector<float> captureMovementLatencies(const mwmp::BaseActorList& actorList)
    {
        std::vector<float> latencies;
        latencies.reserve(actorList.baseActors.size());
        for (const mwmp::BaseActor& actor : actorList.baseActors)
            latencies.push_back(actor.movementLatencySeconds);
        return latencies;
    }

    void setMovementLatencies(mwmp::BaseActorList& actorList, float latencySeconds)
    {
        const float sanitizedLatencySeconds = mwmp::sanitizeMovementLatencySeconds(latencySeconds);
        for (mwmp::BaseActor& actor : actorList.baseActors)
            actor.movementLatencySeconds = sanitizedLatencySeconds;
    }

    void restoreMovementLatencies(mwmp::BaseActorList& actorList, const std::vector<float>& latencies)
    {
        const std::size_t count = std::min(actorList.baseActors.size(), latencies.size());
        for (std::size_t i = 0; i < count; ++i)
            actorList.baseActors[i].movementLatencySeconds = latencies[i];
    }

    std::string normalizedWorldLookupKey(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char c : value)
        {
            if (c == '\\')
                result.push_back('/');
            else
                result.push_back(static_cast<char>(std::tolower(c)));
        }
        return result;
    }

    std::string worldDatabaseCellKeyForCell(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return "exterior:" + std::to_string(cell.mData.mX) + "," + std::to_string(cell.mData.mY);

        return "interior:" + normalizedWorldLookupKey(cell.mName);
    }

    ESM::Position makePosition(float posX, float posY, float posZ, float rotX, float rotY, float rotZ)
    {
        ESM::Position position;
        position.pos[0] = posX;
        position.pos[1] = posY;
        position.pos[2] = posZ;
        position.rot[0] = rotX;
        position.rot[1] = rotY;
        position.rot[2] = rotZ;
        return position;
    }

    bool isActorBootstrapCategory(std::string_view category)
    {
        return category == "actor" || category == "levelledActor";
    }

    bool isObjectBootstrapCategory(std::string_view category)
    {
        return category == "container" || category == "door" || category == "item"
            || category == "levelledItem" || category == "static" || category == "activator";
    }

    std::pair<unsigned int, unsigned int> objectKey(const mwmp::BaseObject& object)
    {
        return { object.refNum, object.mpNum };
    }

    const mwmp::BaseObject* findObject(const mwmp::BaseObjectList& objectList, unsigned int refNum, unsigned int mpNum)
    {
        for (const mwmp::BaseObject& object : objectList.baseObjects)
        {
            if (object.refNum == refNum && object.mpNum == mpNum)
                return &object;
        }

        return nullptr;
    }

    mwmp::BaseActor buildServerWorldActor(const Cell::ServerWorldReference& reference, const ESM::Cell& cell)
    {
        mwmp::BaseActor actor;
        actor.refId = reference.refId;
        actor.refNum = reference.refNum;
        actor.mpNum = reference.mpNum;
        actor.position = reference.position;
        actor.direction = {};
        actor.cell = cell;
        actor.hasPositionData = true;
        actor.positionSequence = 1;
        actor.movementSampleIntervalSeconds = 1.f / 60.f;
        actor.movementLatencySeconds = 0.f;
        if (reference.baseActorAiAvailable)
        {
            actor.hasAiData = true;
            actor.aiAction = reference.baseActorAiAction;
            actor.aiDistance = reference.baseActorAiDistance;
            actor.aiDuration = reference.baseActorAiDuration;
            actor.aiShouldRepeat = reference.baseActorAiShouldRepeat;
            actor.aiCoordinates = reference.baseActorAiCoordinates;
        }
        return actor;
    }

    mwmp::BaseObject buildServerWorldObject(const Cell::ServerWorldReference& reference)
    {
        mwmp::BaseObject object{};
        object.refId = reference.refId;
        object.refNum = reference.refNum;
        object.mpNum = reference.mpNum;
        object.count = reference.count;
        object.charge = -1;
        object.enchantmentCharge = -1.0;
        object.position = reference.position;
        object.objectState = true;
        object.lockLevel = reference.locked ? reference.lockLevel : 0;
        object.scale = reference.scale;
        object.doorState = 0;
        object.teleportState = reference.teleport;
        object.destinationPosition = reference.destinationPosition;
        object.hasContainer = reference.baseRecordCategory == "container";
        object.containerItemCount = 0;

        if (!reference.destinationCell.empty())
            object.destinationCell = Utils::getCellFromDescription(reference.destinationCell);

        return object;
    }

    bool sameContainerStack(const mwmp::ContainerItem& left, const mwmp::ContainerItem& right)
    {
        return left.refId == right.refId
            && left.charge == right.charge
            && left.enchantmentCharge == right.enchantmentCharge
            && left.soul == right.soul;
    }

    void addContainerItem(std::vector<mwmp::ContainerItem>& items, const mwmp::ContainerItem& incoming)
    {
        for (mwmp::ContainerItem& item : items)
        {
            if (!sameContainerStack(item, incoming))
                continue;

            item.count += incoming.count;
            item.actionCount = incoming.actionCount;
            return;
        }

        items.push_back(incoming);
    }

    void removeContainerItem(std::vector<mwmp::ContainerItem>& items, const mwmp::ContainerItem& incoming)
    {
        const int removeCount = incoming.actionCount > 0 ? incoming.actionCount : incoming.count;
        if (removeCount <= 0)
            return;

        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (!sameContainerStack(*it, incoming))
                continue;

            it->count -= removeCount;
            it->actionCount = incoming.actionCount;
            if (it->count <= 0)
                items.erase(it);
            return;
        }
    }

    void mergeContainerItems(mwmp::BaseObject& target, const mwmp::BaseObject& incoming, unsigned char action)
    {
        target.hasContainer = true;

        if (action == mwmp::BaseObjectList::SET)
            target.containerItems = incoming.containerItems;
        else if (action == mwmp::BaseObjectList::ADD)
        {
            for (const mwmp::ContainerItem& item : incoming.containerItems)
                addContainerItem(target.containerItems, item);
        }
        else if (action == mwmp::BaseObjectList::REMOVE)
        {
            for (const mwmp::ContainerItem& item : incoming.containerItems)
                removeContainerItem(target.containerItems, item);
        }

        target.containerItemCount = static_cast<unsigned int>(target.containerItems.size());
    }

    void mergeObjectPacketFields(mwmp::BaseObject& target, const mwmp::BaseObject& incoming,
        unsigned char packetID, unsigned char action)
    {
        switch (packetID)
        {
            case ID_OBJECT_PLACE:
            case ID_OBJECT_SPAWN:
                target = incoming;
                target.objectState = true;
                target.scale = 1.f;
                target.doorState = 0;
                target.teleportState = false;
                target.containerItemCount = static_cast<unsigned int>(target.containerItems.size());
                break;
            case ID_OBJECT_MOVE:
                target.position.pos[0] = incoming.position.pos[0];
                target.position.pos[1] = incoming.position.pos[1];
                target.position.pos[2] = incoming.position.pos[2];
                break;
            case ID_OBJECT_ROTATE:
                target.position.rot[0] = incoming.position.rot[0];
                target.position.rot[1] = incoming.position.rot[1];
                target.position.rot[2] = incoming.position.rot[2];
                break;
            case ID_OBJECT_SCALE:
                target.scale = incoming.scale;
                break;
            case ID_OBJECT_LOCK:
                target.lockLevel = incoming.lockLevel;
                break;
            case ID_OBJECT_STATE:
                target.objectState = incoming.objectState;
                break;
            case ID_DOOR_STATE:
                target.doorState = incoming.doorState;
                break;
            case ID_DOOR_DESTINATION:
                target.teleportState = incoming.teleportState;
                target.destinationCell = incoming.destinationCell;
                target.destinationPosition = incoming.destinationPosition;
                break;
            case ID_CONTAINER:
                mergeContainerItems(target, incoming, action);
                break;
            default:
                if (!incoming.refId.empty())
                    target.refId = incoming.refId;
                break;
        }
    }

    bool isDoorCategory(const std::vector<Cell::ServerWorldReference>& references, const mwmp::BaseObject& object)
    {
        for (const Cell::ServerWorldReference& reference : references)
        {
            if (reference.refNum == object.refNum && reference.mpNum == object.mpNum)
                return reference.baseRecordCategory == "door";
        }

        return false;
    }

}

Cell::Cell(ESM::Cell cell)
    : cell(cell)
    , authorityGuid(mwmp::unassignedPacketGuid())
    , actorListRequestGuid(mwmp::unassignedPacketGuid())
    , actorListSnapshotReceived(false)
    , objectListSnapshotReceived(false)
    , serverWorldActorListSeeded(false)
    , serverWorldObjectListSeeded(false)
    , serverWorldSeededActorCount(0)
    , serverWorldSeededObjectCount(0)
    , simulationInterest(false)
{
    cellActorList.count = 0;
    serverWorldActorList.count = 0;
    cellObjectList.baseObjectCount = 0;
    cellObjectList.guid = mwmp::unassignedPacketGuid();
    cellObjectList.cell = this->cell;
    cellObjectList.action = mwmp::BaseObjectList::SET;
    cellObjectList.isValid = true;
    serverWorldObjectList.baseObjectCount = 0;
    serverWorldObjectList.guid = mwmp::unassignedPacketGuid();
    serverWorldObjectList.cell = this->cell;
    serverWorldObjectList.action = mwmp::BaseObjectList::SET;
    serverWorldObjectList.isValid = true;
}

Cell::Iterator Cell::begin() const
{
    return players.begin();
}

Cell::Iterator Cell::end() const
{
    return players.end();
}

void Cell::addPlayer(Player *player)
{
    // Ensure the player hasn't already been added
    auto it = find(begin(), end(), player);

    if (it != end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Attempt to add %s to Cell %s again was ignored", player->npc.mName.c_str(), getShortDescription().c_str());
        return;
    }

    auto it2 = find(player->cells.begin(), player->cells.end(), this);
    if (it2 == player->cells.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Adding %s to Player %s", getShortDescription().c_str(), player->npc.mName.c_str());

        player->cells.push_back(this);
    }

    LOG_APPEND(TimedLog::LOG_INFO, "- Adding %s to Cell %s", player->npc.mName.c_str(), getShortDescription().c_str());

    mwmp::ServerEvents::cellLoad(player->getId(), getShortDescription().c_str());

    players.push_back(player);
    sendServerObjectStateSnapshotTo(*player);
}

void Cell::removePlayer(Player *player, bool cleanPlayer)
{
    for (Iterator it = begin(); it != end(); it++)
    {
        if (*it == player)
        {
            if (cleanPlayer)
            {
                auto it2 = find(player->cells.begin(), player->cells.end(), this);
                if (it2 != player->cells.end())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Removing %s from Player %s", getShortDescription().c_str(), player->npc.mName.c_str());

                    player->cells.erase(it2);
                }
            }

            LOG_APPEND(TimedLog::LOG_INFO, "- Removing %s from Cell %s", player->npc.mName.c_str(), getShortDescription().c_str());

            mwmp::ServerEvents::cellUnload(player->getId(), getShortDescription().c_str());

            if (hasPendingActorListRequestFrom(player->guid))
                actorListRequestGuid = mwmp::unassignedPacketGuid();

            players.erase(it);
            return;
        }
    }
}

void Cell::readActorList(unsigned char packetID, const mwmp::BaseActorList *newActorList)
{
    if (packetID == ID_ACTOR_LIST && newActorList->action == mwmp::BaseActorList::SET)
    {
        std::vector<mwmp::BaseActor> replacementActors;
        replacementActors.reserve(newActorList->baseActors.size());

        for (const mwmp::BaseActor& incomingActor : newActorList->baseActors)
        {
            mwmp::BaseActor actorToStore = incomingActor;
            actorToStore.cell = cell;

            if (mwmp::BaseActor* existingActor = getActor(actorToStore.refNum, actorToStore.mpNum))
            {
                mwmp::BaseActor mergedActor = *existingActor;
                mergedActor.refId = actorToStore.refId;
                mergedActor.cell = cell;
                replacementActors.push_back(std::move(mergedActor));
            }
            else
                replacementActors.push_back(std::move(actorToStore));
        }

        cellActorList.baseActors = std::move(replacementActors);
        cellActorList.count = static_cast<unsigned int>(cellActorList.baseActors.size());
        actorListSnapshotReceived = true;
        return;
    }

    for (const mwmp::BaseActor& incomingActor : newActorList->baseActors)
    {
        mwmp::BaseActor newActor = incomingActor;
        if (packetID == ID_ACTOR_AI)
            newActor.hasAiData = true;

        mwmp::BaseActor *cellActor;

        if (containsActor(newActor.refNum, newActor.mpNum))
        {
            cellActor = getActor(newActor.refNum, newActor.mpNum);

            switch (packetID)
            {
            case ID_ACTOR_LIST:

                cellActor->refId = newActor.refId;
                cellActor->cell = cell;
                break;

            case ID_ACTOR_POSITION:

                if (!isFiniteActorMovementSnapshot(newActor))
                    break;

                if (!cellActor->hasPositionData || mwmp::isNewerPositionSequence(newActor.positionSequence, cellActor->positionSequence))
                {
                    cellActor->hasPositionData = true;
                    cellActor->positionSequence = newActor.positionSequence;
                    cellActor->position = newActor.position;
                    cellActor->direction = newActor.direction;
                    cellActor->movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                        newActor.movementSampleIntervalSeconds);
                    cellActor->movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(newActor.movementLatencySeconds);
                }
                break;

            case ID_ACTOR_ANIM_FLAGS:

                {
                    mwmp::BaseActor actorToCache = newActor;
                    if (actorToCache.hasPositionData && !isFiniteActorMovementSnapshot(actorToCache))
                        actorToCache.hasPositionData = false;

                    mwmp::mergeNewestActorAnimFlags(*cellActor, actorToCache);
                }
                break;

            case ID_ACTOR_STATS_DYNAMIC:

                if (cellActor->hasStatsDynamicData
                    && !mwmp::isNewerActorStatsDynamicSequence(newActor.statsDynamicSequence, cellActor->statsDynamicSequence))
                    break;

                cellActor->hasStatsDynamicData = true;
                cellActor->statsDynamicSequence = newActor.statsDynamicSequence;
                cellActor->creatureStats.mDead = newActor.creatureStats.mDead;
                cellActor->creatureStats.mDeathAnimationFinished = newActor.creatureStats.mDeathAnimationFinished;
                cellActor->creatureStats.mDynamic[0] = newActor.creatureStats.mDynamic[0];
                cellActor->creatureStats.mDynamic[1] = newActor.creatureStats.mDynamic[1];
                cellActor->creatureStats.mDynamic[2] = newActor.creatureStats.mDynamic[2];
                break;

            case ID_ACTOR_EQUIPMENT:

                if (cellActor->hasEquipmentData
                    && !mwmp::isNewerActorEquipmentSequence(newActor.equipmentSequence, cellActor->equipmentSequence))
                    break;

                if (!mwmp::hasValidActorEquipment(newActor))
                    break;

                cellActor->hasEquipmentData = true;
                cellActor->equipmentSequence = newActor.equipmentSequence;

                for (int slot = 0; slot < mwmp::equipmentSlotCount; ++slot)
                    cellActor->equipmentItems[slot] = newActor.equipmentItems[slot];
                break;

            case ID_ACTOR_DEATH:

                if (newActor.hasPositionData && isFiniteActorMovementSnapshot(newActor)
                    && (!cellActor->hasPositionData
                    || mwmp::isNewerPositionSequence(newActor.positionSequence, cellActor->positionSequence)))
                {
                    cellActor->hasPositionData = true;
                    cellActor->positionSequence = newActor.positionSequence;
                    cellActor->position = newActor.position;
                    cellActor->direction = newActor.direction;
                    cellActor->movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                        newActor.movementSampleIntervalSeconds);
                    cellActor->movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(newActor.movementLatencySeconds);
                }

                cellActor->creatureStats.mDead = true;
                cellActor->creatureStats.mDynamic[0].mCurrent = 0.f;
                cellActor->deathState = newActor.deathState;
                cellActor->isInstantDeath = newActor.isInstantDeath;
                cellActor->killer = newActor.killer;
                break;

            case ID_ACTOR_AI:

                if (newActor.hasPositionData && isFiniteActorMovementSnapshot(newActor)
                    && (!cellActor->hasPositionData
                    || mwmp::isNewerPositionSequence(newActor.positionSequence, cellActor->positionSequence)))
                {
                    cellActor->hasPositionData = true;
                    cellActor->positionSequence = newActor.positionSequence;
                    cellActor->position = newActor.position;
                    cellActor->direction = newActor.direction;
                    cellActor->movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                        newActor.movementSampleIntervalSeconds);
                    cellActor->movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(newActor.movementLatencySeconds);
                }

                cellActor->hasAiData = true;
                cellActor->hasAiTarget = newActor.hasAiTarget;
                cellActor->aiTarget = newActor.aiTarget;
                cellActor->aiAction = newActor.aiAction;
                cellActor->aiDistance = newActor.aiDistance;
                cellActor->aiDuration = newActor.aiDuration;
                cellActor->aiShouldRepeat = newActor.aiShouldRepeat;
                cellActor->aiCoordinates = newActor.aiCoordinates;
                break;
            }
        }
        else if (packetID == ID_ACTOR_LIST && newActorList->action != mwmp::BaseActorList::REMOVE
            && newActorList->action != mwmp::BaseActorList::REQUEST)
        {
            newActor.cell = cell;
            cellActorList.baseActors.push_back(newActor);
        }
    }

    cellActorList.count = static_cast<unsigned int>(cellActorList.baseActors.size());
}

bool Cell::containsActor(int refNum, int mpNum)
{
    for (unsigned int i = 0; i < cellActorList.baseActors.size(); i++)
    {
        mwmp::BaseActor actor = cellActorList.baseActors.at(i);

        if (actor.refNum == static_cast<unsigned int>(refNum) && actor.mpNum == static_cast<unsigned int>(mpNum))
            return true;
    }
    return false;
}

mwmp::BaseActor *Cell::getActor(int refNum, int mpNum)
{
    for (unsigned int i = 0; i < cellActorList.baseActors.size(); i++)
    {
        mwmp::BaseActor *actor = &cellActorList.baseActors.at(i);

        if (actor->refNum == static_cast<unsigned int>(refNum) && actor->mpNum == static_cast<unsigned int>(mpNum))
            return actor;
    }
    return 0;
}

void Cell::upsertActors(const mwmp::BaseActorList *newActorList)
{
    for (const mwmp::BaseActor& incomingActor : newActorList->baseActors)
    {
        mwmp::BaseActor actorToStore = incomingActor;
        actorToStore.cell = cell;

        if (mwmp::BaseActor* cellActor = getActor(actorToStore.refNum, actorToStore.mpNum))
            *cellActor = actorToStore;
        else
            cellActorList.baseActors.push_back(actorToStore);
    }

    cellActorList.count = static_cast<unsigned int>(cellActorList.baseActors.size());
}

void Cell::removeActors(const mwmp::BaseActorList *newActorList)
{
    for (std::vector<mwmp::BaseActor>::iterator it = cellActorList.baseActors.begin(); it != cellActorList.baseActors.end();)
    {
        unsigned int refNum = (*it).refNum;
        unsigned int mpNum = (*it).mpNum;

        bool foundActor = false;

        for (const mwmp::BaseActor& newActor : newActorList->baseActors)
        {
            if (newActor.refNum == refNum && newActor.mpNum == mpNum)
            {
                it = cellActorList.baseActors.erase(it);
                foundActor = true;
                break;
            }
        }

        if (!foundActor)
            it++;
    }

    cellActorList.count = static_cast<unsigned int>(cellActorList.baseActors.size());
}

void Cell::requestActorListFrom(const mwmp::PacketGuid& guid)
{
    actorListRequestGuid = guid;
}

bool Cell::hasPendingActorListRequest() const
{
    return mwmp::isPacketGuidAssigned(actorListRequestGuid);
}

bool Cell::hasPendingActorListRequestFrom(const mwmp::PacketGuid& guid) const
{
    return mwmp::isPacketGuidAssigned(actorListRequestGuid) && actorListRequestGuid == guid;
}

bool Cell::consumePendingActorListRequestFrom(const mwmp::PacketGuid& guid)
{
    if (!hasPendingActorListRequestFrom(guid))
        return false;

    actorListRequestGuid = mwmp::unassignedPacketGuid();
    return true;
}

bool Cell::hasActorListSnapshot() const
{
    return actorListSnapshotReceived;
}

void Cell::ensureServerWorldStateBootstrapped()
{
    if (serverWorldBootstrapStats.attempted)
        return;

    serverWorldBootstrapStats = {};
    serverWorldBootstrapStats.attempted = true;
    serverWorldBootstrapStats.cellKey = worldDatabaseCellKeyForCell(cell);
    serverWorldReferences.clear();
    serverWorldActorList = {};
    serverWorldActorList.guid = mwmp::unassignedPacketGuid();
    serverWorldActorList.cell = cell;
    serverWorldActorList.action = mwmp::BaseActorList::SET;
    serverWorldActorList.isValid = true;
    serverWorldObjectList = {};
    serverWorldObjectList.guid = mwmp::unassignedPacketGuid();
    serverWorldObjectList.cell = cell;
    serverWorldObjectList.action = mwmp::BaseObjectList::SET;
    serverWorldObjectList.isValid = true;

    mwmp::WorldDatabaseStore::get().ensureLoaded();
    const mwmp::WorldDatabaseStatistics worldStats = mwmp::WorldDatabaseStore::get().statistics();
    if (!worldStats.loaded)
        return;

    std::vector<mwmp::WorldCellReferenceRecord> references
        = mwmp::WorldDatabaseStore::get().findReferencesByCellKey(serverWorldBootstrapStats.cellKey);
    serverWorldReferences.reserve(references.size());
    serverWorldActorList.baseActors.reserve(references.size());
    serverWorldObjectList.baseObjects.reserve(references.size());

    for (const mwmp::WorldCellReferenceRecord& ref : references)
    {
        if (ref.baseRecordDeleted)
        {
            ++serverWorldBootstrapStats.deletedBaseRecordCount;
            continue;
        }

        ServerWorldReference snapshot;
        snapshot.refKey = ref.refKey;
        snapshot.refId = ref.refId;
        snapshot.baseRecordType = ref.baseRecordType;
        snapshot.baseRecordCategory = ref.baseRecordCategory;
        snapshot.baseRecordSourceFile = ref.baseRecordSourceFile;
        snapshot.baseActorAiAvailable = ref.baseActorAiAvailable;
        snapshot.baseActorAiPackageCount = ref.baseActorAiPackageCount;
        snapshot.baseActorAiAction = ref.baseActorAiAction;
        snapshot.baseActorAiDistance = ref.baseActorAiDistance;
        snapshot.baseActorAiDuration = ref.baseActorAiDuration;
        snapshot.baseActorAiShouldRepeat = ref.baseActorAiShouldRepeat;
        snapshot.baseActorAiCoordinates = makePosition(
            ref.baseActorAiCoordinateX, ref.baseActorAiCoordinateY, ref.baseActorAiCoordinateZ, 0.f, 0.f, 0.f);
        snapshot.baseActorAiTargetId = ref.baseActorAiTargetId;
        snapshot.baseActorAiCellName = ref.baseActorAiCellName;
        snapshot.baseActorAiHello = ref.baseActorAiHello;
        snapshot.baseActorAiFight = ref.baseActorAiFight;
        snapshot.baseActorAiFlee = ref.baseActorAiFlee;
        snapshot.baseActorAiAlarm = ref.baseActorAiAlarm;
        snapshot.refNum = ref.refNumIndex;
        snapshot.mpNum = 0;
        snapshot.refNumContentFile = ref.refNumContentFile;
        snapshot.count = ref.count;
        snapshot.scale = ref.scale;
        snapshot.position = makePosition(ref.posX, ref.posY, ref.posZ, ref.rotX, ref.rotY, ref.rotZ);
        snapshot.moved = ref.moved;
        snapshot.teleport = ref.teleport;
        snapshot.locked = ref.locked;
        snapshot.lockLevel = ref.lockLevel;
        snapshot.destinationCell = ref.destCell;
        snapshot.destinationPosition = makePosition(ref.doorDestPosX, ref.doorDestPosY, ref.doorDestPosZ,
            ref.doorDestRotX, ref.doorDestRotY, ref.doorDestRotZ);
        snapshot.baseRecordResolved = ref.baseRecordResolved;
        snapshot.baseRecordAmbiguous = ref.baseRecordAmbiguous;
        snapshot.baseRecordDeleted = ref.baseRecordDeleted;

        if (snapshot.baseRecordAmbiguous)
            ++serverWorldBootstrapStats.ambiguousCount;
        else if (!snapshot.baseRecordResolved)
            ++serverWorldBootstrapStats.unresolvedCount;
        else if (isActorBootstrapCategory(snapshot.baseRecordCategory))
        {
            ++serverWorldBootstrapStats.actorCount;
            if (snapshot.baseActorAiAvailable)
                ++serverWorldBootstrapStats.actorAiCount;
            serverWorldActorList.baseActors.push_back(buildServerWorldActor(snapshot, cell));
        }
        else
        {
            if (snapshot.baseRecordCategory == "container")
                ++serverWorldBootstrapStats.containerCount;
            else if (snapshot.baseRecordCategory == "door")
                ++serverWorldBootstrapStats.doorCount;
            else if (snapshot.baseRecordCategory == "item" || snapshot.baseRecordCategory == "levelledItem")
                ++serverWorldBootstrapStats.itemCount;
            else if (snapshot.baseRecordCategory == "static")
                ++serverWorldBootstrapStats.staticCount;
            else if (snapshot.baseRecordCategory == "activator")
                ++serverWorldBootstrapStats.activatorCount;

            if (isObjectBootstrapCategory(snapshot.baseRecordCategory))
                serverWorldObjectList.baseObjects.push_back(buildServerWorldObject(snapshot));
        }

        serverWorldReferences.push_back(std::move(snapshot));
    }

    serverWorldBootstrapStats.referenceCount = serverWorldReferences.size();
    serverWorldActorList.count = static_cast<unsigned int>(serverWorldActorList.baseActors.size());
    serverWorldObjectList.baseObjectCount = static_cast<unsigned int>(serverWorldObjectList.baseObjects.size());
    serverWorldBootstrapStats.objectCount = serverWorldObjectList.baseObjects.size();
    serverWorldBootstrapStats.loaded = true;

    LOG_APPEND(TimedLog::LOG_INFO,
        "- Bootstrapped server world cell %s from worlddb with %zu refs, %zu actors, %zu actor AI packages, %zu objects, %zu containers, %zu doors, %zu unresolved",
        getShortDescription().c_str(), serverWorldBootstrapStats.referenceCount, serverWorldBootstrapStats.actorCount,
        serverWorldBootstrapStats.actorAiCount, serverWorldBootstrapStats.objectCount,
        serverWorldBootstrapStats.containerCount, serverWorldBootstrapStats.doorCount,
        serverWorldBootstrapStats.unresolvedCount);
}

bool Cell::hasServerWorldStateBootstrap() const
{
    return serverWorldBootstrapStats.loaded;
}

const Cell::ServerWorldBootstrapStats& Cell::getServerWorldBootstrapStats() const
{
    return serverWorldBootstrapStats;
}

const std::vector<Cell::ServerWorldReference>& Cell::getServerWorldReferences() const
{
    return serverWorldReferences;
}

const mwmp::BaseActorList& Cell::getServerWorldActorList() const
{
    return serverWorldActorList;
}

const mwmp::BaseObjectList& Cell::getServerWorldObjectList() const
{
    return serverWorldObjectList;
}

bool Cell::seedActorListFromServerWorldState()
{
    ensureServerWorldStateBootstrapped();

    if (serverWorldActorListSeeded)
        return false;

    if (!serverWorldBootstrapStats.loaded || serverWorldActorList.baseActors.empty())
        return false;

    if (actorListSnapshotReceived || !cellActorList.baseActors.empty())
        return false;

    mwmp::BaseActorList seedList = serverWorldActorList;
    seedList.guid = mwmp::unassignedPacketGuid();
    seedList.cell = cell;
    seedList.action = mwmp::BaseActorList::SET;
    seedList.isValid = true;
    seedList.count = static_cast<unsigned int>(seedList.baseActors.size());

    readActorList(ID_ACTOR_LIST, &seedList);
    serverWorldActorListSeeded = true;
    serverWorldSeededActorCount = seedList.baseActors.size();

    LOG_APPEND(TimedLog::LOG_INFO,
        "- Seeded live actor cache for server world cell %s from worlddb with %zu actors",
        getShortDescription().c_str(), serverWorldSeededActorCount);
    return true;
}

bool Cell::seedObjectListFromServerWorldState()
{
    ensureServerWorldStateBootstrapped();

    if (serverWorldObjectListSeeded)
        return false;

    if (!serverWorldBootstrapStats.loaded || serverWorldObjectList.baseObjects.empty())
        return false;

    if (objectListSnapshotReceived || !cellObjectList.baseObjects.empty())
        return false;

    mwmp::BaseObjectList seedList = serverWorldObjectList;
    seedList.guid = mwmp::unassignedPacketGuid();
    seedList.cell = cell;
    seedList.action = mwmp::BaseObjectList::SET;
    seedList.isValid = true;
    seedList.baseObjectCount = static_cast<unsigned int>(seedList.baseObjects.size());

    readObjectList(ID_OBJECT_PLACE, &seedList);
    objectListSnapshotReceived = true;
    serverWorldObjectListSeeded = true;
    serverWorldSeededObjectCount = seedList.baseObjects.size();

    LOG_APPEND(TimedLog::LOG_INFO,
        "- Seeded live object cache for server world cell %s from worlddb with %zu objects",
        getShortDescription().c_str(), serverWorldSeededObjectCount);
    return true;
}

bool Cell::hasServerWorldSeededActorList() const
{
    return serverWorldActorListSeeded;
}

bool Cell::hasServerWorldSeededObjectList() const
{
    return serverWorldObjectListSeeded;
}

std::size_t Cell::getServerWorldSeededActorCount() const
{
    return serverWorldSeededActorCount;
}

std::size_t Cell::getServerWorldSeededObjectCount() const
{
    return serverWorldSeededObjectCount;
}

mwmp::PacketGuid *Cell::getAuthority()
{
    return &authorityGuid;
}

void Cell::setAuthority(const mwmp::PacketGuid& guid)
{
    authorityGuid = guid;
}

bool Cell::hasAuthority(const mwmp::PacketGuid& guid) const
{
    return mwmp::isPacketGuidAssigned(authorityGuid) && authorityGuid == guid;
}

mwmp::BaseActorList *Cell::getActorList()
{
    return &cellActorList;
}

void Cell::readObjectList(unsigned char packetID, const mwmp::BaseObjectList *newObjectList)
{
    if (newObjectList == nullptr)
        return;

    if (packetID == ID_OBJECT_DELETE)
    {
        for (const mwmp::BaseObject& incomingObject : newObjectList->baseObjects)
        {
            mwmp::BaseObject* cellObject = getObject(incomingObject.refNum, incomingObject.mpNum);
            const mwmp::BaseObject* serverWorldObject
                = findObject(serverWorldObjectList, incomingObject.refNum, incomingObject.mpNum);

            if (cellObject != nullptr && serverWorldObject != nullptr)
                cellObject->objectState = false;
            else if (cellObject != nullptr)
            {
                mwmp::BaseObjectList deleteList;
                deleteList.baseObjects.push_back(incomingObject);
                removeObjects(&deleteList);
            }
            else if (serverWorldObject != nullptr)
            {
                mwmp::BaseObject tombstone = *serverWorldObject;
                tombstone.objectState = false;
                tombstone.containerItemCount = static_cast<unsigned int>(tombstone.containerItems.size());
                cellObjectList.baseObjects.push_back(std::move(tombstone));
            }
        }

        cellObjectList.baseObjectCount = static_cast<unsigned int>(cellObjectList.baseObjects.size());
        objectListSnapshotReceived = true;
        return;
    }

    if (packetID == ID_CONTAINER && newObjectList->action == mwmp::BaseObjectList::REQUEST)
        return;

    for (const mwmp::BaseObject& incomingObject : newObjectList->baseObjects)
    {
        const auto key = objectKey(incomingObject);

        if (packetID == ID_CONTAINER && newObjectList->action != mwmp::BaseObjectList::SET
            && knownContainerSnapshots.find(key) == knownContainerSnapshots.end())
            continue;

        mwmp::BaseObject* cellObject = getObject(incomingObject.refNum, incomingObject.mpNum);

        if (cellObject == nullptr)
        {
            mwmp::BaseObject objectToStore{};
            if (const mwmp::BaseObject* serverWorldObject
                = findObject(serverWorldObjectList, incomingObject.refNum, incomingObject.mpNum))
            {
                objectToStore = *serverWorldObject;
            }
            else
            {
                objectToStore.refId = incomingObject.refId;
                objectToStore.refNum = incomingObject.refNum;
                objectToStore.mpNum = incomingObject.mpNum;
                objectToStore.count = 1;
                objectToStore.charge = -1;
                objectToStore.enchantmentCharge = -1.0;
                objectToStore.objectState = true;
                objectToStore.scale = 1.f;
            }

            objectToStore.containerItemCount = static_cast<unsigned int>(objectToStore.containerItems.size());
            cellObjectList.baseObjects.push_back(std::move(objectToStore));
            cellObject = &cellObjectList.baseObjects.back();
        }

        mergeObjectPacketFields(*cellObject, incomingObject, packetID, newObjectList->action);

        if (packetID == ID_CONTAINER && newObjectList->action == mwmp::BaseObjectList::SET)
            knownContainerSnapshots.insert(key);
    }

    cellObjectList.baseObjectCount = static_cast<unsigned int>(cellObjectList.baseObjects.size());
    objectListSnapshotReceived = true;
}

bool Cell::containsObject(int refNum, int mpNum)
{
    return getObject(refNum, mpNum) != nullptr;
}

mwmp::BaseObject *Cell::getObject(int refNum, int mpNum)
{
    for (mwmp::BaseObject& object : cellObjectList.baseObjects)
    {
        if (object.refNum == static_cast<unsigned int>(refNum) && object.mpNum == static_cast<unsigned int>(mpNum))
            return &object;
    }

    return nullptr;
}

void Cell::upsertObjects(const mwmp::BaseObjectList *newObjectList)
{
    if (newObjectList == nullptr)
        return;

    for (const mwmp::BaseObject& incomingObject : newObjectList->baseObjects)
    {
        mwmp::BaseObject objectToStore = incomingObject;
        objectToStore.containerItemCount = static_cast<unsigned int>(objectToStore.containerItems.size());

        if (mwmp::BaseObject* cellObject = getObject(objectToStore.refNum, objectToStore.mpNum))
            *cellObject = objectToStore;
        else
            cellObjectList.baseObjects.push_back(objectToStore);
    }

    cellObjectList.baseObjectCount = static_cast<unsigned int>(cellObjectList.baseObjects.size());
    objectListSnapshotReceived = true;
}

void Cell::removeObjects(const mwmp::BaseObjectList *newObjectList)
{
    if (newObjectList == nullptr)
        return;

    for (std::vector<mwmp::BaseObject>::iterator it = cellObjectList.baseObjects.begin();
         it != cellObjectList.baseObjects.end();)
    {
        const unsigned int refNum = it->refNum;
        const unsigned int mpNum = it->mpNum;
        bool foundObject = false;

        for (const mwmp::BaseObject& newObject : newObjectList->baseObjects)
        {
            if (newObject.refNum == refNum && newObject.mpNum == mpNum)
            {
                it = cellObjectList.baseObjects.erase(it);
                foundObject = true;
                break;
            }
        }

        if (!foundObject)
            ++it;
    }

    cellObjectList.baseObjectCount = static_cast<unsigned int>(cellObjectList.baseObjects.size());
    objectListSnapshotReceived = true;
}

mwmp::BaseObjectList *Cell::getObjectList()
{
    return &cellObjectList;
}

const ESM::Cell& Cell::getCellData() const
{
    return cell;
}

Cell::TPlayers Cell::getPlayers() const
{
    return players;
}

bool Cell::hasPlayers() const
{
    return !players.empty();
}

bool Cell::hasPlayer(const Player* player) const
{
    return player != nullptr && std::find(players.begin(), players.end(), player) != players.end();
}

bool Cell::hasSimulationInterest() const
{
    return simulationInterest;
}

void Cell::setSimulationInterest(bool enabled)
{
    simulationInterest = enabled;
}

void Cell::sendToLoaded(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList) const
{
    if (players.empty())
        return;

    std::list <Player*> plList;

    for (auto pl : players)
    {
        if (pl != nullptr && !pl->npc.mName.empty())
            plList.push_back(pl);
    }

    plList.sort();
    plList.unique();

    const std::vector<float> originalLatencies = captureMovementLatencies(*baseActorList);
    for (auto pl : plList)
    {
        if (pl->guid == baseActorList->guid) continue;

        setMovementLatencies(*baseActorList, estimateRouteLatencySeconds(baseActorList->guid, pl->guid));
        actorPacket->setActorList(baseActorList);

        // Send the packet to this eligible guid
        actorPacket->Send(pl->guid);
    }
    restoreMovementLatencies(*baseActorList, originalLatencies);
}

void Cell::sendToLoadedAndGuids(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList,
    const std::vector<mwmp::PacketGuid>& targetGuids) const
{
    std::list <Player*> plList;

    for (auto pl : players)
    {
        if (pl != nullptr && !pl->npc.mName.empty())
            plList.push_back(pl);
    }

    for (const mwmp::PacketGuid& targetGuid : targetGuids)
    {
        if (targetGuid == mwmp::unassignedPacketGuid() || targetGuid == baseActorList->guid)
            continue;

        Player* target = Players::getPlayer(targetGuid);
        if (target != nullptr && !target->npc.mName.empty())
            plList.push_back(target);
    }

    plList.sort();
    plList.unique();

    const std::vector<float> originalLatencies = captureMovementLatencies(*baseActorList);
    for (auto pl : plList)
    {
        if (pl->guid == baseActorList->guid) continue;

        setMovementLatencies(*baseActorList, estimateRouteLatencySeconds(baseActorList->guid, pl->guid));
        actorPacket->setActorList(baseActorList);
        actorPacket->Send(pl->guid);
    }
    restoreMovementLatencies(*baseActorList, originalLatencies);
}

void Cell::sendToLoaded(mwmp::ObjectPacket *objectPacket, mwmp::BaseObjectList *baseObjectList) const
{
    if (players.empty())
        return;

    std::list <Player*> plList;

    for (auto pl : players)
    {
        if (pl != nullptr && !pl->npc.mName.empty())
            plList.push_back(pl);
    }

    plList.sort();
    plList.unique();

    for (auto pl : plList)
    {
        if (pl->guid == baseObjectList->guid) continue;

        objectPacket->setObjectList(baseObjectList);

        // Send the packet to this eligible guid
        objectPacket->Send(pl->guid);
    }
}

void Cell::sendServerObjectStateSnapshotTo(Player& player) const
{
    if (player.npc.mName.empty())
        return;

    mwmp::BaseObjectList containers;
    mwmp::BaseObjectList doorDestinations;
    mwmp::BaseObjectList doorStates;
    mwmp::BaseObjectList disabledObjects;

    auto initializeList = [&](mwmp::BaseObjectList& list) {
        list.guid = mwmp::unassignedPacketGuid();
        list.cell = cell;
        list.packetOrigin = mwmp::SERVER_SCRIPT;
        list.originClientScript.clear();
        list.action = mwmp::BaseObjectList::SET;
        list.containerSubAction = mwmp::BaseObjectList::NONE;
        list.isValid = true;
        list.baseObjectCount = 0;
    };

    initializeList(containers);
    initializeList(doorDestinations);
    initializeList(doorStates);
    initializeList(disabledObjects);

    containers.containerSubAction = mwmp::BaseObjectList::NONE;

    for (const mwmp::BaseObject& object : cellObjectList.baseObjects)
    {
        if (!object.objectState)
            disabledObjects.baseObjects.push_back(object);

        const bool door = isDoorCategory(serverWorldReferences, object);
        if ((door || object.teleportState) && object.teleportState)
            doorDestinations.baseObjects.push_back(object);

        if (door && object.doorState != 0)
            doorStates.baseObjects.push_back(object);

        if (object.hasContainer && knownContainerSnapshots.find(objectKey(object)) != knownContainerSnapshots.end())
            containers.baseObjects.push_back(object);
    }

    auto sendList = [&](auto packetID, mwmp::BaseObjectList& list) -> std::size_t {
        constexpr std::size_t maxSnapshotObjectsPerPacket = 512;
        if (list.baseObjects.empty())
            return 0;

        mwmp::ObjectPacket* packet = mwmp::ServerNetworking::get().getObjectPacketController()->GetPacket(packetID);
        std::size_t sentObjects = 0;

        for (std::size_t offset = 0; offset < list.baseObjects.size(); offset += maxSnapshotObjectsPerPacket)
        {
            mwmp::BaseObjectList chunk = list;
            const std::size_t end = std::min(offset + maxSnapshotObjectsPerPacket, list.baseObjects.size());
            chunk.baseObjects.assign(list.baseObjects.begin() + offset, list.baseObjects.begin() + end);
            chunk.baseObjectCount = static_cast<unsigned int>(chunk.baseObjects.size());

            packet->setObjectList(&chunk);
            packet->Send(player.guid);
            sentObjects += chunk.baseObjects.size();
        }

        return sentObjects;
    };

    const std::size_t sentDisabledObjects = sendList(ID_OBJECT_STATE, disabledObjects);
    const std::size_t sentDoorDestinations = sendList(ID_DOOR_DESTINATION, doorDestinations);
    const std::size_t sentDoorStates = sendList(ID_DOOR_STATE, doorStates);
    const std::size_t sentContainers = sendList(ID_CONTAINER, containers);

    if (sentDisabledObjects != 0 || sentDoorDestinations != 0 || sentDoorStates != 0 || sentContainers != 0)
    {
        LOG_APPEND(TimedLog::LOG_INFO,
            "- Sent server object snapshot for cell %s to %s: %zu disabled, %zu door destinations, %zu door states, %zu containers",
            getShortDescription().c_str(), player.npc.mName.c_str(), sentDisabledObjects, sentDoorDestinations,
            sentDoorStates, sentContainers);
    }
}

std::string Cell::getShortDescription() const
{
    return cell.getDescription();
}
