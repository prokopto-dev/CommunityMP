#include "Player.hpp"
#include "processors/ProcessorInitializer.hpp"

#include <algorithm>
#include <cctype>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/ServerPassword.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketTransport.hpp>
#include <components/version/version.hpp>

#include <iostream>
#include <Script/Script.hpp>
#include <Script/API/TimerAPI.hpp>
#include <chrono>
#include <thread>
#include <csignal>
#include <limits>
#include <string_view>

#include "ServerNetworking.hpp"
#include "MasterClient.hpp"
#include "CommunityMpClientLuaEventHandler.hpp"
#include "CommunityMpLuaEventSender.hpp"
#include "PlayerQuestStateStore.hpp"
#include "ServerContentRegistry.hpp"
#include "ServerEventDispatcher.hpp"
#include "ServerSimulation.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "ConsoleInput.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"

using namespace mwmp;

ServerNetworking *ServerNetworking::sThis = 0;

static int currentMpNum = 0;
static bool dataFileEnforcementState = true;
static bool scriptErrorIgnoringState = false;
bool killLoop = false;

namespace
{
    bool isBlank(std::string_view value)
    {
        return std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isspace(character) != 0;
        });
    }

    void sendConnectionStatus(PacketTransport* transport, ReceivedPacket* packet, PacketId messageId)
    {
        PacketStream bs;
        bs.Write(messageId);

        transport->send(bs.data(), bs.size(), PacketPriority::High, PacketReliability::ReliableOrdered, CHANNEL_SYSTEM,
            packet->destination(), false);
    }

    void logCompatibleBuildMetadataDifference(const PacketPreInit& packetPreInit, const std::string& expectedVersion,
        const std::string& expectedCommitHash)
    {
        if (packetPreInit.getVersion() == expectedVersion && packetPreInit.getCommitHash() == expectedCommitHash)
            return;

        LOG_APPEND(TimedLog::LOG_WARN,
            "- Client build metadata differs, but protocol version matches; allowing connection");
        LOG_APPEND(TimedLog::LOG_WARN, "- Client version: %s, server version: %s",
            packetPreInit.getVersion().c_str(), expectedVersion.c_str());
        LOG_APPEND(TimedLog::LOG_WARN, "- Client commit: %s, server commit: %s",
            packetPreInit.getCommitHash().c_str(), expectedCommitHash.c_str());
    }

    bool getPlayerForGameplayPacket(ReceivedPacket* packet, Player*& player, const char* packetCategory)
    {
        player = Players::getPlayer(packet->guid());
        if (player != nullptr)
            return true;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Ignoring %s packet %u from unknown player session at %s",
            packetCategory, static_cast<unsigned int>(packet->id()),
            packetAddressToString(packet->address(), true).c_str());
        return false;
    }

    bool appendUniqueHash(mwmp::PacketPreInit::HashList& hashes, std::uint32_t checksum)
    {
        if (std::find(hashes.begin(), hashes.end(), checksum) != hashes.end())
            return false;

        hashes.push_back(checksum);
        return true;
    }

    bool tryParseChecksum(std::string_view value, std::uint32_t& checksum)
    {
        if (isBlank(value))
            return false;

        try
        {
            std::size_t parsedLength = 0;
            const unsigned long parsed = std::stoul(std::string(value), &parsedLength, 0);
            if (parsedLength != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
                return false;

            checksum = static_cast<std::uint32_t>(parsed);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}

ServerNetworking::ServerNetworking(PacketTransport *transport) : mclient(nullptr)
{
    sThis = this;
    this->transport = transport;
    BasePacket::SetPacketTransport(transport);
    players = Players::getPlayers();
    expectedVersion = TES3MP_VERSION;
    expectedProtocolVersion = TES3MP_PROTO_VERSION;
    expectedCommitHash = Version::getCommitHash();
    expectedCommitHash.erase(std::remove(expectedCommitHash.begin(), expectedCommitHash.end(), '\r'),
        expectedCommitHash.end());

    CellController::create();

    systemPacketController = new SystemPacketController();
    playerPacketController = new PlayerPacketController();
    actorPacketController = new ActorPacketController();
    objectPacketController = new ObjectPacketController();
    worldstatePacketController = new WorldstatePacketController();
    serverSimulation = std::make_unique<ServerSimulation>();

    // Set send stream
    systemPacketController->SetStream(0, &bsOut);
    playerPacketController->SetStream(0, &bsOut);
    actorPacketController->SetStream(0, &bsOut);
    objectPacketController->SetStream(0, &bsOut);
    worldstatePacketController->SetStream(0, &bsOut);

    running = true;
    exitCode = 0;

    mwmp::ServerEvents::serverInit();

    serverPassword = TES3MP_DEFAULT_PASSW;

    ProcessorInitializer();
}

ServerNetworking::~ServerNetworking()
{
    mwmp::ServerEvents::serverExit(false);

    CellController::destroy();

    sThis = 0;
    BasePacket::SetPacketTransport(nullptr);
    delete systemPacketController;
    delete playerPacketController;
    delete actorPacketController;
    delete objectPacketController;
    delete worldstatePacketController;
}

void ServerNetworking::setServerPassword(std::string password) noexcept
{
    serverPassword = normalizeServerPassword(password);
}

bool ServerNetworking::isPassworded() const
{
    return isServerPassworded(serverPassword);
}

void ServerNetworking::processSystemPacket(ReceivedPacket* packet)
{
    Player *player = Players::getPlayer(packet->guid());
    if (player == nullptr)
        return;

    SystemPacket *myPacket = systemPacketController->GetPacket(packet->id());

    if (packet->id() == ID_SYSTEM_HANDSHAKE)
    {
        myPacket->setSystem(&baseSystem);
        myPacket->Read();

        if (!myPacket->isPacketValid())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid handshake packet from client at %s",
                packetAddressToString(packet->address(), true).c_str());
            kickPlayer(player->guid);
            return;
        }

        if (player->isHandshaked())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Wrong handshake with client at %s",
                packetAddressToString(packet->address(), true).c_str());
            kickPlayer(player->guid);
            return;
        }

        const ServerPasswordValidation passwordValidation = validateServerPassword(serverPassword, baseSystem.serverPassword);
        if (passwordValidation == ServerPasswordValidation::Rejected)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Wrong server password %s used by client at %s",
                baseSystem.serverPassword.c_str(), packetAddressToString(packet->address(), true).c_str());
            kickPlayer(player->guid);
            return;
        }
        if (passwordValidation == ServerPasswordValidation::AcceptedWithExtraClientPassword)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Client at %s tried to join using password, despite the server not being passworded",
                packetAddressToString(packet->address(), true).c_str());
        }

        if (isBlank(baseSystem.playerName))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s tried to join without an account name",
                packetAddressToString(packet->address(), true).c_str());
            kickPlayer(player->guid);
            return;
        }

        player->setLoginName(baseSystem.playerName.substr(0, 35));
        player->setLoginPasswordHash(baseSystem.accountPasswordHash);
        player->setHandshake();
        if (player->hasPendingLoaded())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Processing deferred ID_LOADED for handshaked client at %s",
                packetAddressToString(packet->address(), true).c_str());
            player->setPendingLoaded(false);
            processLoadedPlayer(player);
        }
        return;
    }
}

