#include "Cell.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <utility>
#include "Player.hpp"
#include "ServerEventDispatcher.hpp"

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
}

Cell::Cell(ESM::Cell cell)
    : cell(cell)
    , authorityGuid(mwmp::unassignedPacketGuid())
    , actorListRequestGuid(mwmp::unassignedPacketGuid())
    , actorListSnapshotReceived(false)
    , simulationInterest(false)
{
    cellActorList.count = 0;
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
