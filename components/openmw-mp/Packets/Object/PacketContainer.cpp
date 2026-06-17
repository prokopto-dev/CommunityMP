#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketContainer.hpp"

#include <cmath>

using namespace mwmp;

namespace
{
    constexpr int maxContainerItemStackCount = 1000000;

    bool isValidContainerAction(unsigned char action)
    {
        return action <= BaseObjectList::REQUEST;
    }

    bool isValidContainerSubAction(unsigned char subAction)
    {
        return subAction <= BaseObjectList::LOCK_RELEASE;
    }

    bool isContainerLockSubAction(unsigned char subAction)
    {
        return subAction == BaseObjectList::LOCK_REQUEST || subAction == BaseObjectList::LOCK_RELEASE;
    }

    bool hasObjectIdentity(const BaseObject& object)
    {
        return !object.refId.empty() || object.refNum != 0 || object.mpNum != 0;
    }

    bool isValidContainerItem(unsigned char action, const ContainerItem& item)
    {
        if (!std::isfinite(item.enchantmentCharge))
            return false;

        if (action == BaseObjectList::REQUEST)
            return false;

        if (item.refId.empty() || item.refId.find("$dynamic") != std::string::npos)
            return false;

        if (action == BaseObjectList::SET || action == BaseObjectList::ADD)
            return item.count > 0 && item.count <= maxContainerItemStackCount;

        if (action == BaseObjectList::REMOVE)
            return item.actionCount > 0 && item.actionCount <= maxContainerItemStackCount;

        return false;
    }
}

bool mwmp::isContainerPacketAllowedFromClient(const BaseObjectList& objectList)
{
    if (objectList.packetOrigin == SERVER_SCRIPT)
        return false;

    if (objectList.action == BaseObjectList::SET)
        return objectList.containerSubAction == BaseObjectList::REPLY_TO_REQUEST;

    if (objectList.action == BaseObjectList::REQUEST)
    {
        if (!isContainerLockSubAction(objectList.containerSubAction) || objectList.baseObjects.empty())
            return false;

        for (const BaseObject& object : objectList.baseObjects)
        {
            if (!hasObjectIdentity(object) || object.containerItemCount != 0 || !object.containerItems.empty())
                return false;
        }

        return true;
    }

    return objectList.action == BaseObjectList::ADD || objectList.action == BaseObjectList::REMOVE;
}

PacketContainer::PacketContainer() : ObjectPacket()
{
    packetID = ID_CONTAINER;
    hasCellData = true;
}

void PacketContainer::Packet(PacketStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    if (!RW(objectList->action, send) || !RW(objectList->containerSubAction, send))
    {
        objectList->isValid = false;
        return;
    }

    if (!send && (!isValidContainerAction(objectList->action)
        || !isValidContainerSubAction(objectList->containerSubAction)))
    {
        objectList->isValid = false;
        return;
    }

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
        {
            baseObject = objectList->baseObjects.at(i);
            baseObject.containerItemCount = (unsigned int)(baseObject.containerItems.size());
        }
        else
        {
            baseObject.containerItemCount = 0;
            baseObject.containerItems.clear();
        }

        Object(baseObject, send);

        if (!packetValid || !RW(baseObject.containerItemCount, send))
        {
            objectList->isValid = false;
            return;
        }

        const bool isRequest = objectList->action == BaseObjectList::REQUEST;
        const bool isLockRequest = isRequest && isContainerLockSubAction(objectList->containerSubAction);
        if (baseObject.containerItemCount > maxObjects
            || (!isRequest && !hasObjectIdentity(baseObject))
            || (isLockRequest && !hasObjectIdentity(baseObject))
            || (isRequest && baseObject.containerItemCount != 0))
        {
            objectList->isValid = false;
            return;
        }

        ContainerItem containerItem;

        for (unsigned int j = 0; j < baseObject.containerItemCount; j++)
        {
            if (send)
                containerItem = baseObject.containerItems.at(j);

            if (!RW(containerItem.refId, send, true)
                || !RW(containerItem.count, send)
                || !RW(containerItem.charge, send)
                || !RW(containerItem.enchantmentCharge, send)
                || !RW(containerItem.soul, send, true)
                || !RW(containerItem.actionCount, send))
            {
                objectList->isValid = false;
                return;
            }

            if (!send)
            {
                if (!isValidContainerItem(objectList->action, containerItem))
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                        "Skipping invalid container item %s with count %i and actionCount %i",
                        containerItem.refId.c_str(), containerItem.count, containerItem.actionCount);
                    continue;
                }

                baseObject.containerItems.push_back(containerItem);
            }
        }
        if (!send)
        {
            baseObject.containerItemCount = static_cast<unsigned int>(baseObject.containerItems.size());
            objectList->baseObjects.push_back(baseObject);
        }
    }
}