void ServerNetworking::processLoadedPlayer(Player* player)
{
    if (player == nullptr || player->getLoadState() != Player::NOTLOADED)
        return;

    if (!player->getLoginName().empty())
        player->npc.mName = player->getLoginName();

    player->setLoadState(Player::LOADED);

    unsigned short pid = player->getId();
    mwmp::ServerEvents::playerConnect(pid);

    if (player->getLoadState() == Player::KICKED)
    {
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(false);
        Players::deletePlayer(player->guid);
        return;
    }

    if (player->getLoadState() == Player::LOADED)
    {
        player->setLoadState(Player::POSTLOADED);
        newPlayer(player->guid);
    }
}

void ServerNetworking::processPlayerPacket(ReceivedPacket* packet)
{
    Player *player = nullptr;
    if (!getPlayerForGameplayPacket(packet, player, "PlayerPacket"))
        return;

    PlayerPacket *myPacket = playerPacketController->GetPacket(packet->id());

    if (!player->isHandshaked())
    {
        if (packet->id() == ID_LOADED)
        {
            if (!player->hasPendingLoaded())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Deferring ID_LOADED until system handshake completes for %s",
                    packetAddressToString(packet->address(), true).c_str());
                player->setPendingLoaded(true);
            }
            return;
        }

        player->incrementHandshakeAttempts();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Have not completed handshake with client at %s",
            packetAddressToString(packet->address(), true).c_str());
        LOG_APPEND(TimedLog::LOG_WARN, "- Attempts so far: %i", player->getHandshakeAttempts());

        if (player->getHandshakeAttempts() > 20)
            kickPlayer(player->guid, false);
        else if (player->getHandshakeAttempts() > 5)
            kickPlayer(player->guid, true);

        return;
    }

    if (packet->id() == ID_LOADED)
    {
        processLoadedPlayer(player);
        return;
    }
    else if (packet->id() == ID_PLAYER_BASEINFO)
    {
        myPacket->setPlayer(player);
        myPacket->Read();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Received ID_PLAYER_BASEINFO about %s: race=%s, head=%s, hair=%s, birthsign=%s",
            player->npc.mName.c_str(),
            player->npc.mRace.serializeText().c_str(),
            player->npc.mHead.serializeText().c_str(),
            player->npc.mHair.serializeText().c_str(),
            player->birthsign.c_str());
        if (player->getLoadState() == Player::POSTLOADED)
            mwmp::ServerEvents::playerBaseInfo(player->getId());
        myPacket->Send(true);
        return;
    }

    if (player->getLoadState() == Player::NOTLOADED)
        return;
    if (!PlayerProcessor::Process(*packet))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->id());

}

