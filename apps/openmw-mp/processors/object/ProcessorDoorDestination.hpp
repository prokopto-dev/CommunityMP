#ifndef OPENMW_PROCESSORDOORDESTINATION_HPP
#define OPENMW_PROCESSORDOORDESTINATION_HPP

#include "../ObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorDoorDestination : public ObjectProcessor
    {
    public:
        ProcessorDoorDestination()
        {
            BPP_INIT(ID_DOOR_DESTINATION)
        }

        void Do(ObjectPacket& packet, Player& player, BaseObjectList& objectList) override
        {
            sendToLoadedOrBroadcast(packet, objectList);

            ServerEvents::objectEvent("OnDoorDestination", player.getId(), objectList.cell.getDescription().c_str());
        }
    };
}

#endif // OPENMW_PROCESSORDOORDESTINATION_HPP
