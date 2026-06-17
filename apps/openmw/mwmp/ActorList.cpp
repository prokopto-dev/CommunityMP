#include "ActorList.hpp"
#include "CellController.hpp"
#include "CellIdentity.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "MechanicsHelper.hpp"

#include "../mwworld/class.hpp"

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

namespace
{
    bool hasSameActorIdentity(const BaseActor& left, const BaseActor& right)
    {
        return left.refNum == right.refNum && left.mpNum == right.mpNum;
    }

    void acceptNewestPositionActor(std::vector<BaseActor>& actors, const BaseActor& baseActor)
    {
        for (BaseActor& actor : actors)
        {
            if (!hasSameActorIdentity(actor, baseActor))
                continue;

            if (isNewerPositionSequence(baseActor.positionSequence, actor.positionSequence))
                actor = baseActor;

            return;
        }

        actors.push_back(baseActor);
    }

    void acceptNewestAnimFlagsActor(std::vector<BaseActor>& actors, const BaseActor& baseActor)
    {
        for (BaseActor& actor : actors)
        {
            if (!hasSameActorIdentity(actor, baseActor))
                continue;

            mergeNewestActorAnimFlags(actor, baseActor);

            return;
        }

        actors.push_back(baseActor);
    }

    void acceptNewestEquipmentActor(std::vector<BaseActor>& actors, const BaseActor& baseActor)
    {
        for (BaseActor& actor : actors)
        {
            if (!hasSameActorIdentity(actor, baseActor))
                continue;

            if (isNewerActorEquipmentSequence(baseActor.equipmentSequence, actor.equipmentSequence))
                actor = baseActor;

            return;
        }

        actors.push_back(baseActor);
    }

}

ActorList::ActorList()
{

}

ActorList::~ActorList()
{

}

Networking *ActorList::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

void ActorList::reset()
{
    cell.blank();
    baseActors.clear();
    positionActors.clear();
    animFlagsActors.clear();
    animPlayActors.clear();
    speechActors.clear();
    statsDynamicActors.clear();
    deathActors.clear();
    equipmentActors.clear();
    aiActors.clear();
    attackActors.clear();
    castActors.clear();
    cellChangeActors.clear();
    guid = mwmp::Main::get().getNetworking()->getLocalPlayer()->guid;
}

void ActorList::addActor(BaseActor baseActor)
{
    baseActors.push_back(baseActor);
}

void ActorList::addPositionActor(BaseActor baseActor)
{
    acceptNewestPositionActor(positionActors, baseActor);
}

void ActorList::addAnimFlagsActor(BaseActor baseActor)
{
    acceptNewestAnimFlagsActor(animFlagsActors, baseActor);
}

void ActorList::addAnimPlayActor(BaseActor baseActor)
{
    animPlayActors.push_back(baseActor);
}

void ActorList::addSpeechActor(BaseActor baseActor)
{
    speechActors.push_back(baseActor);
}

void ActorList::addStatsDynamicActor(BaseActor baseActor)
{
    for (BaseActor& actor : statsDynamicActors)
    {
        if (hasSameActorIdentity(actor, baseActor))
        {
            actor = baseActor;
            return;
        }
    }

    statsDynamicActors.push_back(baseActor);
}

void ActorList::addDeathActor(BaseActor baseActor)
{
    deathActors.push_back(baseActor);
}

void ActorList::addEquipmentActor(BaseActor baseActor)
{
    acceptNewestEquipmentActor(equipmentActors, baseActor);
}

void ActorList::addAiActor(BaseActor baseActor)
{
    aiActors.push_back(baseActor);
}