void ServerNetworking::processActorPacket(ReceivedPacket* packet)
{
    Player *player = nullptr;
    if (!getPlayerForGameplayPacket(packet, player, "ActorPacket"))
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ActorProcessor::Process(*packet, baseActorList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->id());

}

void ServerNetworking::processObjectPacket(ReceivedPacket* packet)
{
    Player *player = nullptr;
    if (!getPlayerForGameplayPacket(packet, player, "ObjectPacket"))
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ObjectProcessor::Process(*packet, baseObjectList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->id());

}

void ServerNetworking::processWorldstatePacket(ReceivedPacket* packet)
{
    Player *player = nullptr;
    if (!getPlayerForGameplayPacket(packet, player, "WorldstatePacket"))
        return;

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!WorldstateProcessor::Process(*packet, baseWorldstate))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->id());

}

bool ServerNetworking::preInit(ReceivedPacket* packet, PacketStream &bsIn)
{
    if (packet->id() != ID_GAME_PREINIT)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "%s sent wrong first packet (ID_GAME_PREINIT was expected)",
                           packetAddressToString(packet->address(), true).c_str());
        transport->closeConnection(packet->destination(), true);
        return false;
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_GAME_PREINIT from %s",
        packetAddressToString(packet->address(), true).c_str());
    PacketPreInit::PluginContainer dataFiles;

    PacketPreInit packetPreInit;
    packetPreInit.SetReadStream(&bsIn);
    packetPreInit.setChecksums(&dataFiles);
    packetPreInit.Read();

    if (!packetPreInit.isPacketValid() || dataFiles.empty())
    {
        LOG_APPEND(TimedLog::LOG_ERROR, "- Packet was invalid");
        sendConnectionStatus(transport, packet, ID_INVALID_PASSWORD);
        transport->closeConnection(packet->destination(), true);
        return false;
    }

    if (packetPreInit.getProtocolVersion() != expectedProtocolVersion)
    {
        LOG_APPEND(TimedLog::LOG_WARN, "- Client was not allowed to connect due to protocol version mismatch");
        LOG_APPEND(TimedLog::LOG_WARN, "- Client protocol: %u, server protocol: %u",
            packetPreInit.getProtocolVersion(), expectedProtocolVersion);
        sendConnectionStatus(transport, packet, ID_INCOMPATIBLE_PROTOCOL_VERSION);
        transport->closeConnection(packet->destination(), true);
        return false;
    }

    logCompatibleBuildMetadataDifference(packetPreInit, expectedVersion, expectedCommitHash);

    auto dataFile = dataFiles.begin();
    if (samples.size() == dataFiles.size())
    {
        for (int i = 0; dataFile != dataFiles.end(); dataFile++, i++)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- idx: %i\tchecksum: %X\tfile: %s", i, dataFile->second[0], dataFile->first.c_str());
            // Check if the filenames match, ignoring case
            if (Misc::StringUtils::ciEqual(samples[i].first, dataFile->first))
            {
                auto &hashList = samples[i].second;
                // Proceed if no checksums have been listed for this dataFile on the server
                if (hashList.empty())
                    continue;
                auto it = find(hashList.begin(), hashList.end(), dataFile->second[0]);
                // Break the loop if the client's checksum isn't among those accepted by
                // the server
                if (it == hashList.end())
                    break;
            }
            else // name is incorrect
                break;
        }
    }
    PacketStream bs;
    packetPreInit.SetSendStream(&bs);

    // If the loop above was broken, then the client's data files do not match the server's
    if (dataFileEnforcementState && dataFile != dataFiles.end())
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was not allowed to connect due to incompatible data files");
        packetPreInit.setChecksums(&samples);
        packetPreInit.setProtocolVersionInfo(expectedVersion, expectedProtocolVersion, expectedCommitHash);
        packetPreInit.Send(packet->address());
        transport->closeConnection(packet->destination(), true);
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was allowed to connect");
        PacketPreInit::PluginContainer tmp;
        packetPreInit.setChecksums(&tmp);
        packetPreInit.setProtocolVersionInfo(expectedVersion, expectedProtocolVersion, expectedCommitHash);
        packetPreInit.Send(packet->address());
        Players::newPlayer(packet->guid()); // create player if connection allowed
        systemPacketController->SetStream(&bsIn, nullptr); // and request handshake
        const uint32_t handshakeRequestResult = systemPacketController->GetPacket(ID_SYSTEM_HANDSHAKE)->RequestData(packet->guid());
        LOG_APPEND(TimedLog::LOG_INFO, "- Requested handshake from client: %u", handshakeRequestResult);
        return true;
    }

    return false;
}

