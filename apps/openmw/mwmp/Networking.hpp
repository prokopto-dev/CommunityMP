#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <memory>
#include <string>

#include <components/openmw-mp/NetworkMessages.hpp>

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

#include <components/files/collections.hpp>

#include "LocalSystem.hpp"
#include "ActorList.hpp"
#include "ObjectList.hpp"
#include "Worldstate.hpp"

namespace mwmp
{
    class LocalPlayer;
    class GnsTransport;
    class PacketTransport;
    class ReceivedPacket;

    class Networking
    {
    public:
        Networking();
        ~Networking();
        void connect(const std::string& ip, unsigned short port, std::vector<std::string> &content, Files::Collections &collections);
        void update();

        SystemPacket *getSystemPacket(PacketId id);
        PlayerPacket *getPlayerPacket(PacketId id);
        ActorPacket *getActorPacket(PacketId id);
        ObjectPacket *getObjectPacket(PacketId id);
        WorldstatePacket *getWorldstatePacket(PacketId id);

        PacketAddress serverAddress()
        {
            return serverAddr;
        }

        bool isConnected();

        LocalSystem *getLocalSystem();
        LocalPlayer *getLocalPlayer();
        ActorList *getActorList();
        ObjectList *getObjectList();
        Worldstate *getWorldstate();

    private:
        bool connected;
        std::unique_ptr<GnsTransport> transport;
        PacketAddress serverAddr;
        PacketStream bsOut;

        SystemPacketController systemPacketController;
        PlayerPacketController playerPacketController;
        ActorPacketController actorPacketController;
        ObjectPacketController objectPacketController;
        WorldstatePacketController worldstatePacketController;

        ActorList actorList;
        ObjectList objectList;
        Worldstate worldstate;

        void receiveMessage(ReceivedPacket* packet);

        void preInit(std::vector<std::string> &content, Files::Collections &collections);
    };
}


#endif //OPENMW_NETWORKING_HPP
