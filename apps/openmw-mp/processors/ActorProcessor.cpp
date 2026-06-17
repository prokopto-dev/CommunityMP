#include "ActorProcessor.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "Networking.hpp"
#include "actor/ActorSequenceCoalescing.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>
#include <set>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

namespace
{
    void sendToLoadedCellPlayers(ActorPacket &packet, BaseActorList &actorList, Cell *serverCell,
        std::set<PacketGuid>& sentGuids)
    {
        if (serverCell == nullptr)
            return;

        for (Player *pl : serverCell->getPlayers())
        {
            if (pl == nullptr || pl->npc.mName.empty() || pl->guid == actorList.guid)
                continue;

            if (!sentGuids.insert(pl->guid).second)
                continue;

            packet.setActorList(&actorList);
            packet.Send(pl->guid);
        }
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

            ActorPacket *myPacket = Networking::get().getActorPacketController()->GetPacket(packet.id());

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
