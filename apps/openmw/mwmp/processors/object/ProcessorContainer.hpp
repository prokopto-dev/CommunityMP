#ifndef OPENMW_PROCESSORCONTAINER_HPP
#define OPENMW_PROCESSORCONTAINER_HPP

#include "BaseObjectProcessor.hpp"

namespace mwmp
{
    inline ESM::Cell makeContainerPacketCell(const MWWorld::Cell& cell)
    {
        ESM::Cell packetCell;

        if (cell.isExterior())
        {
            packetCell.mData.mX = cell.getGridX();
            packetCell.mData.mY = cell.getGridY();
        }
        else
        {
            packetCell.mData.mFlags = ESM::Cell::Interior;
            packetCell.mName = std::string(cell.getNameId());
        }

        packetCell.mRegion = cell.getRegion();
        packetCell.updateId();
        return packetCell;
    }

    class ProcessorContainer final: public BaseObjectProcessor
    {
    public:
        ProcessorContainer()
        {
            BPP_INIT(ID_CONTAINER)
        }

        virtual void Do(ObjectPacket &packet, ObjectList &objectList)
        {
            BaseObjectProcessor::Do(packet, objectList);

            ptrCellStore = Main::get().getCellController()->getCellStore(objectList.cell);

            if (!ptrCellStore) return;

            if (objectList.action == mwmp::BaseObjectList::SET
                && objectList.containerSubAction != mwmp::BaseObjectList::NONE
                && objectList.containerSubAction != mwmp::BaseObjectList::REPLY_TO_REQUEST)
                objectList.containerSubAction = mwmp::BaseObjectList::NONE;

            std::string debugMessage = "- action ";
            unsigned char action = objectList.action;
            unsigned char containerSubAction = objectList.containerSubAction;

            if (action == mwmp::BaseObjectList::SET)
                debugMessage += "SET";
            else if (action == mwmp::BaseObjectList::ADD)
                debugMessage += "ADD";
            else if (action == mwmp::BaseObjectList::REMOVE)
                debugMessage += "REMOVE";
            else if (action == mwmp::BaseObjectList::REQUEST)
                debugMessage += "REQUEST";

            debugMessage += " and subaction ";

            if (containerSubAction == mwmp::BaseObjectList::NONE)
                debugMessage += "NONE";
            else if (containerSubAction == mwmp::BaseObjectList::DRAG)
                debugMessage += "DRAG";
            else if (containerSubAction == mwmp::BaseObjectList::DROP)
                debugMessage += "DROP";
            else if (containerSubAction == mwmp::BaseObjectList::TAKE_ALL)
                debugMessage += "TAKE_ALL";
            else if (containerSubAction == mwmp::BaseObjectList::REPLY_TO_REQUEST)
                debugMessage += "REPLY_TO_REQUEST";
            else if (containerSubAction == mwmp::BaseObjectList::BARTER)
                debugMessage += "BARTER";
            else if (containerSubAction == mwmp::BaseObjectList::LOCK_REQUEST)
                debugMessage += "LOCK_REQUEST";
            else if (containerSubAction == mwmp::BaseObjectList::LOCK_RELEASE)
                debugMessage += "LOCK_RELEASE";

            LOG_APPEND(TimedLog::LOG_VERBOSE, "%s", debugMessage.c_str());

            if (objectList.action == mwmp::BaseObjectList::REQUEST
                && objectList.containerSubAction == mwmp::BaseObjectList::LOCK_REQUEST)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Server granted exclusive container interaction");
                objectList.openGrantedContainers(ptrCellStore);
                return;
            }

            // If we've received a request for information, comply with it
            if (objectList.action == mwmp::BaseObjectList::REQUEST)
            {
                if (objectList.baseObjectCount == 0)
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Request had no objects attached, so we are sending all containers in the cell %s",
                        objectList.cell.getDescription().c_str());
                    objectList.reset();
                    objectList.packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList.originClientScript.clear();
                    objectList.cell = makeContainerPacketCell(*ptrCellStore->getCell());
                    objectList.action = mwmp::BaseObjectList::SET;
                    objectList.containerSubAction = mwmp::BaseObjectList::REPLY_TO_REQUEST;
                    objectList.addAllContainers(ptrCellStore);
                    objectList.sendContainer();
                }
                else
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Request was for %i %s", objectList.baseObjectCount, objectList.baseObjectCount == 1 ? "object" : "objects");
                    std::vector<BaseObject> requestObjects = objectList.baseObjects;
                    objectList.reset();
                    objectList.packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList.originClientScript.clear();
                    objectList.cell = makeContainerPacketCell(*ptrCellStore->getCell());
                    objectList.action = mwmp::BaseObjectList::SET;
                    objectList.containerSubAction = mwmp::BaseObjectList::REPLY_TO_REQUEST;
                    objectList.addRequestedContainers(ptrCellStore, requestObjects);

                    if (objectList.baseObjects.size() > 0)
                        objectList.sendContainer();
                }
            }
            // Otherwise, edit containers based on the information received
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Editing container contents to match those of packet", objectList.baseObjectCount);
                objectList.editContainers(ptrCellStore);
            }
        }

    };
}

#endif //OPENMW_PROCESSORCONTAINER_HPP