void ActorList::addAiActor(const MWWorld::Ptr& actorPtr, const MWWorld::Ptr& targetPtr, unsigned int aiAction)
{
    mwmp::BaseActor baseActor;

    CellController* cellController = Main::get().getCellController();
    const auto [refNum, mpNum] = cellController->getActorNetworkId(actorPtr);
    baseActor.refNum = refNum;
    baseActor.mpNum = mpNum;

    if (cellController->isLocalActor(actorPtr))
    {
        LocalActor* localActor = cellController->getLocalActor(actorPtr);
        if (localActor != nullptr)
        {
            localActor->updatePosition(true, false);
            baseActor = *localActor;
        }
    }

    baseActor.aiAction = aiAction;
    baseActor.aiTarget = MechanicsHelper::getTarget(targetPtr);
    baseActor.hasAiTarget = !MechanicsHelper::isEmptyTarget(baseActor.aiTarget);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Preparing to send ID_ACTOR_AI about %s %i-%i\n- action: %i",
        actorPtr.getCellRef().getRefId().serializeText().c_str(), baseActor.refNum, baseActor.mpNum, aiAction);

    if (baseActor.aiTarget.isPlayer)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has player target %s",
            std::string(targetPtr.getClass().getName(targetPtr)).c_str());
    }
    else
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has actor target %s %i-%i",
            targetPtr.getCellRef().getRefId().serializeText().c_str(), baseActor.aiTarget.refNum,
            baseActor.aiTarget.mpNum);
    }

    addAiActor(baseActor);
}

void ActorList::addAttackActor(BaseActor baseActor)
{
    attackActors.push_back(baseActor);
}

void ActorList::addAttackActor(const MWWorld::Ptr& actorPtr, const mwmp::Attack &attack)
{
    mwmp::BaseActor baseActor;
    const auto [refNum, mpNum] = Main::get().getCellController()->getActorNetworkId(actorPtr);
    baseActor.refNum = refNum;
    baseActor.mpNum = mpNum;
    baseActor.attack = attack;
    attackActors.push_back(baseActor);
}

void ActorList::addCastActor(BaseActor baseActor)
{
    castActors.push_back(baseActor);
}

void ActorList::addCellChangeActor(BaseActor baseActor)
{
    cellChangeActors.push_back(baseActor);
}

void ActorList::sendPositionActors()
{
    if (positionActors.size() > 0)
    {
        baseActors = positionActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->Send();
    }
}

void ActorList::sendAnimFlagsActors()
{
    if (animFlagsActors.size() > 0)
    {
        baseActors = animFlagsActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->Send();
    }
}

void ActorList::sendAnimPlayActors()
{
    if (animPlayActors.size() > 0)
    {
        baseActors = animPlayActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->Send();
    }
}

void ActorList::sendSpeechActors()
{
    if (speechActors.size() > 0)
    {
        baseActors = speechActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->Send();
    }
}

void ActorList::sendStatsDynamicActors()
{
    if (statsDynamicActors.size() > 0)
    {
        baseActors = statsDynamicActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->Send();
    }
}

void ActorList::sendDeathActors()
{
    if (deathActors.size() > 0)
    {
        baseActors = deathActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();
    }
}

void ActorList::sendEquipmentActors()
{
    if (equipmentActors.size() > 0)
    {
        baseActors = equipmentActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
    }
}

void ActorList::sendAiActors()
{
    if (aiActors.size() > 0)
    {
        baseActors = aiActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->Send();
    }
}

void ActorList::sendAttackActors()
{
    if (attackActors.size() > 0)
    {
        baseActors = attackActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->Send();
    }
}

void ActorList::sendCastActors()
{
    if (castActors.size() > 0)
    {
        baseActors = castActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->Send();
    }
}

void ActorList::sendCellChangeActors()
{
    if (cellChangeActors.size() > 0)
    {
        baseActors = cellChangeActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->Send();
    }
}

void ActorList::sendActorsInCell(MWWorld::CellStore* cellStore)
{
    reset();
    cell = makeActorPacketCell(*cellStore->getCell());
    action = BaseActorList::SET;

    auto addCellActor = [this](const MWWorld::Ptr& ptr) {
        if (ptr.getCellRef().getRefNum().mIndex == 0)
            return true;

        BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId().serializeText();
        const auto [refNum, mpNum] = Main::get().getCellController()->getActorNetworkId(ptr);
        actor.refNum = refNum;
        actor.mpNum = mpNum;

        addActor(actor);
        return true;
    };

    cellStore->forEachType<ESM::NPC>(addCellActor);
    cellStore->forEachType<ESM::Creature>(addCellActor);

    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->setActorList(this);
    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->Send();
}

