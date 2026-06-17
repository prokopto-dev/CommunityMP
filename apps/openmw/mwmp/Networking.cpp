#include <stdexcept>
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Transport/GnsTransport.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include <components/esm3/cellid.hpp>
#include <components/files/configurationmanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include <SDL_messagebox.h>
#include <iomanip>
#include <thread>
#include <components/version/version.hpp>

#include <components/openmw-mp/Branding.hpp>
#include "Networking.hpp"
#include "Main.hpp"
#include "processors/ProcessorInitializer.hpp"
#include "processors/SystemProcessor.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"
#include "GUIController.hpp"
#include "CellController.hpp"

using namespace mwmp;

std::string listDiscrepancies(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse)
{
    std::ostringstream sstr;
    sstr << "Your plugins or their load order don't match the server's. A full comparison is included in your debug window and latest log file. In short, the following discrepancies have been found:\n\n";

    int discrepancyCount = 0;

    for (size_t fileIndex = 0; fileIndex < checksums.size() || fileIndex < checksumsResponse.size(); fileIndex++)
    {
        if (fileIndex >= checksumsResponse.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string clientFilename = checksums.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << clientFilename << " is past the number of plugins used by the server";
        }
        else if (fileIndex >= checksums.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << serverFilename << " is completely missing from the client but required by the server";
        }
        else
        {
            std::string clientFilename = checksums.at(fileIndex).first;
            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            std::string clientChecksum = Utils::intToHexStr(checksums.at(fileIndex).second.at(0));

            bool filenameMatches = false;
            bool checksumMatches = false;
            std::string eligibleChecksums = "";

            if (Misc::StringUtils::ciEqual(clientFilename, serverFilename))
                filenameMatches = true;

            if (checksumsResponse.at(fileIndex).second.size() > 0)
            {
                for (size_t checksumIndex = 0; checksumIndex < checksumsResponse.at(fileIndex).second.size(); checksumIndex++)
                {
                    std::string serverChecksum = Utils::intToHexStr(checksumsResponse.at(fileIndex).second.at(checksumIndex));

                    if (checksumIndex != 0)
                        eligibleChecksums = eligibleChecksums + " or ";

                    eligibleChecksums = eligibleChecksums + serverChecksum;

                    if (Misc::StringUtils::ciEqual(clientChecksum, serverChecksum))
                    {
                        checksumMatches = true;
                        break;
                    }
                }
            }
            else
                checksumMatches = true;

            if (!filenameMatches || !checksumMatches)
            {
                discrepancyCount++;

                if (discrepancyCount > 1)
                    sstr << "\n";

                sstr << fileIndex << ": ";

                if (!filenameMatches)
                    sstr << clientFilename << " doesn't match " << serverFilename;

                if (!filenameMatches && !checksumMatches)
                    sstr << ", ";

                if (!checksumMatches)
                    sstr << "checksum " << clientChecksum << " doesn't match " << eligibleChecksums;
            }
        }
    }

    return sstr.str();
}

std::string listComparison(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse,
                      bool full = false)
{
    std::ostringstream sstr;
    size_t pluginNameLen1 = 0;
    size_t pluginNameLen2 = 0;
    for (const auto &checksum : checksums)
        if (pluginNameLen1 < checksum.first.size())
            pluginNameLen1 = checksum.first.size();

    for (const auto &checksum : checksums)
        if (pluginNameLen2 < checksum.first.size())
            pluginNameLen2 = checksum.first.size();

    Utils::printWithWidth(sstr, "Your current plugins are:", pluginNameLen1 + 16);
    sstr << "To join this server, use:\n";

    Utils::printWithWidth(sstr, "name", pluginNameLen1 + 2);
    Utils::printWithWidth(sstr, "hash", 14);
    Utils::printWithWidth(sstr, "name", pluginNameLen2 + 2);
    sstr << "hash\n";

    for (size_t i = 0; i < checksums.size() || i < checksumsResponse.size(); i++)
    {
        std::string plugin;
        unsigned val;

        if (i < checksums.size())
        {
            plugin = checksums.at(i).first;
            val = checksums.at(i).second[0];

            Utils::printWithWidth(sstr, plugin, pluginNameLen1 + 2);
            Utils::printWithWidth(sstr, Utils::intToHexStr(val), 14);
        }
        else
            Utils::printWithWidth(sstr, "", pluginNameLen1 + 16);

        if (i < checksumsResponse.size())
        {
            Utils::printWithWidth(sstr, checksumsResponse[i].first, pluginNameLen2 + 2);
            if (checksumsResponse[i].second.size() > 0)
            {
                if (full)
                    for (size_t j = 0; j < checksumsResponse[i].second.size(); j++)
                        Utils::printWithWidth(sstr, Utils::intToHexStr(checksumsResponse[i].second[j]), 14);
                else
                    sstr << Utils::intToHexStr(checksumsResponse[i].second[0]);
            }
            else
                sstr << "any";
        }

        sstr << "\n";
    }

    return sstr.str();
}

