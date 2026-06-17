#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorEquipment.hpp"

using namespace mwmp;

PacketActorEquipment::PacketActorEquipment() : ActorPacket()
{
    packetID = ID_ACTOR_EQUIPMENT;
}

void PacketActorEquipment::Actor(BaseActor &actor, bool send)
{
    if (!RW(actor.equipmentSequence, send))
        return;

    for (auto &&equipmentItem : actor.equipmentItems)
    {
        if (!RW(equipmentItem.refId, send)
            || !RW(equipmentItem.count, send)
            || !RW(equipmentItem.charge, send)
            || !RW(equipmentItem.enchantmentCharge, send))
            return;

        if (!send && !isValidEquipmentItem(equipmentItem))
        {
            packetValid = false;
            return;
        }
    }

    if (!send)
        actor.hasEquipmentData = true;
}
