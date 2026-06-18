#ifndef OPENMW_MP_SERVER_NETWORKING_HPP
#define OPENMW_MP_SERVER_NETWORKING_HPP

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Transport/PacketDestination.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>
#include <memory>
#include "Player.hpp"

class MasterClient;
namespace  mwmp
{
    class PacketTransport;
    class ReceivedPacket;
    class ServerSimulation;

    class ServerNetworking
    {
    public:
        explicit ServerNetworking(PacketTransport *transport);
        ~ServerNetworking();

        void newPlayer(PacketGuid guid);
        void disconnectPlayer(PacketGuid guid);
        void kickPlayer(PacketGuid guid, bool sendNotification = true);
        
        void banAddress(const char *ipAddress);
        void unbanAddress(const char *ipAddress);
        PacketAddress getPacketAddress(PacketGuid guid);

        void processSystemPacket(ReceivedPacket* packet);
        void processPlayerPacket(ReceivedPacket* packet);
        void processActorPacket(ReceivedPacket* packet);
        void processObjectPacket(ReceivedPacket* packet);
        void processWorldstatePacket(ReceivedPacket* packet);
        void update(ReceivedPacket* packet, PacketStream &bsIn);

        unsigned short numberOfConnections() const;
        unsigned int maxConnections() const;
        int getAvgPing(const PacketDestination& destination) const;
        unsigned short getPort() const;

        int mainLoop();

        void stopServer(int code);

        SystemPacketController *getSystemPacketController() const;
        PlayerPacketController *getPlayerPacketController() const;
        ActorPacketController *getActorPacketController() const;
        ObjectPacketController *getObjectPacketController() const;
        WorldstatePacketController *getWorldstatePacketController() const;
        ServerSimulation& getServerSimulation();

        BaseActorList *getReceivedActorList();
        BaseObjectList *getReceivedObjectList();
        BaseWorldstate *getReceivedWorldstate();

        int getCurrentMpNum() const;
        void setCurrentMpNum(int value);
        int incrementMpNum();

        bool getDataFileEnforcementState() const;
        void setDataFileEnforcementState(bool state);

        bool getScriptErrorIgnoringState() const;
        void setScriptErrorIgnoringState(bool state);
        bool usesNativeServerPolicies() const;
        void setNativeServerPoliciesEnabled(bool enabled);

        MasterClient *getMasterClient();
        void InitQuery(std::string queryAddr, unsigned short queryPort);
        void setServerPassword(std::string passw) noexcept;
        bool isPassworded() const;

        static const ServerNetworking &get();
        static ServerNetworking *getPtr();

        void postInit();

        PacketPreInit::PluginContainer &getSamples();
        bool usesNativeDataFileRegistry() const;
    private:
        bool loadDataFileRequirementsFromRegistry();
        bool preInit(ReceivedPacket* packet, PacketStream &bsIn);
        void processLoadedPlayer(Player* player);
        std::string serverPassword;
        std::string expectedVersion;
        uint32_t expectedProtocolVersion;
        std::string expectedCommitHash;
        static ServerNetworking *sThis;

        PacketTransport *transport;
        PacketStream bsOut;
        TPlayers *players;
        MasterClient *mclient;

        BaseSystem baseSystem;
        BaseActorList baseActorList;
        BaseObjectList baseObjectList;
        BaseWorldstate baseWorldstate;

        SystemPacketController *systemPacketController;
        PlayerPacketController *playerPacketController;
        ActorPacketController *actorPacketController;
        ObjectPacketController *objectPacketController;
        WorldstatePacketController *worldstatePacketController;
        std::unique_ptr<ServerSimulation> serverSimulation;

        bool running;
        int exitCode;
        PacketPreInit::PluginContainer samples;
        int currentMpNum = 0;
        bool dataFileEnforcementState = true;
        bool scriptErrorIgnoringState = false;
        bool nativeDataFileRegistryLoaded = false;
        bool nativeServerPoliciesEnabled = false;
    };
}


#endif // OPENMW_MP_SERVER_NETWORKING_HPP
