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
}

Cell::Cell(ESM::Cell cell)
    : cell(cell)
    , authorityGuid(mwmp::unassignedPacketGuid())
    , actorListRequestGuid(mwmp::unassignedPacketGuid())
    , actorListSnapshotReceived(false)
    , serverWorldActorListSeeded(false)
    , serverWorldSeededActorCount(0)
    , simulationInterest(false)
{
    cellActorList.count = 0;
    serverWorldActorList.count = 0;
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

    mwmp::WorldDatabaseStore::get().ensureLoaded();
    const mwmp::WorldDatabaseStatistics worldStats = mwmp::WorldDatabaseStore::get().statistics();
    if (!worldStats.loaded)
        return;

    std::vector<mwmp::WorldCellReferenceRecord> references
        = mwmp::WorldDatabaseStore::get().findReferencesByCellKey(serverWorldBootstrapStats.cellKey);
    serverWorldReferences.reserve(references.size());
    serverWorldActorList.baseActors.reserve(references.size());

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
        else if (snapshot.baseRecordCategory == "container")
            ++serverWorldBootstrapStats.containerCount;
        else if (snapshot.baseRecordCategory == "door")
            ++serverWorldBootstrapStats.doorCount;
        else if (snapshot.baseRecordCategory == "item" || snapshot.baseRecordCategory == "levelledItem")
            ++serverWorldBootstrapStats.itemCount;
        else if (snapshot.baseRecordCategory == "static")
            ++serverWorldBootstrapStats.staticCount;
        else if (snapshot.baseRecordCategory == "activator")
            ++serverWorldBootstrapStats.activatorCount;

        serverWorldReferences.push_back(std::move(snapshot));
    }

    serverWorldBootstrapStats.referenceCount = serverWorldReferences.size();
    serverWorldActorList.count = static_cast<unsigned int>(serverWorldActorList.baseActors.size());
    serverWorldBootstrapStats.loaded = true;

    LOG_APPEND(TimedLog::LOG_INFO,
        "- Bootstrapped server world cell %s from worlddb with %zu refs, %zu actors, %zu actor AI packages, %zu containers, %zu doors, %zu unresolved",
        getShortDescription().c_str(), serverWorldBootstrapStats.referenceCount, serverWorldBootstrapStats.actorCount,
        serverWorldBootstrapStats.actorAiCount, serverWorldBootstrapStats.containerCount, serverWorldBootstrapStats.doorCount,
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

bool Cell::hasServerWorldSeededActorList() const
{
    return serverWorldActorListSeeded;
}

std::size_t Cell::getServerWorldSeededActorCount() const
{
    return serverWorldSeededActorCount;
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

std::string Cell::getShortDescription() const
{
    return cell.getDescription();
}
