#include "../Networking.hpp"
#include "SystemProcessor.hpp"
#include "../Main.hpp"
#include <components/openmw-mp/Transport/ReceivedPacket.hpp>

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

SystemProcessor::~SystemProcessor()
{

}

bool SystemProcessor::Process(ReceivedPacket& packet)
{
    PacketStream bsIn(&packet.data()[1], packet.length());
    if (!readPacketGuid(bsIn, guid))
        return false;

    SystemPacket *myPacket = Main::get().getNetworking()->getSystemPacket(packet.id());
    myPacket->SetReadStream(&bsIn);

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == packet.id())
        {
            myGuid = Main::get().getLocalSystem()->guid;
            request = packet.length() == myPacket->headerSize();

            BaseSystem *system = 0;
            system = Main::get().getLocalSystem();

            if (!request && !processor.second->avoidReading && system != 0)
            {
                myPacket->setSystem(system);
                myPacket->Read();
            }

            processor.second->Do(*myPacket, system);
            return true;
        }
    }
    return false;
}

