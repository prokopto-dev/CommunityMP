#ifndef OPENMW_PROCESSORCONTAINER_HPP
#define OPENMW_PROCESSORCONTAINER_HPP

#include "../ObjectProcessor.hpp"
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>

namespace mwmp
{
    class ProcessorContainer : public ObjectProcessor
    {
    public:
        ProcessorContainer()
        {
            BPP_INIT(ID_CONTAINER)
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            LOG_APPEND(TimedLog::LOG_INFO, "- action: %i", objectList.action);

            if (!isContainerPacketAllowedFromClient(objectList))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Rejected untrusted container packet from %s with origin %u, action %u and subaction %u",
                    player.npc.mName.c_str(), objectList.packetOrigin, objectList.action, objectList.containerSubAction);
                objectList.isValid = false;
                return;
            }

            // Don't have any hardcoded sync, and instead expect Lua scripts to forward
            // container packets to ensure their integrity based on what exists in the
            // server data

            ServerEvents::objectEvent("OnContainer", player.getId(), objectList.cell.getDescription().c_str());

            LOG_APPEND(TimedLog::LOG_INFO, "- Finished processing ID_CONTAINER");
        }
    };
}

#endif //OPENMW_PROCESSORCONTAINER_HPP
