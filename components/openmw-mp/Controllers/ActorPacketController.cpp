#include "../Packets/Actor/PacketActorList.hpp"
#include "../Packets/Actor/PacketActorAuthority.hpp"
#include "../Packets/Actor/PacketActorTest.hpp"
#include "../Packets/Actor/PacketActorAI.hpp"
#include "../Packets/Actor/PacketActorAnimFlags.hpp"
#include "../Packets/Actor/PacketActorAnimPlay.hpp"
#include "../Packets/Actor/PacketActorAttack.hpp"
#include "../Packets/Actor/PacketActorCast.hpp"
#include "../Packets/Actor/PacketActorCellChange.hpp"
#include "../Packets/Actor/PacketActorDeath.hpp"
#include "../Packets/Actor/PacketActorEquipment.hpp"
#include "../Packets/Actor/PacketActorPosition.hpp"
#include "../Packets/Actor/PacketActorSpeech.hpp"
#include "../Packets/Actor/PacketActorSpellsActive.hpp"
#include "../Packets/Actor/PacketActorStatsDynamic.hpp"


#include "ActorPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::ActorPacketController::packets_t *packets)
{
    T *packet = new T();
    typedef mwmp::ActorPacketController::packets_t::value_type value_t;
    packets->insert(value_t(packet->GetPacketID(), value_t::second_type(packet)));
}

mwmp::ActorPacketController::ActorPacketController()
{
    AddPacket<PacketActorList>(&packets);
    AddPacket<PacketActorAuthority>(&packets);
    AddPacket<PacketActorTest>(&packets);
    AddPacket<PacketActorAI>(&packets);
    AddPacket<PacketActorAnimFlags>(&packets);
    AddPacket<PacketActorAnimPlay>(&packets);
    AddPacket<PacketActorAttack>(&packets);
    AddPacket<PacketActorCast>(&packets);
    AddPacket<PacketActorCellChange>(&packets);
    AddPacket<PacketActorDeath>(&packets);
    AddPacket<PacketActorEquipment>(&packets);
    AddPacket<PacketActorPosition>(&packets);
    AddPacket<PacketActorSpeech>(&packets);
    AddPacket<PacketActorSpellsActive>(&packets);
    AddPacket<PacketActorStatsDynamic>(&packets);
}


mwmp::ActorPacket *mwmp::ActorPacketController::GetPacket(PacketId id)
{
    return packets[id].get();
}

void mwmp::ActorPacketController::SetStream(PacketStream *inStream, PacketStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

bool mwmp::ActorPacketController::ContainsPacket(PacketId id)
{
    for(const auto &packet : packets)
    {
        if (packet.first == id)
            return true;
    }
    return false;
}
