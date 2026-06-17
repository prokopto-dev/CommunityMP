#include "CommunityMpClientLuaEventHandler.hpp"

#include <map>
#include <mutex>

#include <yaml-cpp/yaml.h>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "CommunityMpLuaEventSender.hpp"
#include "Player.hpp"

namespace
{
    std::mutex sObservationMutex;
    std::map<mwmp::PacketGuid, std::uint32_t> sLastClientLuaEventSequences;
    std::map<mwmp::PacketGuid, mwmp::CommunityMpPlayerObservation> sLatestLocationObservations;
    std::map<mwmp::PacketGuid, mwmp::CommunityMpClientStateObservation> sLatestStateObservations;

    bool isObservationKind(const std::string& kind)
    {
        return kind == "hello" || kind == "cell_changed" || kind == "teleported" || kind == "chargen_release";
    }

    bool isLocationObservationKind(const std::string& kind)
    {
        return kind == "cell_changed" || kind == "teleported";
    }

    bool isStateObservationKind(const std::string& kind)
    {
        return kind == "chargen_release";
    }

    std::string readString(const YAML::Node& node)
    {
        if (!node || !node.IsScalar())
            return {};

        try
        {
            return node.as<std::string>();
        }
        catch (const YAML::Exception&)
        {
            return {};
        }
    }

    double readDouble(const YAML::Node& node, bool* valid = nullptr)
    {
        if (valid != nullptr)
            *valid = false;

        if (!node || !node.IsScalar())
            return 0.0;

        try
        {
            const double value = node.as<double>();
            if (valid != nullptr)
                *valid = true;
            return value;
        }
        catch (const YAML::Exception&)
        {
            return 0.0;
        }
    }

    bool readBool(const YAML::Node& node)
    {
        if (!node || !node.IsScalar())
            return false;

        try
        {
            return node.as<bool>();
        }
        catch (const YAML::Exception&)
        {
            return false;
        }
    }

    int readInt(const YAML::Node& node, bool* valid = nullptr)
    {
        if (valid != nullptr)
            *valid = false;

        if (!node || !node.IsScalar())
            return 0;

        try
        {
            const int value = node.as<int>();
            if (valid != nullptr)
                *valid = true;
            return value;
        }
        catch (const YAML::Exception&)
        {
            return 0;
        }
    }

    std::uint16_t readSchemaVersion(const YAML::Node& node)
    {
        bool valid = false;
        const double value = readDouble(node, &valid);
        if (!valid || value < 0.0 || value > static_cast<double>(mwmp::clientLuaEventSchemaVersion))
            return 0;

        return static_cast<std::uint16_t>(value);
    }

    mwmp::CommunityMpPlayerObservation parseObservation(Player& player, const YAML::Node& root)
    {
        mwmp::CommunityMpPlayerObservation observation;
        observation.schemaVersion = readSchemaVersion(root["schema"]);
        observation.sequence = player.luaEvent.sequence;
        observation.receivedAt = std::chrono::steady_clock::now();
        observation.kind = readString(root["kind"]);
        observation.objectId = readString(root["objectId"]);
        observation.cellKey = readString(root["cellKey"]);
        observation.simulationTime = readDouble(root["time"]);

        const YAML::Node cell = root["cell"];
        if (cell && cell.IsMap())
        {
            observation.cellId = readString(cell["id"]);
            observation.cellName = readString(cell["name"]);
            observation.cellDisplayName = readString(cell["displayName"]);
            observation.cellRegion = readString(cell["region"]);
            observation.worldSpaceId = readString(cell["worldSpaceId"]);
            observation.isExterior = readBool(cell["isExterior"]);

            bool validGridX = false;
            bool validGridY = false;
            observation.gridX = readDouble(cell["gridX"], &validGridX);
            observation.gridY = readDouble(cell["gridY"], &validGridY);
            observation.hasGrid = validGridX && validGridY;
        }

        const YAML::Node position = root["position"];
        if (position && position.IsMap())
        {
            bool validX = false;
            bool validY = false;
            bool validZ = false;
            observation.positionX = readDouble(position["x"], &validX);
            observation.positionY = readDouble(position["y"], &validY);
            observation.positionZ = readDouble(position["z"], &validZ);
            observation.hasPosition = validX && validY && validZ;
        }

        return observation;
    }

    mwmp::CommunityMpClientStateObservation parseStateObservation(Player& player, const YAML::Node& root)
    {
        mwmp::CommunityMpClientStateObservation observation;
        observation.schemaVersion = readSchemaVersion(root["schema"]);
        observation.sequence = player.luaEvent.sequence;
        observation.receivedAt = std::chrono::steady_clock::now();
        observation.kind = readString(root["kind"]);
        observation.objectId = readString(root["objectId"]);
        observation.simulationTime = readDouble(root["time"]);
        observation.releaseQuestStage = readInt(root["releaseQuestStage"]);
        observation.releaseJournalRecovered = readBool(root["releaseJournalRecovered"]);
        observation.releaseTopicsApplied = readBool(root["releaseTopicsApplied"]);
        return observation;
    }