Networking::Networking()
{
    transport = std::make_unique<GnsTransport>(GnsMode::Client);
    BasePacket::SetPacketTransport(transport.get());

    systemPacketController.SetStream(0, &bsOut);
    playerPacketController.SetStream(0, &bsOut);
    actorPacketController.SetStream(0, &bsOut);
    objectPacketController.SetStream(0, &bsOut);
    worldstatePacketController.SetStream(0, &bsOut);

    connected = 0;
    ProcessorInitializer();
}

Networking::~Networking()
{
    BasePacket::SetPacketTransport(nullptr);
}

void Networking::update()
{
    std::string errmsg = "";

    for (ReceivedPacket* receivedPacket = transport->receive(); receivedPacket;
         transport->deallocatePacket(receivedPacket), receivedPacket = transport->receive())
    {
        switch (receivedPacket->id())
        {
            case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has disconnected.");
                break;
            case ID_REMOTE_CONNECTION_LOST:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has lost connection.");
                break;
            case ID_REMOTE_NEW_INCOMING_CONNECTION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has connected.");
                break;
            case ID_CONNECTION_REQUEST_ACCEPTED:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Our connection request has been accepted.");
                break;
            case ID_NEW_INCOMING_CONNECTION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "A connection is incoming.");
                break;
            case ID_NO_FREE_INCOMING_CONNECTIONS:
                errmsg = "The server is full.";
                break;
            case ID_DISCONNECTION_NOTIFICATION:
                errmsg = "We have been disconnected.";
                break;
            case ID_CONNECTION_LOST:
                errmsg = "Connection lost.";
                break;
            default:
                receiveMessage(receivedPacket);
                //LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Message with identifier %i has arrived.", packet->data[0]);
                break;
        }
    }

    if (!errmsg.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Branding::productName, errmsg.c_str(), 0);
        MWBase::Environment::get().getStateManager()->requestQuit();
    }
}