void ServerNetworking::update(ReceivedPacket* packet, PacketStream &bsIn)
{
    if (systemPacketController->ContainsPacket(packet->id()))
    {
        systemPacketController->SetStream(&bsIn, nullptr);
        processSystemPacket(packet);
    }
    else if (playerPacketController->ContainsPacket(packet->id()))
    {
        playerPacketController->SetStream(&bsIn, nullptr);
        processPlayerPacket(packet);
    }
    else if (actorPacketController->ContainsPacket(packet->id()))
    {
        actorPacketController->SetStream(&bsIn, 0);
        processActorPacket(packet);
    }
    else if (objectPacketController->ContainsPacket(packet->id()))
    {
        objectPacketController->SetStream(&bsIn, 0);
        processObjectPacket(packet);
    }
    else if (worldstatePacketController->ContainsPacket(packet->id()))
    {
        worldstatePacketController->SetStream(&bsIn, 0);
        processWorldstatePacket(packet);
    }
    else
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled packet with identifier %i has arrived", packet->id());
}

void ServerNetworking::newPlayer(PacketGuid guid)
{
    playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_POSITION)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_ANIM_FLAGS)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_SHAPESHIFT)->RequestData(guid);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Sending info about other players to %llu",
        static_cast<unsigned long long>(packetGuidValue(guid)));

    for (TPlayers::iterator pl = players->begin(); pl != players->end(); pl++) //sending other players to new player
    {
        // If we are iterating over the new player, don't send the packets below
        if (pl->first == guid) continue;

        // If an invalid key makes it into the Players map, ignore it
        else if (!isPacketGuidAssigned(pl->first)) continue;

        // if player not fully connected
        else if (pl->second == nullptr) continue;

        // If we are iterating over a player who has inputted their name, proceed
        else if (pl->second->getLoadState() == Player::POSTLOADED)
        {
            const bool previousExchangeFullInfo = pl->second->exchangeFullInfo;
            pl->second->exchangeFullInfo = true;

            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(pl->second);

            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_SHAPESHIFT)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->SendWithReliability(
                guid, PacketReliability::ReliableOrdered);
            playerPacketController->GetPacket(ID_PLAYER_ANIM_FLAGS)->SendWithReliability(
                guid, PacketReliability::ReliableOrdered);

            pl->second->exchangeFullInfo = previousExchangeFullInfo;
        }
    }

    LOG_APPEND(TimedLog::LOG_WARN, "- Done");

}