    bool hasAcceptedSequence(Player& player)
    {
        std::lock_guard lock(sObservationMutex);
        const auto lastSequence = sLastClientLuaEventSequences.find(player.guid);
        if (lastSequence != sLastClientLuaEventSequences.end()
            && player.luaEvent.sequence <= lastSequence->second)
            return true;

        sLastClientLuaEventSequences[player.guid] = player.luaEvent.sequence;
        return false;
    }

    void storeLocationObservation(Player& player, mwmp::CommunityMpPlayerObservation observation)
    {
        std::lock_guard lock(sObservationMutex);
        sLatestLocationObservations[player.guid] = std::move(observation);
    }

    void storeStateObservation(Player& player, mwmp::CommunityMpClientStateObservation observation)
    {
        std::lock_guard lock(sObservationMutex);
        sLatestStateObservations[player.guid] = std::move(observation);
    }
}

namespace mwmp
{
    bool CommunityMpClientLuaEventHandler::handlePlayerEvent(Player& player)
    {
        if (player.luaEvent.namespaceName != "communitymp.player")
            return false;

        if (hasAcceptedSequence(player))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "Ignored stale CommunityMP Lua event from %s: sequence=%u",
                player.npc.mName.c_str(), static_cast<unsigned int>(player.luaEvent.sequence));
            return true;
        }

        YAML::Node root;
        try
        {
            root = YAML::Load(player.luaEvent.payload);
        }
        catch (const YAML::Exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Rejected CommunityMP Lua event from %s because payload parsing failed: %s",
                player.npc.mName.c_str(), e.what());
            return true;
        }

        if (!root || !root.IsMap())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Rejected CommunityMP Lua event from %s because payload is not an object",
                player.npc.mName.c_str());
            return true;
        }

        CommunityMpPlayerObservation observation = parseObservation(player, root);
        if (observation.schemaVersion == 0 || !isObservationKind(observation.kind)
            || observation.kind != player.luaEvent.eventName)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Rejected CommunityMP Lua event from %s: event=%s kind=%s schema=%u",
                player.npc.mName.c_str(), player.luaEvent.eventName.c_str(), observation.kind.c_str(),
                static_cast<unsigned int>(observation.schemaVersion));
            return true;
        }

        if (isLocationObservationKind(observation.kind))
        {
            storeLocationObservation(player, observation);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "Stored CommunityMP Lua location observation from %s: kind=%s cell=%s position=(%.2f, %.2f, %.2f)",
                player.npc.mName.c_str(), player.luaEvent.eventName.c_str(), observation.cellKey.c_str(),
                observation.positionX, observation.positionY, observation.positionZ);
        }
        else if (isStateObservationKind(observation.kind))
        {
            CommunityMpClientStateObservation stateObservation = parseStateObservation(player, root);
            storeStateObservation(player, stateObservation);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "Stored CommunityMP Lua state observation from %s: kind=%s questStage=%i journalRecovered=%s topicsApplied=%s",
                player.npc.mName.c_str(), stateObservation.kind.c_str(), stateObservation.releaseQuestStage,
                stateObservation.releaseJournalRecovered ? "yes" : "no",
                stateObservation.releaseTopicsApplied ? "yes" : "no");
        }

        if (player.luaEvent.eventName == "hello")
        {
            if (!CommunityMpLuaEventSender::sendToPlayer(
                    player, "communitymp.server", "ready", "{\"schema\":1,\"kind\":\"ready\"}"))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Failed to send CommunityMP Lua ready event to %s", player.npc.mName.c_str());
            }
        }

        return true;
    }

    std::optional<CommunityMpPlayerObservation> CommunityMpClientLuaEventHandler::getLatestLocationObservation(
        PacketGuid guid)
    {
        std::lock_guard lock(sObservationMutex);
        const auto observation = sLatestLocationObservations.find(guid);
        if (observation == sLatestLocationObservations.end())
            return std::nullopt;

        return observation->second;
    }

    std::optional<CommunityMpClientStateObservation> CommunityMpClientLuaEventHandler::getLatestStateObservation(
        PacketGuid guid)
    {
        std::lock_guard lock(sObservationMutex);
        const auto observation = sLatestStateObservations.find(guid);
        if (observation == sLatestStateObservations.end())
            return std::nullopt;

        return observation->second;
    }

    std::optional<CommunityMpPlayerObservation> CommunityMpClientLuaEventHandler::getLatestObservation(PacketGuid guid)
    {
        return getLatestLocationObservation(guid);
    }

    void CommunityMpClientLuaEventHandler::clearPlayer(PacketGuid guid)
    {
        std::lock_guard lock(sObservationMutex);
        sLastClientLuaEventSequences.erase(guid);
        sLatestLocationObservations.erase(guid);
        sLatestStateObservations.erase(guid);
    }
}
