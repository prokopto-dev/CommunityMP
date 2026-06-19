#include "ObjectProcessor.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "ServerNetworking.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void ObjectProcessor::Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList)
{
    sendToLoadedOrBroadcast(packet, objectList);
}

void ObjectProcessor::sendToLoadedOrBroadcast(ObjectPacket &packet, BaseObjectList &objectList)
{
    if (!packet.carriesCellData())
    {
        packet.setObjectList(&objectList);
        packet.Send(true);
        return;
    }

    Cell *serverCell = CellController::get()->getCell(&objectList.cell);
    if (serverCell == nullptr)
        return;

    serverCell->sendToLoaded(&packet, &objectList);
}

bool ObjectProcessor::Process(ReceivedPacket& packet, BaseObjectList &objectList) noexcept
{
    // Clear our BaseObjectList before loading new data in it
    objectList.cell.blank();
    objectList.baseObjects.clear();
    objectList.guid = packet.guid();

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

            ObjectPacket *myPacket = ServerNetworking::get().getObjectPacketController()->GetPacket(packet.id());

            myPacket->setObjectList(&objectList);
            objectList.isValid = true;

            if (!processor.second->avoidReading)
                myPacket->Read();

            if (objectList.isValid && myPacket->isPacketValid())
            {
                processor.second->Do(*myPacket, *player, objectList);
                if (objectList.isValid && myPacket->carriesCellData())
                {
                    // Client object packets are intent. The C++ cache is updated only from the
                    // server-authored send path after Lua/runtime validation has accepted or
                    // rewritten the result.
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                        "Deferred %s cell cache update until server-authored object state send",
                        processor.second->strPacketID.c_str());
                }
            }
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
            
            return true;
        }
    }
    return false;
}