void Networking::connect(const std::string &ip, unsigned short port, std::vector<std::string> &content, Files::Collections &collections)
{
    std::string errmsg = "";

    try
    {
        transport->connect(ip, port);
    }
    catch (const std::exception& e)
    {
        errmsg = std::string("Connection attempt failed.\n") + e.what();
    }

    bool queue = errmsg.empty();
    const auto connectStart = std::chrono::steady_clock::now();
    while (queue)
    {
        bool receivedPacket = false;
        for (ReceivedPacket* incomingPacket = transport->receive(); incomingPacket;
             transport->deallocatePacket(incomingPacket), incomingPacket = transport->receive())
        {
            receivedPacket = true;
            switch (incomingPacket->id())
            {
                case ID_CONNECTION_ATTEMPT_FAILED:
                {
                    errmsg = "Connection failed.\n"
                            "Either the IP address is wrong or a firewall on either system is blocking\n"
                            "UDP packets on the port you have chosen.";
                    queue = false;
                    break;
                }
                case ID_INVALID_PASSWORD:
                {
                    errmsg = "Pre-init compatibility check rejected!\n"
                        "The server rejected this client before gameplay login.";
                    queue = false;
                    break;
                }
                case ID_INCOMPATIBLE_PROTOCOL_VERSION:
                {
                    errmsg = "Network protocol mismatch!\nMake sure your client is really on the same version\n"
                        "as the server you are trying to connect to.";
                    queue = false;
                    break;
                }
                case ID_CONNECTION_REQUEST_ACCEPTED:
                {
                    serverAddr = incomingPacket->address();
                    BaseClientPacketProcessor::SetServerAddr(incomingPacket->address());

                    connected = true;
                    queue = false;

                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Received ID_CONNECTION_REQUESTED_ACCEPTED from %s",
                                       packetAddressToString(serverAddr, true).c_str());

                    break;
                }
                case ID_DISCONNECTION_NOTIFICATION:
                    errmsg = "We have been disconnected.";
                    queue = false;
                    break;
                case ID_CONNECTION_BANNED:
                    errmsg = "You have been banned from this server.";
                    queue = false;
                    break;
                case ID_CONNECTION_LOST:
                    errmsg = "Connection lost.";
                    queue = false;
                    break;
                default:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Connection message with identifier %i has arrived in initialization.",
                                       incomingPacket->id());
            }
        }

        if (queue && std::chrono::steady_clock::now() - connectStart >= std::chrono::seconds(15))
        {
            errmsg = "Connection timed out.\n"
                "Either the IP address is wrong or a firewall on either system is blocking\n"
                "UDP packets on the port you have chosen.";
            queue = false;
        }

        if (queue && !receivedPacket)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!errmsg.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Branding::productName, errmsg.c_str(), 0);
    }
    else
    {
        getLocalPlayer()->guid = getLocalSystem()->guid = transport->getMyGuid();
        preInit(content, collections);
    }
}

void Networking::preInit(std::vector<std::string> &content, Files::Collections &collections)
{
    PacketPreInit::PluginContainer checksums;
    std::vector<std::string>::const_iterator it(content.begin());
    for (int idx = 0; it != content.end(); ++it, ++idx)
    {
        if (*it == "builtin.omwscripts")
            continue;

        if (collections.doesExist(*it))
        {
            PacketPreInit::HashList hashList;
            const std::filesystem::path pluginPath = collections.getPath(*it);
            unsigned crc32 = Utils::crc32Checksum(pluginPath.string());
            hashList.push_back(crc32);
            checksums.push_back(make_pair(*it, hashList));

            LOG_APPEND(TimedLog::LOG_WARN, "idx: %d\tchecksum: %X\tfile: %s\n", idx, crc32, pluginPath.string().c_str());
        }
        else
            throw std::runtime_error("Plugin doesn't exist: " + *it);
    }

    PacketPreInit packetPreInit;
    PacketStream bs;
    PacketGuid guid = getLocalPlayer()->guid;
    std::string commitHashString(Version::getCommitHash());
    commitHashString.erase(std::remove(commitHashString.begin(), commitHashString.end(), '\r'), commitHashString.end());
    packetPreInit.setChecksums(&checksums);
    packetPreInit.setProtocolVersionInfo(TES3MP_VERSION, TES3MP_PROTO_VERSION, commitHashString);
    packetPreInit.setGUID(guid);
    packetPreInit.SetSendStream(&bs);
    packetPreInit.Send(serverAddr);

    PacketPreInit::PluginContainer checksumsResponse;
    std::string errmsg;
    bool done = false;
    while (!done)
    {
        ReceivedPacket* receivedPacket = transport->receive();
        if (!receivedPacket)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        PacketStream bsIn(receivedPacket->data(), receivedPacket->length());
        unsigned char packetId = 0;
        if (!bsIn.Read(packetId))
        {
            errmsg = "Server sent an invalid pre-init response.";
            done = true;
        }
        else switch(packetId)
        {
            case ID_INVALID_PASSWORD:
                errmsg = "Pre-init compatibility check rejected!\n"
                    "The server rejected this client before gameplay login.";
                connected = false;
                done = true;
                break;
            case ID_INCOMPATIBLE_PROTOCOL_VERSION:
                errmsg = "Network protocol mismatch!\nMake sure your client is really on the same version\n"
                    "as the server you are trying to connect to.";
                connected = false;
                done = true;
                break;
            case ID_DISCONNECTION_NOTIFICATION:
            case ID_CONNECTION_LOST:
                done = true;
                break;
            case ID_GAME_PREINIT:
                bsIn.IgnoreBytes(static_cast<unsigned int>(packetGuidSize()));
                packetPreInit.setChecksums(&checksumsResponse);
                packetPreInit.Packet(&bsIn, false);
                if (!packetPreInit.isPacketValid())
                {
                    errmsg = "Server sent an invalid pre-init response.";
                    connected = false;
                }
                else if (packetPreInit.getProtocolVersion() != TES3MP_PROTO_VERSION)
                {
                    errmsg = "Network protocol mismatch!\nMake sure your client is really on the same version\n"
                        "as the server you are trying to connect to.";
                    connected = false;
                }
                else if (packetPreInit.getVersion() != TES3MP_VERSION || packetPreInit.getCommitHash() != commitHashString)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                        "Server build metadata differs, but protocol version matches; allowing connection");
                    LOG_APPEND(TimedLog::LOG_WARN, "- Client version: %s, server version: %s",
                        TES3MP_VERSION, packetPreInit.getVersion().c_str());
                    LOG_APPEND(TimedLog::LOG_WARN, "- Client commit: %s, server commit: %s",
                        commitHashString.c_str(), packetPreInit.getCommitHash().c_str());
                }
                done = true;
                break;
        }

        transport->deallocatePacket(receivedPacket);
    }

    if (!errmsg.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Branding::productName, errmsg.c_str(), 0);
        connected = false;
        return;
    }

    if (!checksumsResponse.empty()) // something wrong
    {
        std::string discrepancyMessage = listDiscrepancies(checksums, checksumsResponse);

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, listDiscrepancies(checksums, checksumsResponse).c_str());
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, listComparison(checksums, checksumsResponse, true).c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Branding::productName, discrepancyMessage.c_str(), 0);
        connected = false;
    }
}