void ServerNetworking::disconnectPlayer(PacketGuid guid)
{
    Player *player = Players::getPlayer(guid);
    if (!player)
        return;
    mwmp::ServerEvents::playerDisconnect(player->getId());

    player->setLoadState(Player::KICKED);
    serverSimulation->removePlayer(guid);
    CellController::get()->deletePlayer(player);

    playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
    playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(true);
    CommunityMpClientLuaEventHandler::clearPlayer(guid);
    CommunityMpLuaEventSender::clearPlayer(guid);
    PlayerQuestStateStore::get().clearPlayer(guid);
    Players::deletePlayer(guid);
}

PlayerPacketController *ServerNetworking::getPlayerPacketController() const
{
    return playerPacketController;
}

ActorPacketController *ServerNetworking::getActorPacketController() const
{
    return actorPacketController;
}

ObjectPacketController *ServerNetworking::getObjectPacketController() const
{
    return objectPacketController;
}

WorldstatePacketController *ServerNetworking::getWorldstatePacketController() const
{
    return worldstatePacketController;
}

ServerSimulation& ServerNetworking::getServerSimulation()
{
    return *serverSimulation;
}

BaseActorList *ServerNetworking::getReceivedActorList()
{
    return &baseActorList;
}

BaseObjectList *ServerNetworking::getReceivedObjectList()
{
    return &baseObjectList;
}

BaseWorldstate *ServerNetworking::getReceivedWorldstate()
{
    return &baseWorldstate;
}

int ServerNetworking::getCurrentMpNum()
{
    return currentMpNum;
}

void ServerNetworking::setCurrentMpNum(int value)
{
    currentMpNum = value;
}

int ServerNetworking::incrementMpNum()
{
    currentMpNum++;
    mwmp::ServerEvents::mpNumIncrement(currentMpNum);
    return currentMpNum;
}

bool ServerNetworking::getDataFileEnforcementState()
{
    return dataFileEnforcementState;
}

void ServerNetworking::setDataFileEnforcementState(bool state)
{
    dataFileEnforcementState = state;
}

bool ServerNetworking::getScriptErrorIgnoringState()
{
    return scriptErrorIgnoringState;
}

void ServerNetworking::setScriptErrorIgnoringState(bool state)
{
    scriptErrorIgnoringState = state;
}

const ServerNetworking &ServerNetworking::get()
{
    return *sThis;
}


ServerNetworking *ServerNetworking::getPtr()
{
    return sThis;
}

PacketAddress ServerNetworking::getPacketAddress(PacketGuid guid)
{
    return transport->getPacketAddress(guid);
}

void ServerNetworking::stopServer(int code)
{
    running = false;
    exitCode = code;
}

void signalHandler(int signum) 
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    //15 is SIGTERM(Normal OS stop call), 2 is SIGINT(Ctrl+C)
    if(signum == 15 || signum == 2)
    {
        killLoop = true;
    }
}

