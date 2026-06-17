#ifndef OPENMW_PROCESSOROBJECTSPAWN_HPP
#define OPENMW_PROCESSOROBJECTSPAWN_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Cell.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <apps/openmw-mp/ServerNetworking.hpp>

#include <algorithm>

namespace mwmp
{
    class ProcessorObjectSpawn : public ObjectProcessor
    {
    public:
        ProcessorObjectSpawn()
        {
            BPP_INIT(ID_OBJECT_SPAWN)
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            Cell* serverCell = CellController::get()->getCell(&objectList.cell);
            const auto originalCount = objectList.baseObjects.size();
            objectList.baseObjects.erase(std::remove_if(objectList.baseObjects.begin(), objectList.baseObjects.end(),
                [&](const BaseObject& object) {
                    if (serverCell == nullptr || object.isSummon || object.refNum == 0)
                        return false;

                    // A gameplay ObjectSpawn for a refNum that is already a known actor in the
                    // cell is a duplicate identity, not a new spawn. Accepting it gives the
                    // vanilla actor a fresh mpNum and later clients see actors jump/vanish.
                    return serverCell->containsActor(object.refNum, object.mpNum)
                        || serverCell->containsActor(object.refNum, 0);
                }),
                objectList.baseObjects.end());
            objectList.baseObjectCount = static_cast<unsigned int>(objectList.baseObjects.size());

            if (objectList.baseObjectCount != originalCount)
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Rejected %zu duplicate actor ObjectSpawn entr%s from %s in %s",
                    originalCount - objectList.baseObjectCount,
                    originalCount - objectList.baseObjectCount == 1 ? "y" : "ies",
                    player.npc.mName.c_str(), objectList.cell.getDescription().c_str());

            if (objectList.baseObjectCount == 0)
                return;

            for (BaseObject& object : objectList.baseObjects)
            {
                object.mpNum = mwmp::ServerNetworking::getPtr()->incrementMpNum();
            }

            Script::Call<Script::CallbackIdentity("OnObjectSpawn")>(player.getId(), objectList.cell.getDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTSPAWN_HPP