void Networking::receiveMessage(ReceivedPacket* packet)
{
    if (packet->length() < 2)
        return;

    if (systemPacketController.ContainsPacket(packet->id()))
    {
        if (!SystemProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled SystemPacket with identifier %i has arrived", packet->id());
    }
    else if (playerPacketController.ContainsPacket(packet->id()))
    {
        if (!PlayerProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->id());
    }
    else if (actorPacketController.ContainsPacket(packet->id()))
    {
        if (!ActorProcessor::Process(*packet, actorList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->id());
    }
    else if (objectPacketController.ContainsPacket(packet->id()))
    {
        if (!ObjectProcessor::Process(*packet, objectList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->id());
    }
    else if (worldstatePacketController.ContainsPacket(packet->id()))
    {
        if (!WorldstateProcessor::Process(*packet, worldstate))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->id());
    }
}

SystemPacket *Networking::getSystemPacket(PacketId id)
{
    return systemPacketController.GetPacket(id);
}

PlayerPacket *Networking::getPlayerPacket(PacketId id)
{
    return playerPacketController.GetPacket(id);
}

ActorPacket *Networking::getActorPacket(PacketId id)
{
    return actorPacketController.GetPacket(id);
}

ObjectPacket *Networking::getObjectPacket(PacketId id)
{
    return objectPacketController.GetPacket(id);
}

WorldstatePacket *Networking::getWorldstatePacket(PacketId id)
{
    return worldstatePacketController.GetPacket(id);
}

LocalSystem *Networking::getLocalSystem()
{
    return mwmp::Main::get().getLocalSystem();
}

LocalPlayer *Networking::getLocalPlayer()
{
    return mwmp::Main::get().getLocalPlayer();
}

ActorList *Networking::getActorList()
{
    return &actorList;
}

ObjectList *Networking::getObjectList()
{
    return &objectList;
}

Worldstate *Networking::getWorldstate()
{
    return &worldstate;
}

bool Networking::isConnected()
{
    return connected;
}