int ServerNetworking::mainLoop()
{
    ReceivedPacket* receivedPacket;

#ifndef _WIN32
    struct sigaction sigIntHandler;
    
    sigIntHandler.sa_handler = signalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
#endif
    
    while (running && !killLoop)
    {
#ifndef _WIN32
        sigaction(SIGTERM, &sigIntHandler, NULL);
        sigaction(SIGINT, &sigIntHandler, NULL);
#endif
        if (ConsoleInput::consumeEnterPress())
            break;
        for (receivedPacket = transport->receive(); receivedPacket;
             transport->deallocatePacket(receivedPacket), receivedPacket = transport->receive())
        {
            if (getMasterClient() && getMasterClient()->Process(receivedPacket))
                continue;

            switch (receivedPacket->id())
            {
                case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has disconnected",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    break;
                case ID_REMOTE_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has lost connection",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    break;
                case ID_REMOTE_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has connected",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    break;
                case ID_CONNECTION_REQUEST_ACCEPTED:    // client to server
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Our connection request has been accepted");
                    break;
                }
                case ID_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "A connection is incoming from %s",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    break;
                case ID_NO_FREE_INCOMING_CONNECTIONS:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "The server is full");
                    break;
                case ID_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,  "Client at %s has disconnected",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    disconnectPlayer(receivedPacket->guid());
                    break;
                case ID_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has lost connection",
                        packetAddressToString(receivedPacket->address(), true).c_str());
                    disconnectPlayer(receivedPacket->guid());
                    break;
                case ID_SND_RECEIPT_ACKED:
                case ID_CONNECTED_PING:
                case ID_UNCONNECTED_PING:
                    break;
                default:
                {
                    PacketStream bsIn(&receivedPacket->data()[1], receivedPacket->length());
                    bsIn.IgnoreBytes(static_cast<unsigned int>(packetGuidSize())); // Ignore GUID from received packet


                    if (receivedPacket->id() == ID_GAME_PREINIT)
                    {
                        if (Players::doesPlayerExist(receivedPacket->guid()))
                        {
                            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                                "Received duplicate ID_GAME_PREINIT for existing client GUID %llu; replacing stale player session",
                                static_cast<unsigned long long>(packetGuidValue(receivedPacket->guid())));
                            disconnectPlayer(receivedPacket->guid());
                        }

                        preInit(receivedPacket, bsIn);
                    }
                    else if (Players::doesPlayerExist(receivedPacket->guid()))
                        update(receivedPacket, bsIn);
                    else
                        preInit(receivedPacket, bsIn);
                    break;
                }
            }
        }
        serverSimulation->tick();
        TimerAPI::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    TimerAPI::Terminate();
    return exitCode;
}

void ServerNetworking::kickPlayer(PacketGuid guid, bool sendNotification)
{
    transport->closeConnection(PacketDestination(guid), sendNotification);
}

void ServerNetworking::banAddress(const char *ipAddress)
{
    transport->banAddress(ipAddress);
}

void ServerNetworking::unbanAddress(const char *ipAddress)
{
    transport->unbanAddress(ipAddress);
}

unsigned short ServerNetworking::numberOfConnections() const
{
    return transport->numberOfConnections();
}

unsigned int ServerNetworking::maxConnections() const
{
    return transport->maxConnections();
}

int ServerNetworking::getAvgPing(const PacketDestination& destination) const
{
    return transport->averagePing(destination);
}

unsigned short ServerNetworking::getPort() const
{
    return transport->port();
}

MasterClient *ServerNetworking::getMasterClient()
{
    return mclient;
}

void ServerNetworking::InitQuery(std::string queryAddr, unsigned short queryPort)
{
    mclient = new MasterClient(queryAddr, queryPort);
}

void ServerNetworking::postInit()
{
    nativeDataFileRegistryLoaded = loadDataFileRequirementsFromRegistry();
    mwmp::ServerEvents::requestDataFileList();
    mwmp::ServerEvents::serverPostInit();
}

PacketPreInit::PluginContainer &ServerNetworking::getSamples()
{
    return samples;
}

bool ServerNetworking::usesNativeDataFileRegistry() const
{
    return nativeDataFileRegistryLoaded;
}

bool ServerNetworking::loadDataFileRequirementsFromRegistry()
{
    const mwmp::ServerContentRegistry& registry = mwmp::ServerContentRegistry::get();
    const mwmp::ServerContentRegistryStatistics& stats = registry.statistics();
    if (!stats.loaded || registry.dataFiles().empty())
        return false;

    samples.clear();
    samples.reserve(registry.dataFiles().size());

    for (const mwmp::ServerDataFileRequirement& requirement : registry.dataFiles())
    {
        if (requirement.name.empty())
            continue;

        PacketPreInit::HashList hashes;
        for (const std::string& checksumString : requirement.checksums)
        {
            std::uint32_t checksum = 0;
            if (tryParseChecksum(checksumString, checksum))
                appendUniqueHash(hashes, checksum);
            else
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Ignoring invalid checksum %s for server data file %s from %s",
                    checksumString.c_str(), requirement.name.c_str(), stats.path.string().c_str());
            }
        }

        const std::uint32_t masterChecksum = hashes.empty() ? 0 : hashes.front();
        samples.emplace_back(requirement.name, std::move(hashes));

        if (mclient != nullptr)
            mclient->PushPlugin({ requirement.name, masterChecksum });
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Loaded %zu data file requirement(s) from native C++ registry %s",
        samples.size(), stats.path.string().c_str());
    return !samples.empty();
}
