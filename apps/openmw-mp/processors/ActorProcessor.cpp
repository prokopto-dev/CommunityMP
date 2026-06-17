#include "ActorProcessor.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "ServerNetworking.hpp"
#include "actor/ActorSequenceCoalescing.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>
#include <algorithm>
#include <cstddef>
#include <set>
#include <vector>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

namespace
{
    float estimateOneWayLatencySeconds(mwmp::PacketGuid guid)
    {
        if (guid == mwmp::unassignedPacketGuid())
            return 0.f;

        const int pingMilliseconds = mwmp::ServerNetworking::get().getAvgPing(guid);
        if (pingMilliseconds <= 0)
            return 0.f;

        return mwmp::sanitizeMovementLatencySeconds(static_cast<float>(pingMilliseconds) * 0.0005f);
    }

    float estimateRouteLatencySeconds(mwmp::PacketGuid sourceGuid, mwmp::PacketGuid destinationGuid)
    {
        return mwmp::sanitizeMovementLatencySeconds(
            estimateOneWayLatencySeconds(sourceGuid) + estimateOneWayLatencySeconds(destinationGuid));
    }

    std::vector<float> captureMovementLatencies(const BaseActorList& actorList)
    {
        std::vector<float> latencies;
        latencies.reserve(actorList.baseActors.size());
        for (const BaseActor& actor : actorList.baseActors)
            latencies.push_back(actor.movementLatencySeconds);
        return latencies;
    }

    void setMovementLatencies(BaseActorList& actorList, float latencySeconds)
    {
        const float sanitizedLatencySeconds = mwmp::sanitizeMovementLatencySeconds(latencySeconds);
        for (BaseActor& actor : actorList.baseActors)
            actor.movementLatencySeconds = sanitizedLatencySeconds;
    }

    void restoreMovementLatencies(BaseActorList& actorList, const std::vector<float>& latencies)
    {
        const std::size_t count = std::min(actorList.baseActors.size(), latencies.size());
        for (std::size_t i = 0; i < count; ++i)
            actorList.baseActors[i].movementLatencySeconds = latencies[i];
    }

    void sendToLoadedCellPlayers(ActorPacket &packet, BaseActorList &actorList, Cell *serverCell,
        std::set<PacketGuid>& sentGuids)
    {
        if (serverCell == nullptr)
            return;

        const std::vector<float> originalLatencies = captureMovementLatencies(actorList);
        for (Player *pl : serverCell->getPlayers())
        {
            if (pl == nullptr || pl->npc.mName.empty() || pl->guid == actorList.guid)
                continue;

            if (!sentGuids.insert(pl->guid).second)
                continue;

            setMovementLatencies(actorList, estimateRouteLatencySeconds(actorList.guid, pl->guid));
            packet.setActorList(&actorList);
            packet.Send(pl->guid);
        }
        restoreMovementLatencies(actorList, originalLatencies);
    }

    bool isSameCell(const ESM::Cell& left, const ESM::Cell& right)
    {
        if (left.isExterior() != right.isExterior())
            return false;

        if (left.isExterior())
            return left.mData.mX == right.mData.mX && left.mData.mY == right.mData.mY;

        return left.mName == right.mName;
    }

    BaseActor buildMovedActor(Cell *sourceCell, const BaseActor& actor)
    {
        BaseActor movedActor = actor;

        if (sourceCell != nullptr)
        {
            if (BaseActor *storedActor = sourceCell->getActor(actor.refNum, actor.mpNum))
                movedActor = *storedActor;
        }

        movedActor.cell = actor.cell;
        movedActor.positionSequence = actor.positionSequence;
        movedActor.position = actor.position;
        movedActor.direction = actor.direction;
        movedActor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
            actor.movementSampleIntervalSeconds);
        movedActor.movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(actor.movementLatencySeconds);
        movedActor.hasPositionData = true;
        movedActor.isFollowerCellChange = actor.isFollowerCellChange;

        return movedActor;
    }
}

void ActorProcessor::Do(ActorPacket &packet, Player &player, BaseActorList &actorList)
{
    sendToLoaded(packet, actorList);
}

void ActorProcessor::sendToLoaded(ActorPacket &packet, BaseActorList &actorList)
{
    Cell *serverCell = CellController::get()->getCell(&actorList.cell);
    if (serverCell == nullptr)
        return;

    packet.setActorList(&actorList);
    serverCell->sendToLoaded(&packet, &actorList);
}

void ActorProcessor::sendCellChangeToLoaded(ActorPacket &packet, BaseActorList &actorList)
{
    std::set<PacketGuid> sentGuids;

    sendToLoadedCellPlayers(packet, actorList, CellController::get()->getCell(&actorList.cell), sentGuids);

    for (BaseActor& actor : actorList.baseActors)
        sendToLoadedCellPlayers(packet, actorList, CellController::get()->getCell(&actor.cell), sentGuids);
}

void ActorProcessor::cacheCellChange(BaseActorList &actorList)
{
    actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

    BaseActorList actorsToRemove = actorList;
    actorsToRemove.baseActors.clear();

    Cell *sourceCell = CellController::get()->getCell(&actorList.cell);

    for (const BaseActor& actor : actorList.baseActors)
    {
        if (isSameCell(actorList.cell, actor.cell) || !isFiniteActorMovementSnapshot(actor))
            continue;

        actorsToRemove.baseActors.push_back(actor);

        ESM::Cell destinationCellData = actor.cell;
        Cell *destinationCell = CellController::get()->getCell(&destinationCellData);
        if (destinationCell == nullptr)
            continue;

        BaseActorList destinationActorList = actorList;
        destinationActorList.cell = actor.cell;
        destinationActorList.baseActors.clear();
        destinationActorList.baseActors.push_back(buildMovedActor(sourceCell, actor));
        destinationActorList.count = static_cast<unsigned int>(destinationActorList.baseActors.size());

        destinationCell->upsertActors(&destinationActorList);
    }

    actorsToRemove.count = static_cast<unsigned int>(actorsToRemove.baseActors.size());
    if (sourceCell != nullptr && actorsToRemove.count != 0)
        sourceCell->removeActors(&actorsToRemove);
}

bool ActorProcessor::Process(ReceivedPacket& packet, BaseActorList &actorList) noexcept
{
    // Clear our BaseActorList before loading new data in it
    actorList.cell.blank();
    actorList.baseActors.clear();
    actorList.guid = packet.guid();

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            Player *player = Players::getPlayer(packet.guid());
            if (player == nullptr)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Received %s from missing player session and ignored!",
                    processor.second->strPacketID.c_str());
                return true;
            }

            ActorPacket *myPacket = ServerNetworking::get().getActorPacketController()->GetPacket(packet.id());

            myPacket->setActorList(&actorList);
            actorList.isValid = true;

            if (!processor.second->avoidReading)
                myPacket->Read();

            if (actorList.isValid && myPacket->isPacketValid())
                processor.second->Do(*myPacket, *player, actorList);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());

            return true;
        }
    }
    return false;
}
