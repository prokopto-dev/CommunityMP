#include "ServerSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/openmw-mp/Base/ActorStatsAuthority.hpp>
#include <components/files/conversion.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Transport/PacketDelivery.hpp>

#include "Script/Script.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "CommunityMpClientLuaEventHandler.hpp"
#include "CommunityMpLuaEventSender.hpp"
#include "QuestEffectExecutor.hpp"
#include "QuestDatabaseStore.hpp"
#include "QuestEventJournalStore.hpp"
#include "QuestRuntimeEvaluator.hpp"
#include "ServerContentRegistry.hpp"
#include "ServerEventDispatcher.hpp"
#include "ServerNetworking.hpp"
#include "Player.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/actor/ActorSequenceCoalescing.hpp"

namespace
{
    constexpr float serverMovementUnitsPerSecond = 300.f;
    constexpr float firstMovementStepSeconds = 1.f / 60.f;
    constexpr float maxMovementStepSeconds = 0.05f;
    constexpr float actorTickIntervalSeconds = 1.f / 30.f;
    constexpr float correctionDistanceSquared = 48.f * 48.f;
    constexpr float maxPlayerMovementUnitsPerSecond = 1600.f;
    constexpr float maxPlayerVerticalUnitsPerSecond = 2400.f;
    constexpr float playerMovementCorrectionAllowance = 128.f;
    constexpr float maxPlayerSampleGraceSeconds = 0.025f;
    constexpr float maxPlayerLatencyGraceSeconds = 0.050f;
    constexpr float maxPlayerPlausibilityDeltaSeconds = 0.120f;
    constexpr float cellSpaceTransitionDistance = static_cast<float>(ESM::Cell::sSize) * 0.5f;
    constexpr float cellSpaceTransitionDistanceSquared = cellSpaceTransitionDistance * cellSpaceTransitionDistance;
    constexpr float healthDeadEpsilon = 0.001f;
    constexpr float maxServerAttackDamage = 10000.f;
    constexpr auto luaObservationFreshnessWindow = std::chrono::seconds(5);
    constexpr float aiCoordinateStopDistance = 64.f;
    constexpr float aiTargetStopDistance = 128.f;
    constexpr float aiMinimumStopDistance = 48.f;
    constexpr float aiMaximumStopDistance = 2048.f;
    constexpr float aiWanderStopDistance = 32.f;
    constexpr float aiWanderMinimumDecisionSeconds = 2.f;
    constexpr float aiWanderMaximumDecisionSeconds = 8.f;
    constexpr float twoPi = 6.28318530717958647692f;
    constexpr float followerCellChangeBehindDistance = 96.f;
    constexpr float followerCellChangeRowSpacing = 48.f;
    constexpr float followerCellChangeColumnSpacing = 48.f;
    constexpr std::size_t runtimeStatusContentPreviewLimit = 32;
    constexpr auto luaMovementHealthFreshnessWindow = std::chrono::seconds(2);

    bool containsGuid(const std::vector<mwmp::PacketGuid>& guids, mwmp::PacketGuid guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }

    bool eraseGuid(std::vector<mwmp::PacketGuid>& guids, mwmp::PacketGuid guid)
    {
        const auto previousSize = guids.size();
        guids.erase(std::remove(guids.begin(), guids.end(), guid), guids.end());
        return guids.size() != previousSize;
    }

    std::string shadowAuthorityName(mwmp::PacketGuid guid)
    {
        if (Player* player = Players::getPlayer(guid))
        {
            if (!player->npc.mName.empty())
                return player->npc.mName;
        }

        return mwmp::packetGuidToString(guid);
    }

    void appendJsonString(std::string& result, std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";

        result.push_back('"');
        for (const unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        result += "\\u00";
                        result.push_back(hex[(c >> 4) & 0x0f]);
                        result.push_back(hex[c & 0x0f]);
                    }
                    else
                        result.push_back(static_cast<char>(c));
            }
        }
        result.push_back('"');
    }

    std::string jsonString(std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        appendJsonString(result, value);
        return result;
    }

    const char* jsonBool(bool value)
    {
        return value ? "true" : "false";
    }

    std::string runtimeAuthorityBlockReason(const mwmp::SimulationRuntime& runtime)
    {
        const mwmp::SimulationRuntimeTopology& topology = runtime.topology();
        const mwmp::SimulationRuntimeCapabilities& capabilities = runtime.capabilities();

        if (runtime.canOwnActorAuthority())
            return {};

        if (!topology.hasHeadlessOpenMwEngine)
            return "headless-openmw-engine-missing";
        if (!capabilities.ownsWorldState || !capabilities.resolvesCells)
            return "openmw-world-state-not-owned";
        if (!capabilities.runsScripts)
            return "openmw-scripts-not-running";
        if (!capabilities.runsActorAi || !capabilities.ownsActorMovement)
            return "openmw-actor-ai-not-owned";
        if (!capabilities.ownsActorCombat)
            return "openmw-actor-combat-not-owned";

        return "unknown";
    }

    void appendJsonFloat(std::string& result, float value)
    {
        if (std::isfinite(value))
            result += std::to_string(value);
        else
            result += "null";
    }

    void appendJsonPosition(std::string& result, const ESM::Position& position)
    {
        result += "{\"x\":";
        appendJsonFloat(result, position.pos[0]);
        result += ",\"y\":";
        appendJsonFloat(result, position.pos[1]);
        result += ",\"z\":";
        appendJsonFloat(result, position.pos[2]);
        result += ",\"rotX\":";
        appendJsonFloat(result, position.rot[0]);
        result += ",\"rotY\":";
        appendJsonFloat(result, position.rot[1]);
        result += ",\"rotZ\":";
        appendJsonFloat(result, position.rot[2]);
        result += "}";
    }

    void appendJsonStringArray(std::string& result, const std::vector<std::string>& values)
    {
        result.push_back('[');
        bool first = true;
        for (const std::string& value : values)
        {
            if (!first)
                result.push_back(',');
            first = false;
            appendJsonString(result, value);
        }
        result.push_back(']');
    }

    std::string shadowAuthorityAuditName(std::optional<mwmp::PacketGuid> guid)
    {
        if (!guid || !mwmp::isPacketGuidAssigned(*guid))
            return "unassigned";

        return shadowAuthorityName(*guid);
    }

    std::string shadowAuthorityAuditName(mwmp::PacketGuid guid)
    {
        if (!mwmp::isPacketGuidAssigned(guid))
            return "unassigned";

        return shadowAuthorityName(guid);
    }

    Cell* findLoadedServerCellByDescription(const std::string& cellDescription)
    {
        if (cellDescription.empty())
            return nullptr;

        for (Cell* cell : CellController::get()->getCells())
        {
            if (cell != nullptr && cell->getShortDescription() == cellDescription)
                return cell;
        }

        return nullptr;
    }

    std::string getLuaCellKey(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return "exterior::" + std::to_string(cell.mData.mX) + ":" + std::to_string(cell.mData.mY);

        return "interior:" + cell.mName;
    }

    void sendPlayerMovementCorrectionEvent(Player& player, std::string_view reason, bool sentCorrection,
        float serverDeltaSeconds, float sampleIntervalSeconds, float plausibilityDeltaSeconds,
        const ESM::Position& attemptedPosition, const ESM::Position& authoritativePosition,
        std::string_view attemptedCellDescription, std::string_view authoritativeCellDescription)
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED)
            return;

        const float deltaX = attemptedPosition.pos[0] - authoritativePosition.pos[0];
        const float deltaY = attemptedPosition.pos[1] - authoritativePosition.pos[1];
        const float horizontalDistanceSquared = deltaX * deltaX + deltaY * deltaY;
        const float horizontalDistance = std::isfinite(horizontalDistanceSquared)
            ? std::sqrt(horizontalDistanceSquared)
            : std::numeric_limits<float>::quiet_NaN();
        const float verticalDistance = std::abs(attemptedPosition.pos[2] - authoritativePosition.pos[2]);
        const float maxHorizontalDistance = std::isfinite(plausibilityDeltaSeconds)
            ? maxPlayerMovementUnitsPerSecond * plausibilityDeltaSeconds + playerMovementCorrectionAllowance
            : std::numeric_limits<float>::quiet_NaN();
        const float maxVerticalDistance = std::isfinite(plausibilityDeltaSeconds)
            ? maxPlayerVerticalUnitsPerSecond * plausibilityDeltaSeconds + playerMovementCorrectionAllowance
            : std::numeric_limits<float>::quiet_NaN();

        std::string payload;
        payload.reserve(560 + attemptedCellDescription.size() + authoritativeCellDescription.size());
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"movement_correction\",\"reason\":";
        payload += jsonString(reason);
        payload += ",\"sentCorrection\":";
        payload += jsonBool(sentCorrection);
        payload += ",\"positionSequence\":";
        payload += std::to_string(player.positionSequence);
        payload += ",\"serverDeltaSeconds\":";
        appendJsonFloat(payload, serverDeltaSeconds);
        payload += ",\"sampleIntervalSeconds\":";
        appendJsonFloat(payload, sampleIntervalSeconds);
        payload += ",\"plausibilityDeltaSeconds\":";
        appendJsonFloat(payload, plausibilityDeltaSeconds);
        payload += ",\"horizontalDistance\":";
        appendJsonFloat(payload, horizontalDistance);
        payload += ",\"verticalDistance\":";
        appendJsonFloat(payload, verticalDistance);
        payload += ",\"maxHorizontalDistance\":";
        appendJsonFloat(payload, maxHorizontalDistance);
        payload += ",\"maxVerticalDistance\":";
        appendJsonFloat(payload, maxVerticalDistance);
        payload += ",\"attemptedCellDescription\":";
        payload += jsonString(attemptedCellDescription);
        payload += ",\"authoritativeCellDescription\":";
        payload += jsonString(authoritativeCellDescription);
        payload += ",\"attemptedPosition\":";
        appendJsonPosition(payload, attemptedPosition);
        payload += ",\"authoritativePosition\":";
        appendJsonPosition(payload, authoritativePosition);
        payload += "}";

        if (!mwmp::CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "movement_correction", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send movement correction event for %s", player.npc.mName.c_str());
        }
    }

    float squaredHorizontalLength(float x, float y)
    {
        return x * x + y * y;
    }

    float squaredDistance(const ESM::Position& left, const ESM::Position& right)
    {
        float result = 0.f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float delta = left.pos[axis] - right.pos[axis];
            result += delta * delta;
        }
        return result;
    }

    void sanitizeFinitePosition(ESM::Position& position)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(position.pos[axis]))
                position.pos[axis] = 0.f;

            if (!std::isfinite(position.rot[axis]))
                position.rot[axis] = 0.f;
        }
    }

    void normalizeHorizontalIntent(float& x, float& y)
    {
        const float lengthSquared = squaredHorizontalLength(x, y);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.f)
        {
            x = 0.f;
            y = 0.f;
            return;
        }

        if (lengthSquared <= 1.f)
            return;

        const float inverseLength = 1.f / std::sqrt(lengthSquared);
        x *= inverseLength;
        y *= inverseLength;
    }

    ESM::Position simulateMovementPosition(const ESM::Position& currentPosition,
        const ESM::Position& observedPosition, ESM::Position& observedDirection, float deltaSeconds)
    {
        sanitizeFinitePosition(observedDirection);

        float horizontalX = observedDirection.pos[0];
        float horizontalY = observedDirection.pos[1];
        normalizeHorizontalIntent(horizontalX, horizontalY);

        const float yaw = std::isfinite(observedPosition.rot[2]) ? observedPosition.rot[2] : currentPosition.rot[2];
        const float sinYaw = std::sin(yaw);
        const float cosYaw = std::cos(yaw);
        const float worldX = horizontalX * cosYaw + horizontalY * sinYaw;
        const float worldY = -horizontalX * sinYaw + horizontalY * cosYaw;

        ESM::Position simulatedPosition = currentPosition;
        simulatedPosition.pos[0] += worldX * serverMovementUnitsPerSecond * deltaSeconds;
        simulatedPosition.pos[1] += worldY * serverMovementUnitsPerSecond * deltaSeconds;

        const float maxVerticalDelta = serverMovementUnitsPerSecond * deltaSeconds;
        simulatedPosition.pos[2] += std::clamp(
            observedPosition.pos[2] - currentPosition.pos[2], -maxVerticalDelta, maxVerticalDelta);

        for (int axis = 0; axis < 3; ++axis)
            simulatedPosition.rot[axis] = observedPosition.rot[axis];

        sanitizeFinitePosition(simulatedPosition);
        observedDirection.pos[0] = horizontalX;
        observedDirection.pos[1] = horizontalY;

        return simulatedPosition;
    }

    float clampMovementDeltaSeconds(float seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0.f)
            return firstMovementStepSeconds;

        return std::clamp(seconds, 0.f, maxMovementStepSeconds);
    }

    float estimateOneWayLatencySeconds(mwmp::PacketGuid guid)
    {
        if (guid == mwmp::unassignedPacketGuid())
            return 0.f;

        const int pingMilliseconds = mwmp::ServerNetworking::get().getAvgPing(guid);
        if (pingMilliseconds <= 0)
            return 0.f;

        return mwmp::sanitizeMovementLatencySeconds(static_cast<float>(pingMilliseconds) * 0.0005f);
    }

    float getPlayerPlausibilityDeltaSeconds(
        float serverDeltaSeconds, float sampleIntervalSeconds, float oneWayLatencySeconds)
    {
        const float sampleGraceSeconds = std::min(
            mwmp::sanitizeMovementSampleIntervalSeconds(sampleIntervalSeconds) * 0.75f, maxPlayerSampleGraceSeconds);
        const float latencyGraceSeconds = std::min(
            mwmp::sanitizeMovementLatencySeconds(oneWayLatencySeconds) * 0.50f, maxPlayerLatencyGraceSeconds);

        return std::clamp(clampMovementDeltaSeconds(serverDeltaSeconds) + sampleGraceSeconds + latencyGraceSeconds,
            firstMovementStepSeconds, maxPlayerPlausibilityDeltaSeconds);
    }

    bool isPlausiblePlayerMovement(
        const ESM::Position& acceptedPosition, const ESM::Position& clientPosition, float deltaSeconds)
    {
        const float deltaX = clientPosition.pos[0] - acceptedPosition.pos[0];
        const float deltaY = clientPosition.pos[1] - acceptedPosition.pos[1];
        const float horizontalDistanceSquared = squaredHorizontalLength(deltaX, deltaY);
        if (!std::isfinite(horizontalDistanceSquared))
            return false;

        const float maxHorizontalDistance =
            maxPlayerMovementUnitsPerSecond * deltaSeconds + playerMovementCorrectionAllowance;
        if (horizontalDistanceSquared > maxHorizontalDistance * maxHorizontalDistance)
            return false;

        const float deltaZ = std::abs(clientPosition.pos[2] - acceptedPosition.pos[2]);
        const float maxVerticalDistance =
            maxPlayerVerticalUnitsPerSecond * deltaSeconds + playerMovementCorrectionAllowance;
        return std::isfinite(deltaZ) && deltaZ <= maxVerticalDistance;
    }

    bool positionsMatchWithinEpsilon(const ESM::Position& left, const ESM::Position& right)
    {
        constexpr float positionEpsilon = 0.01f;
        constexpr float rotationEpsilon = 0.0001f;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(left.pos[axis] - right.pos[axis]) > positionEpsilon)
                return false;

            if (std::abs(left.rot[axis] - right.rot[axis]) > rotationEpsilon)
                return false;
        }

        return true;
    }

    bool isLikelyCellSpaceTransitionSnapshot(
        const ESM::Position& acceptedPosition, const ESM::Position& clientPosition)
    {
        const float deltaX = clientPosition.pos[0] - acceptedPosition.pos[0];
        const float deltaY = clientPosition.pos[1] - acceptedPosition.pos[1];
        const float horizontalDistanceSquared = squaredHorizontalLength(deltaX, deltaY);
        return std::isfinite(horizontalDistanceSquared)
            && horizontalDistanceSquared > cellSpaceTransitionDistanceSquared;
    }

    std::string getCellSimulationKey(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return "exterior:" + std::to_string(cell.mData.mX) + "," + std::to_string(cell.mData.mY);

        return "interior:" + cell.mName;
    }

    bool nearlyInteger(double value)
    {
        return std::abs(value - std::round(value)) < 0.001;
    }

    bool observationMatchesCell(const mwmp::CommunityMpPlayerObservation& observation, const ESM::Cell& cell)
    {
        if (cell.isExterior())
        {
            if (!observation.isExterior || !observation.hasGrid || !nearlyInteger(observation.gridX)
                || !nearlyInteger(observation.gridY))
                return false;

            return static_cast<int>(std::lround(observation.gridX)) == cell.mData.mX
                && static_cast<int>(std::lround(observation.gridY)) == cell.mData.mY;
        }

        if (observation.isExterior)
            return false;

        return observation.cellKey == getCellSimulationKey(cell)
            || observation.cellId == cell.mName
            || observation.cellName == cell.mName;
    }

    std::optional<float> getFreshLuaMovementHealthSampleIntervalSeconds(const Player& player)
    {
        std::optional<mwmp::CommunityMpPlayerObservation> observation
            = mwmp::CommunityMpClientLuaEventHandler::getLatestMovementHealthObservation(player.guid);
        if (!observation || observation->kind != "movement_health" || !observation->hasFrameStats)
            return std::nullopt;

        if (observation->receivedAt == std::chrono::steady_clock::time_point()
            || std::chrono::steady_clock::now() - observation->receivedAt > luaMovementHealthFreshnessWindow)
            return std::nullopt;

        if (!observationMatchesCell(*observation, player.cell))
            return std::nullopt;

        if (observation->frameCount <= 0 || !std::isfinite(observation->averageFrameSeconds)
            || observation->averageFrameSeconds <= 0.0)
            return std::nullopt;

        return mwmp::sanitizeMovementSampleIntervalSeconds(static_cast<float>(observation->averageFrameSeconds));
    }

    float getObservedPlayerSampleIntervalSeconds(const Player& player)
    {
        const float packetSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
            player.movementSampleIntervalSeconds);
        const std::optional<float> luaSampleIntervalSeconds = getFreshLuaMovementHealthSampleIntervalSeconds(player);
        if (!luaSampleIntervalSeconds)
            return packetSampleIntervalSeconds;

        return std::max(packetSampleIntervalSeconds, *luaSampleIntervalSeconds);
    }

    enum class LuaObservationCellStatus
    {
        Confirmed,
        Missing,
        WrongKind,
        Stale,
        Mismatch,
    };

    struct LuaObservationCellCheck
    {
        LuaObservationCellStatus status = LuaObservationCellStatus::Missing;
        std::optional<mwmp::CommunityMpPlayerObservation> observation;
        double ageSeconds = 0.0;
    };

    const char* luaObservationCellStatusName(LuaObservationCellStatus status)
    {
        switch (status)
        {
            case LuaObservationCellStatus::Confirmed:
                return "confirmed";
            case LuaObservationCellStatus::Missing:
                return "missing";
            case LuaObservationCellStatus::WrongKind:
                return "wrong-kind";
            case LuaObservationCellStatus::Stale:
                return "stale";
            case LuaObservationCellStatus::Mismatch:
                return "mismatch";
        }

        return "unknown";
    }

    std::string describeLuaObservationCell(const mwmp::CommunityMpPlayerObservation& observation)
    {
        if (observation.isExterior)
        {
            if (observation.hasGrid)
            {
                return "exterior grid (" + std::to_string(static_cast<int>(std::lround(observation.gridX)))
                    + ", " + std::to_string(static_cast<int>(std::lround(observation.gridY)))
                    + ") key=" + observation.cellKey;
            }

            return "exterior key=" + observation.cellKey;
        }

        return "interior key=" + observation.cellKey + " id=" + observation.cellId
            + " name=" + observation.cellName;
    }

    LuaObservationCellCheck checkFreshLuaObservationCell(const Player& player, const ESM::Cell& cell)
    {
        LuaObservationCellCheck check;
        check.observation
            = mwmp::CommunityMpClientLuaEventHandler::getLatestLocationObservation(player.guid);
        if (!check.observation)
            return check;

        if (check.observation->kind != "cell_changed" && check.observation->kind != "teleported")
        {
            check.status = LuaObservationCellStatus::WrongKind;
            return check;
        }

        const auto now = std::chrono::steady_clock::now();
        if (check.observation->receivedAt != std::chrono::steady_clock::time_point())
            check.ageSeconds = std::chrono::duration<double>(now - check.observation->receivedAt).count();

        if (check.observation->receivedAt == std::chrono::steady_clock::time_point()
            || now - check.observation->receivedAt > luaObservationFreshnessWindow)
        {
            check.status = LuaObservationCellStatus::Stale;
            return check;
        }

        if (!observationMatchesCell(*check.observation, cell))
        {
            check.status = LuaObservationCellStatus::Mismatch;
            return check;
        }

        check.status = LuaObservationCellStatus::Confirmed;
        return check;
    }

    bool luaObservationConfirmsCell(const LuaObservationCellCheck& check)
    {
        return check.status == LuaObservationCellStatus::Confirmed;
    }

    void logLuaObservationCellCheckFailure(
        const Player& player, const ESM::Cell& attemptedCell, const LuaObservationCellCheck& check)
    {
        if (check.observation)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "OpenMW Lua observation did not confirm cell change for %s to %s: status=%s kind=%s age=%.3fs "
                "observed=%s expectedKey=%s",
                player.npc.mName.c_str(), attemptedCell.getDescription().c_str(),
                luaObservationCellStatusName(check.status), check.observation->kind.c_str(), check.ageSeconds,
                describeLuaObservationCell(*check.observation).c_str(), getCellSimulationKey(attemptedCell).c_str());
        }
        else
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "OpenMW Lua observation did not confirm cell change for %s to %s: status=%s expectedKey=%s",
                player.npc.mName.c_str(), attemptedCell.getDescription().c_str(),
                luaObservationCellStatusName(check.status), getCellSimulationKey(attemptedCell).c_str());
        }
    }

    std::uint32_t mixWanderHash(std::uint32_t hash, std::uint32_t value)
    {
        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 16777619u;
        }

        return hash;
    }

    std::uint32_t getActorWanderHash(const std::string& cellKey, unsigned int refNum, unsigned int mpNum,
        std::uint32_t sequence, std::uint32_t salt)
    {
        std::uint32_t hash = 2166136261u;

        for (const char character : cellKey)
        {
            hash ^= static_cast<unsigned char>(character);
            hash *= 16777619u;
        }

        hash = mixWanderHash(hash, refNum);
        hash = mixWanderHash(hash, mpNum);
        hash = mixWanderHash(hash, sequence);
        hash = mixWanderHash(hash, salt);
        return hash;
    }

    float getUnitWanderValue(std::uint32_t hash)
    {
        return static_cast<float>(hash & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    bool isSameSimulationCell(const ESM::Cell& left, const ESM::Cell& right)
    {
        return getCellSimulationKey(left) == getCellSimulationKey(right);
    }

    bool areAdjacentExteriorCells(const ESM::Cell& left, const ESM::Cell& right)
    {
        if (!left.isExterior() || !right.isExterior())
            return false;

        const int deltaX = left.mData.mX - right.mData.mX;
        const int deltaY = left.mData.mY - right.mData.mY;
        return deltaX >= -1 && deltaX <= 1 && deltaY >= -1 && deltaY <= 1;
    }

    bool exteriorAxisMatchesPosition(int cellIndex, float coordinate)
    {
        if (!std::isfinite(coordinate))
            return false;

        constexpr double cellSize = static_cast<double>(ESM::Cell::sSize);
        const double coordinateValue = static_cast<double>(coordinate);
        const int positionCellIndex = static_cast<int>(std::floor(coordinateValue / cellSize));

        if (cellIndex == positionCellIndex)
            return true;

        if (cellIndex == positionCellIndex + 1)
            return coordinateValue >= static_cast<double>(cellIndex) * cellSize - playerMovementCorrectionAllowance;

        if (cellIndex == positionCellIndex - 1)
            return coordinateValue <= static_cast<double>(cellIndex + 1) * cellSize + playerMovementCorrectionAllowance;

        return false;
    }

    bool isExteriorCellConsistentWithPosition(const ESM::Cell& cell, const ESM::Position& position)
    {
        if (!cell.isExterior())
            return true;

        return exteriorAxisMatchesPosition(cell.mData.mX, position.pos[0])
            && exteriorAxisMatchesPosition(cell.mData.mY, position.pos[1]);
    }

    bool hasServerAcceptedDestinationTransform(const Player& player)
    {
        return player.hasAcceptedPositionPacket
            && isExteriorCellConsistentWithPosition(player.cell, player.position)
            && squaredDistance(player.acceptedPosition, player.position) <= playerMovementCorrectionAllowance
                * playerMovementCorrectionAllowance;
    }

    bool isCellChangePlausibleFromAcceptedState(const Player& player, const ESM::Cell& acceptedCell)
    {
        if (!isExteriorCellConsistentWithPosition(player.cell, player.position))
            return false;

        if (isSameSimulationCell(acceptedCell, player.cell))
            return true;

        if (!acceptedCell.isExterior() || !player.cell.isExterior())
            return mwmp::isExplicitCellChangeReason(player.cellChangeReason);

        if (areAdjacentExteriorCells(acceptedCell, player.cell))
            return true;

        return mwmp::isExplicitCellChangeReason(player.cellChangeReason)
            || hasServerAcceptedDestinationTransform(player);
    }

    void sendAcceptedPlayerCellCorrection(Player& player, mwmp::PlayerPacket& packet, const ESM::Cell& acceptedCell)
    {
        const std::string attemptedCell = player.cell.getDescription();
        const std::string correctionCell = acceptedCell.getDescription();
        const ESM::Position attemptedPosition = player.position;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Rejecting implausible cell change for %s to %s; correcting to %s",
            player.npc.mName.c_str(), attemptedCell.c_str(), correctionCell.c_str());

        player.cell = acceptedCell;
        player.cellChangeReason = mwmp::CELL_CHANGE_REASON_SERVER;
        if (player.hasAcceptedPositionPacket)
            player.restoreAcceptedPositionPacket();

        player.previousCellPosition = player.position;
        player.isChangingRegion = false;
        packet.setPlayer(&player);
        packet.SendWithReliability(player.guid, mwmp::PacketReliability::ReliableOrdered);
        sendPlayerMovementCorrectionEvent(player, "cell_change_correction", true,
            std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(), attemptedPosition, player.position,
            attemptedCell, correctionCell);
    }

    bool hasMovementIntent(const ESM::Position& direction)
    {
        return direction.pos[0] != 0.f || direction.pos[1] != 0.f || direction.pos[2] != 0.f
            || direction.rot[0] != 0.f || direction.rot[1] != 0.f || direction.rot[2] != 0.f;
    }

    ESM::Position zeroPosition()
    {
        ESM::Position position;
        for (int axis = 0; axis < 3; ++axis)
        {
            position.pos[axis] = 0.f;
            position.rot[axis] = 0.f;
        }
        return position;
    }

    bool hasFiniteWorldPosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2]);
    }

    bool canApplyServerAttackDamage(const mwmp::Attack& attack)
    {
        return attack.isHit && attack.success && !attack.block && std::isfinite(attack.damage)
            && attack.damage > healthDeadEpsilon;
    }

    bool hasValidCastShape(const mwmp::Cast& cast)
    {
        if (cast.type != mwmp::Cast::REGULAR && cast.type != mwmp::Cast::ITEM)
            return false;

        if (cast.type == mwmp::Cast::REGULAR)
            return !cast.spellId.empty();

        return !cast.itemId.empty();
    }

    bool hasExplicitActorTarget(const mwmp::Target& target)
    {
        return !target.refId.empty() || target.refNum != static_cast<unsigned int>(-1)
            || target.mpNum != static_cast<unsigned int>(-1);
    }

    bool castHasReleasedOutcome(const mwmp::Cast& cast)
    {
        if (cast.pressed)
            return false;

        return cast.success || cast.type == mwmp::Cast::ITEM;
    }

    bool isAcceptedCastTarget(const mwmp::Cast& cast, const ESM::Cell& casterCell, Player* playerCaster,
        Cell* serverCell);

    float getServerAttackDamage(const mwmp::Attack& attack)
    {
        return std::clamp(attack.damage, 0.f, maxServerAttackDamage);
    }

    bool isUnarmedMeleeAttack(const mwmp::Attack& attack)
    {
        return attack.type == mwmp::Attack::MELEE && attack.rangedWeaponId.empty();
    }

    bool shouldApplyUnarmedHealthDamage(bool isKnockedDown, float fatigue)
    {
        return isKnockedDown || (std::isfinite(fatigue) && fatigue <= 0.f);
    }

    bool shouldApplyAttackHealthDamage(const mwmp::Attack& attack, const ESM::CreatureStats& targetStats)
    {
        if (!isUnarmedMeleeAttack(attack))
            return true;

        return shouldApplyUnarmedHealthDamage(targetStats.mKnockdown, targetStats.mDynamic[2].mCurrent);
    }

    bool shouldApplyAttackHealthDamage(const mwmp::Attack& attack, const mwmp::SimpleCreatureStats& targetStats)
    {
        if (!isUnarmedMeleeAttack(attack))
            return true;

        return shouldApplyUnarmedHealthDamage(false, targetStats.mDynamic[2].mCurrent);
    }

    bool targetMatchesActor(const mwmp::Target& target, const mwmp::BaseActor& actor)
    {
        if (target.refNum != actor.refNum || target.mpNum != actor.mpNum)
            return false;

        return target.refId.empty() || actor.refId.empty() || target.refId == actor.refId;
    }

    bool isMissingActorTarget(const mwmp::Target& target)
    {
        return !target.isPlayer && !hasExplicitActorTarget(target);
    }

    bool targetsReferToSameEntity(const mwmp::Target& left, const mwmp::Target& right)
    {
        if (left.isPlayer != right.isPlayer)
            return false;

        if (left.isPlayer)
            return mwmp::isPacketGuidAssigned(left.guid) && left.guid == right.guid;

        if (left.refNum != right.refNum || left.mpNum != right.mpNum)
            return false;

        return left.refId.empty() || right.refId.empty() || left.refId == right.refId;
    }

    mwmp::BaseActor* findActorTarget(Cell& cell, const mwmp::Target& target)
    {
        mwmp::BaseActor* actor = cell.getActor(target.refNum, target.mpNum);
        if (actor == nullptr || !targetMatchesActor(target, *actor))
            return nullptr;

        return actor;
    }

    std::pair<Cell*, mwmp::BaseActor*> findLoadedActorTarget(Player& player, const mwmp::Target& target)
    {
        for (Cell* loadedCell : *player.getCells())
        {
            if (loadedCell == nullptr)
                continue;

            if (mwmp::BaseActor* actor = findActorTarget(*loadedCell, target))
                return { loadedCell, actor };
        }

        return { nullptr, nullptr };
    }

    bool isLivePlayerAiTarget(const Player& player)
    {
        if (player.creatureStats.mDead)
            return false;

        if (player.hasFiniteDynamicStats()
            && player.creatureStats.mDynamic[0].mCurrent <= healthDeadEpsilon)
            return false;

        return true;
    }

    bool isAcceptedPlayerCastTarget(const mwmp::Target& target, const ESM::Cell& casterCell)
    {
        Player* targetPlayer = Players::getPlayer(target.guid);
        if (targetPlayer == nullptr || !isLivePlayerAiTarget(*targetPlayer))
            return false;

        return isSameSimulationCell(targetPlayer->cell, casterCell);
    }

    bool isAcceptedActorCombatTarget(Cell& cell, const mwmp::Target& target)
    {
        if (target.isPlayer)
            return isAcceptedPlayerCastTarget(target, cell.getCellData());

        mwmp::BaseActor* targetActor = findActorTarget(cell, target);
        return isClientActorControlUpdateAllowed(targetActor);
    }

    bool actorHasServerCombatTarget(const mwmp::BaseActor& storedActor)
    {
        return storedActor.hasAiData && storedActor.hasAiTarget
            && storedActor.aiAction == mwmp::BaseActorList::COMBAT;
    }

    bool actorCombatTargetMatchesServerAi(Cell& cell, const mwmp::BaseActor& storedActor,
        const mwmp::Target& observedTarget, bool allowMissingTarget)
    {
        if (!actorHasServerCombatTarget(storedActor))
            return false;

        if (allowMissingTarget && isMissingActorTarget(observedTarget))
            return true;

        if (!targetsReferToSameEntity(storedActor.aiTarget, observedTarget))
            return false;

        return isAcceptedActorCombatTarget(cell, observedTarget);
    }

    bool isAcceptedActorAttackObservation(Cell& cell, const mwmp::BaseActor& storedActor,
        const mwmp::BaseActor& observedActor)
    {
        const bool isDamageEvent = canApplyServerAttackDamage(observedActor.attack);
        if (actorCombatTargetMatchesServerAi(cell, storedActor, observedActor.attack.target, !isDamageEvent))
            return true;

        if (isDamageEvent)
            return false;

        if (isMissingActorTarget(observedActor.attack.target))
            return true;

        return isAcceptedActorCombatTarget(cell, observedActor.attack.target);
    }

    bool isAcceptedActorCastObservation(Cell& cell, const mwmp::BaseActor& storedActor,
        const mwmp::BaseActor& observedActor)
    {
        if (!hasValidCastShape(observedActor.cast))
            return false;

        const bool isReleasedOutcome = castHasReleasedOutcome(observedActor.cast);
        if (!isReleasedOutcome)
            return actorCombatTargetMatchesServerAi(cell, storedActor, observedActor.cast.target, true);

        const bool hasTargetedOutcome = observedActor.cast.target.isPlayer || hasExplicitActorTarget(observedActor.cast.target);
        if (!actorCombatTargetMatchesServerAi(cell, storedActor, observedActor.cast.target, !hasTargetedOutcome))
            return false;

        return isAcceptedCastTarget(observedActor.cast, cell.getCellData(), nullptr, &cell);
    }

    bool isAcceptedCastTarget(const mwmp::Cast& cast, const ESM::Cell& casterCell, Player* playerCaster,
        Cell* serverCell)
    {
        if (!hasValidCastShape(cast))
            return false;

        if (!castHasReleasedOutcome(cast))
            return true;

        if (cast.target.isPlayer)
            return isAcceptedPlayerCastTarget(cast.target, casterCell);

        if (!hasExplicitActorTarget(cast.target))
            return true;

        if (serverCell != nullptr)
            return findActorTarget(*serverCell, cast.target) != nullptr;

        if (playerCaster != nullptr)
        {
            const auto [targetCell, targetActor] = findLoadedActorTarget(*playerCaster, cast.target);
            return targetCell != nullptr && targetActor != nullptr;
        }

        return false;
    }

    bool getAiTargetPosition(Cell& cell, const mwmp::Target& target, ESM::Position& destination)
    {
        if (target.isPlayer)
        {
            Player* player = Players::getPlayer(target.guid);
            if (player == nullptr || !player->hasFinitePositionPacket())
                return false;

            if (!isLivePlayerAiTarget(*player))
                return false;

            if (getCellSimulationKey(player->cell) != getCellSimulationKey(cell.getCellData()))
                return false;

            destination = player->position;
            return true;
        }

        mwmp::BaseActor* actor = findActorTarget(cell, target);
        if (actor == nullptr || !actor->hasPositionData || !hasFiniteWorldPosition(actor->position))
            return false;

        destination = actor->position;
        return true;
    }

    float getAiStopDistance(const mwmp::BaseActor& actor, bool coordinatePackage)
    {
        if (coordinatePackage)
            return aiCoordinateStopDistance;

        if (actor.aiDistance == 0)
            return aiTargetStopDistance;

        return std::clamp(static_cast<float>(actor.aiDistance), aiMinimumStopDistance, aiMaximumStopDistance);
    }

    bool buildAiMovementIntent(Cell& cell, mwmp::BaseActor& actor, ESM::Position& direction)
    {
        if (!actor.hasAiData || !actor.hasPositionData)
            return false;

        ESM::Position destination;
        bool coordinatePackage = false;

        switch (actor.aiAction)
        {
            case mwmp::BaseActorList::TRAVEL:
            case mwmp::BaseActorList::ESCORT:
                if (!hasFiniteWorldPosition(actor.aiCoordinates))
                    return false;
                destination = actor.aiCoordinates;
                coordinatePackage = true;
                break;

            case mwmp::BaseActorList::ACTIVATE:
            case mwmp::BaseActorList::COMBAT:
            case mwmp::BaseActorList::FOLLOW:
                if (!actor.hasAiTarget || !getAiTargetPosition(cell, actor.aiTarget, destination))
                    return false;
                break;

            default:
                return false;
        }

        direction = zeroPosition();

        const float deltaX = destination.pos[0] - actor.position.pos[0];
        const float deltaY = destination.pos[1] - actor.position.pos[1];
        const float distanceSquared = squaredHorizontalLength(deltaX, deltaY);
        if (!std::isfinite(distanceSquared))
            return true;

        const float stopDistance = getAiStopDistance(actor, coordinatePackage);
        if (distanceSquared <= stopDistance * stopDistance)
            return true;

        actor.position.rot[2] = std::atan2(deltaX, deltaY);
        direction.pos[1] = 1.f;
        sanitizeFinitePosition(actor.position);
        return true;
    }

    void chooseWanderDestination(const std::string& cellKey, unsigned int refNum, unsigned int mpNum,
        mwmp::BaseActor& actor, mwmp::ActorWanderState& wanderState)
    {
        if (!wanderState.hasOrigin || !hasFiniteWorldPosition(wanderState.origin))
        {
            wanderState.origin = actor.position;
            sanitizeFinitePosition(wanderState.origin);
            wanderState.hasOrigin = true;
        }

        const std::uint32_t sequence = wanderState.decisionSequence++;
        const float wanderDistance = std::clamp(
            static_cast<float>(actor.aiDistance), 0.f, aiMaximumStopDistance);
        const float angle = getUnitWanderValue(getActorWanderHash(cellKey, refNum, mpNum, sequence, 0x01u)) * twoPi;
        const float distance = std::sqrt(
            getUnitWanderValue(getActorWanderHash(cellKey, refNum, mpNum, sequence, 0x02u))) * wanderDistance;

        wanderState.destination = wanderState.origin;
        wanderState.destination.pos[0] += std::sin(angle) * distance;
        wanderState.destination.pos[1] += std::cos(angle) * distance;
        sanitizeFinitePosition(wanderState.destination);
        wanderState.hasDestination = true;

        const float decisionWindow = aiWanderMaximumDecisionSeconds - aiWanderMinimumDecisionSeconds;
        wanderState.remainingDecisionSeconds = aiWanderMinimumDecisionSeconds
            + getUnitWanderValue(getActorWanderHash(cellKey, refNum, mpNum, sequence, 0x03u)) * decisionWindow;
    }

    bool buildWanderMovementIntent(const std::string& cellKey, unsigned int refNum, unsigned int mpNum,
        mwmp::BaseActor& actor, mwmp::ActorWanderState& wanderState, float deltaSeconds, ESM::Position& direction)
    {
        if (!actor.hasAiData || actor.aiAction != mwmp::BaseActorList::WANDER
            || !actor.hasPositionData || !hasFiniteWorldPosition(actor.position))
            return false;

        if (!wanderState.hasOrigin || !hasFiniteWorldPosition(wanderState.origin))
        {
            wanderState.origin = actor.position;
            sanitizeFinitePosition(wanderState.origin);
            wanderState.hasOrigin = true;
            wanderState.hasDestination = false;
        }

        wanderState.remainingDecisionSeconds = std::max(0.f, wanderState.remainingDecisionSeconds - std::max(0.f, deltaSeconds));

        if (!wanderState.hasDestination || !hasFiniteWorldPosition(wanderState.destination)
            || wanderState.remainingDecisionSeconds <= 0.f)
            chooseWanderDestination(cellKey, refNum, mpNum, actor, wanderState);

        direction = zeroPosition();

        const float deltaX = wanderState.destination.pos[0] - actor.position.pos[0];
        const float deltaY = wanderState.destination.pos[1] - actor.position.pos[1];
        const float distanceSquared = squaredHorizontalLength(deltaX, deltaY);
        if (!std::isfinite(distanceSquared))
        {
            wanderState.hasDestination = false;
            return true;
        }

        if (distanceSquared <= aiWanderStopDistance * aiWanderStopDistance)
            return true;

        actor.position.rot[2] = std::atan2(deltaX, deltaY);
        direction.pos[1] = 1.f;
        sanitizeFinitePosition(actor.position);
        return true;
    }

    bool hasServerOwnedActorMovement(const mwmp::BaseActor& actor)
    {
        if (!actor.hasAiData || !actor.hasPositionData)
            return false;

        switch (actor.aiAction)
        {
            case mwmp::BaseActorList::CANCEL:
            case mwmp::BaseActorList::WANDER:
            case mwmp::BaseActorList::TRAVEL:
            case mwmp::BaseActorList::ESCORT:
            case mwmp::BaseActorList::ACTIVATE:
            case mwmp::BaseActorList::COMBAT:
            case mwmp::BaseActorList::FOLLOW:
                return true;

            default:
                return false;
        }
    }

    bool hasValidAiTarget(Cell& cell, const mwmp::BaseActor& actor)
    {
        if (!actor.hasAiTarget)
            return false;

        ESM::Position unusedDestination;
        return getAiTargetPosition(cell, actor.aiTarget, unusedDestination);
    }

    bool hasValidActorAiSnapshot(Cell& cell, const mwmp::BaseActor& actor)
    {
        switch (actor.aiAction)
        {
            case mwmp::BaseActorList::CANCEL:
                return true;

            case mwmp::BaseActorList::WANDER:
                return true;

            case mwmp::BaseActorList::TRAVEL:
                return hasFiniteWorldPosition(actor.aiCoordinates);

            case mwmp::BaseActorList::ACTIVATE:
            case mwmp::BaseActorList::COMBAT:
            case mwmp::BaseActorList::FOLLOW:
                return hasValidAiTarget(cell, actor);

            case mwmp::BaseActorList::ESCORT:
                return hasFiniteWorldPosition(actor.aiCoordinates) && hasValidAiTarget(cell, actor);

            default:
                return false;
        }
    }

    mwmp::BaseActor buildServerAcceptedAiActor(Cell& cell, const mwmp::BaseActor& incomingActor,
        const mwmp::BaseActor& currentActor)
    {
        mwmp::BaseActor acceptedActor = incomingActor;
        acceptedActor.refId = currentActor.refId.empty() ? incomingActor.refId : currentActor.refId;
        acceptedActor.cell = cell.getCellData();
        acceptedActor.hasAiData = true;

        if (currentActor.hasPositionData)
        {
            acceptedActor.hasPositionData = true;
            acceptedActor.positionSequence = currentActor.positionSequence;
            acceptedActor.position = currentActor.position;
            acceptedActor.direction = currentActor.direction;
            acceptedActor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                currentActor.movementSampleIntervalSeconds);
            acceptedActor.movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(
                currentActor.movementLatencySeconds);
        }
        else if (incomingActor.hasPositionData && isFiniteActorMovementSnapshot(incomingActor))
        {
            acceptedActor.hasPositionData = true;
            sanitizeFinitePosition(acceptedActor.direction);
            acceptedActor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(
                incomingActor.movementSampleIntervalSeconds);
            acceptedActor.movementLatencySeconds = mwmp::sanitizeMovementLatencySeconds(
                incomingActor.movementLatencySeconds);
        }
        else
            acceptedActor.hasPositionData = false;

        return acceptedActor;
    }

    bool isPlayerFollowerPackage(const mwmp::BaseActor& actor, mwmp::PacketGuid playerGuid)
    {
        return actor.hasAiData && actor.hasAiTarget && actor.aiTarget.isPlayer && actor.aiTarget.guid == playerGuid
            && (actor.aiAction == mwmp::BaseActorList::FOLLOW || actor.aiAction == mwmp::BaseActorList::ESCORT);
    }

    ESM::Position makeFollowerCellChangePosition(const Player& player, std::size_t followerIndex)
    {
        ESM::Position position = player.position;
        sanitizeFinitePosition(position);

        const float yaw = std::isfinite(position.rot[2]) ? position.rot[2] : 0.f;
        const float sinYaw = std::sin(yaw);
        const float cosYaw = std::cos(yaw);

        const float forwardX = sinYaw;
        const float forwardY = cosYaw;
        const float rightX = cosYaw;
        const float rightY = -sinYaw;

        const int lane = static_cast<int>(followerIndex % 3) - 1;
        const int row = static_cast<int>(followerIndex / 3);
        const float behind = followerCellChangeBehindDistance + followerCellChangeRowSpacing * row;
        const float lateral = followerCellChangeColumnSpacing * lane;

        position.pos[0] += rightX * lateral - forwardX * behind;
        position.pos[1] += rightY * lateral - forwardY * behind;

        sanitizeFinitePosition(position);
        return position;
    }

    ESM::Position zeroMovementDirectionLike(const ESM::Position& position)
    {
        ESM::Position direction = zeroPosition();
        direction.rot[0] = position.rot[0];
        direction.rot[1] = position.rot[1];
        direction.rot[2] = position.rot[2];
        return direction;
    }

    class ScopedReceivedActorList
    {
    public:
        explicit ScopedReceivedActorList(const mwmp::BaseActorList& actorList)
            : mReceivedActorList(mwmp::ServerNetworking::getPtr()->getReceivedActorList())
            , mPreviousActorList(*mReceivedActorList)
        {
            *mReceivedActorList = actorList;
        }

        ~ScopedReceivedActorList()
        {
            *mReceivedActorList = mPreviousActorList;
        }

        ScopedReceivedActorList(const ScopedReceivedActorList&) = delete;
        ScopedReceivedActorList& operator=(const ScopedReceivedActorList&) = delete;

    private:
        mwmp::BaseActorList* mReceivedActorList;
        mwmp::BaseActorList mPreviousActorList;
    };

    void persistServerGeneratedActorCellChange(Player& player, mwmp::BaseActorList& actorList)
    {
        const std::string sourceCellDescription = actorList.cell.getDescription();
        ScopedReceivedActorList receivedActorList(actorList);
        mwmp::ServerEvents::actorCellChange(player.getId(), sourceCellDescription.c_str());
    }

    void moveFollowingActorsAcrossPlayerCellChange(Player& player, const ESM::Cell& sourceCellData)
    {
        if (isSameSimulationCell(sourceCellData, player.cell))
            return;

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return;

        ESM::Cell sourceLookupCell = sourceCellData;
        Cell* sourceCell = cellController->getCell(&sourceLookupCell);
        if (sourceCell == nullptr)
            return;

        mwmp::BaseActorList* sourceActors = sourceCell->getActorList();
        if (sourceActors == nullptr || sourceActors->baseActors.empty())
            return;

        mwmp::BaseActorList movedFollowers;
        movedFollowers.cell = sourceCell->getCellData();
        movedFollowers.guid = player.guid;

        std::size_t followerIndex = 0;
        for (const mwmp::BaseActor& actor : sourceActors->baseActors)
        {
            if (!isClientActorControlUpdateAllowed(&actor) || !isPlayerFollowerPackage(actor, player.guid))
                continue;

            mwmp::BaseActor movedActor = actor;
            movedActor.cell = player.cell;
            movedActor.position = makeFollowerCellChangePosition(player, followerIndex);
            movedActor.direction = zeroMovementDirectionLike(movedActor.position);
            movedActor.isFollowerCellChange = true;
            movedActor.hasPositionData = true;
            ++movedActor.positionSequence;
            movedFollowers.baseActors.push_back(movedActor);
            ++followerIndex;
        }

        movedFollowers.count = static_cast<unsigned int>(movedFollowers.baseActors.size());
        if (movedFollowers.count == 0)
            return;

        persistServerGeneratedActorCellChange(player, movedFollowers);

        mwmp::ActorProcessor::cacheCellChange(movedFollowers);
        mwmp::ActorPacket* actorPacket = mwmp::ServerNetworking::get().getActorPacketController()->GetPacket(
            ID_ACTOR_CELL_CHANGE);
        actorPacket->setActorList(&movedFollowers);
        mwmp::ActorProcessor::sendCellChangeToLoaded(*actorPacket, movedFollowers);
        actorPacket->Send(player.guid);
    }

    bool applyHealthDamageToPlayer(Player& target, float damage, bool& becameDead)
    {
        becameDead = false;
        if (!target.hasFiniteDynamicStats())
            return false;

        float& health = target.creatureStats.mDynamic[0].mCurrent;
        if (target.creatureStats.mDead || !std::isfinite(health) || health <= healthDeadEpsilon)
            return false;

        health = std::max(0.f, health - damage);
        target.creatureStats.mDead = health <= healthDeadEpsilon;
        becameDead = target.creatureStats.mDead;
        ++target.statsDynamicSequence;
        target.exchangeFullInfo = false;
        target.statsDynamicIndexChanges.clear();
        target.statsDynamicIndexChanges.push_back(0);
        target.acceptCurrentStatsDynamicPacket();
        return true;
    }

    bool applyFatigueDamageToPlayer(Player& target, float damage)
    {
        if (!target.hasFiniteDynamicStats())
            return false;

        float& fatigue = target.creatureStats.mDynamic[2].mCurrent;
        if (target.creatureStats.mDead || !std::isfinite(fatigue))
            return false;

        fatigue -= damage;
        target.creatureStats.mKnockdown = target.creatureStats.mKnockdown || fatigue <= 0.f;
        ++target.statsDynamicSequence;
        target.exchangeFullInfo = false;
        target.statsDynamicIndexChanges.clear();
        target.statsDynamicIndexChanges.push_back(2);
        target.acceptCurrentStatsDynamicPacket();
        return true;
    }

    bool applyAttackDamageToPlayer(Player& target, const mwmp::Attack& attack, bool& becameDead)
    {
        const float damage = getServerAttackDamage(attack);
        if (shouldApplyAttackHealthDamage(attack, target.creatureStats))
            return applyHealthDamageToPlayer(target, damage, becameDead);

        becameDead = false;
        return applyFatigueDamageToPlayer(target, damage);
    }

    bool applyHealthDamageToActor(mwmp::BaseActor& target, float damage)
    {
        if (!target.hasStatsDynamicData || !mwmp::hasFiniteActorDynamicStats(target))
            return false;

        float& health = target.creatureStats.mDynamic[0].mCurrent;
        if (!std::isfinite(health) || health <= healthDeadEpsilon)
            return false;

        health = std::max(0.f, health - damage);
        target.creatureStats.mDead = health <= healthDeadEpsilon;
        if (target.creatureStats.mDead)
            target.creatureStats.mDeathAnimationFinished = false;
        ++target.statsDynamicSequence;
        target.hasStatsDynamicData = true;
        return true;
    }

    bool applyFatigueDamageToActor(mwmp::BaseActor& target, float damage)
    {
        if (!target.hasStatsDynamicData || !mwmp::hasFiniteActorDynamicStats(target))
            return false;

        float& fatigue = target.creatureStats.mDynamic[2].mCurrent;
        if (!std::isfinite(fatigue))
            return false;

        fatigue -= damage;
        ++target.statsDynamicSequence;
        target.hasStatsDynamicData = true;
        return true;
    }

    bool applyAttackDamageToActor(mwmp::BaseActor& target, const mwmp::Attack& attack)
    {
        const float damage = getServerAttackDamage(attack);
        if (shouldApplyAttackHealthDamage(attack, target.creatureStats))
            return applyHealthDamageToActor(target, damage);

        return applyFatigueDamageToActor(target, damage);
    }

    void broadcastPlayerStats(Player& target)
    {
        mwmp::PlayerPacket* statsPacket = mwmp::ServerNetworking::get().getPlayerPacketController()->GetPacket(
            ID_PLAYER_STATS_DYNAMIC);
        statsPacket->setPlayer(&target);
        statsPacket->Send(target.guid);
        target.sendToLoaded(statsPacket);
    }

    void broadcastActorStats(Cell& cell, const mwmp::BaseActor& target)
    {
        mwmp::BaseActorList statsList;
        statsList.cell = cell.getCellData();
        statsList.guid = mwmp::unassignedPacketGuid();
        statsList.baseActors.push_back(target);
        statsList.count = static_cast<unsigned int>(statsList.baseActors.size());

        mwmp::ActorPacket* statsPacket = mwmp::ServerNetworking::get().getActorPacketController()->GetPacket(
            ID_ACTOR_STATS_DYNAMIC);
        statsPacket->setActorList(&statsList);
        cell.sendToLoaded(statsPacket, &statsList);
    }

    void notifyPlayerDeath(Player& target)
    {
        mwmp::ServerEvents::playerDeath(target.getId());
    }

    void notifyPlayerStatsDynamic(Player& target)
    {
        mwmp::ServerEvents::playerStatsDynamic(target.getId());
    }

    void notifyActorStatsDynamic(Player& source, Cell& cell)
    {
        mwmp::ServerEvents::actorStatsDynamic(source.getId(), cell.getCellData().getDescription().c_str());
    }
}

namespace mwmp
{
    ServerSimulation::ServerSimulation()
        : mRuntime(createSimulationRuntime())
        , mLastTick(Clock::now())
    {
        QuestDatabaseStore::get().ensureLoaded();
        QuestEventJournalStore::get().ensureOpened();

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Server simulation runtime requested=%s active=%s openmwLinked=%s headlessEngine=%s openmwWorld=%s "
            "actorAuthority=%s",
            mRuntime->requestedName(), mRuntime->activeName(), mRuntime->topology().linksOpenMwCore ? "yes" : "no",
            mRuntime->hasHeadlessOpenMwEngine() ? "yes" : "no", mRuntime->hasOpenMwWorld() ? "yes" : "no",
            mRuntime->canOwnActorAuthority() ? "yes" : "no");
    }

    void ServerSimulation::tick()
    {
        const Clock::time_point now = Clock::now();
        const float deltaSeconds = clampDeltaSeconds(std::chrono::duration<float>(now - mLastTick).count());
        mLastTick = now;

        mRuntime->tick(deltaSeconds);
        std::vector<BaseActorList> runtimeActorSnapshots;
        const bool exportedRuntimeActorSnapshots = mRuntime->collectActorSnapshots(runtimeActorSnapshots);
        if (exportedRuntimeActorSnapshots)
            applyRuntimeActorSnapshots(runtimeActorSnapshots, deltaSeconds);

        if (!canAuthoritativelySimulateActors())
            return;

        if (exportedRuntimeActorSnapshots)
            return;

        mActorTickAccumulator += deltaSeconds;
        if (mActorTickAccumulator < actorTickIntervalSeconds)
            return;

        const float actorDeltaSeconds = std::min(mActorTickAccumulator, maxMovementStepSeconds);
        mActorTickAccumulator = 0.f;
        tickActors(actorDeltaSeconds);
    }

    void ServerSimulation::removePlayer(PacketGuid guid)
    {
        mPlayerMovementStates.erase(guid);
        mPlayerAcceptedCells.erase(guid);

        for (auto it = mShadowCellAuthority.begin(); it != mShadowCellAuthority.end();)
        {
            ShadowCellAuthorityState& state = it->second;
            const bool wasAuthority = state.authority == guid;
            const bool wasVisitor = eraseGuid(state.visitors, guid);

            if (!wasVisitor && !wasAuthority)
            {
                ++it;
                continue;
            }

            if (state.visitors.empty())
            {
                updateCellSimulationInterest(it->first, state);
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Cleared C++ shadow authority of cell %s because its final visitor disconnected",
                    it->first.c_str());
                it = mShadowCellAuthority.erase(it);
                continue;
            }

            updateCellSimulationInterest(it->first, state);
            broadcastCellActivityEvent(it->first, state);

            if (wasAuthority || !isShadowCellAuthorityCandidate(state, state.authority))
                refreshShadowCellAuthority(it->first, state, "current authority disconnected", unassignedPacketGuid(), guid);
            else
                broadcastShadowCellAuthorityEvent(it->first, state);

            ++it;
        }
    }

    void ServerSimulation::noteCellLoadedByPlayer(unsigned short playerId, std::string cellDescription)
    {
        if (cellDescription.empty())
            return;

        Player* player = Players::getPlayer(playerId);
        if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid))
            return;

        ShadowCellAuthorityState& state = mShadowCellAuthority[cellDescription];
        const bool hadVisitors = !state.visitors.empty();
        const bool wasVisitor = containsGuid(state.visitors, player->guid);
        const bool previousAuthorityWasCandidate = isShadowCellAuthorityCandidate(state, state.authority);

        if (!wasVisitor)
            state.visitors.push_back(player->guid);

        updateCellSimulationInterest(cellDescription, state);
        broadcastCellActivityEvent(cellDescription, state);

        if (canAuthoritativelySimulateActors())
        {
            state.authority = mwmp::unassignedPacketGuid();
            return;
        }

        if (!previousAuthorityWasCandidate)
        {
            const PacketGuid preferredGuid = hadVisitors ? mwmp::unassignedPacketGuid() : player->guid;
            refreshShadowCellAuthority(cellDescription, state, "cell authority was missing or stale", preferredGuid);
        }
        else if (!wasVisitor)
        {
            applyShadowCellAuthorityToLiveCell(cellDescription, state, true);
            sendShadowCellAuthorityEvent(*player, cellDescription, state);
        }
    }

    void ServerSimulation::noteCellUnloadedByPlayer(unsigned short playerId, std::string cellDescription)
    {
        if (cellDescription.empty())
            return;

        Player* player = Players::getPlayer(playerId);
        if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid))
            return;

        auto stateIt = mShadowCellAuthority.find(cellDescription);
        if (stateIt == mShadowCellAuthority.end())
            return;

        ShadowCellAuthorityState& state = stateIt->second;
        const bool wasAuthority = state.authority == player->guid;
        const bool wasVisitor = eraseGuid(state.visitors, player->guid);

        if (!wasVisitor)
            return;

        if (state.visitors.empty())
        {
            updateCellSimulationInterest(cellDescription, state);
            sendCellActivityEvent(*player, cellDescription, state, false);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Cleared C++ shadow authority of cell %s because no valid visitors remain", cellDescription.c_str());
            mShadowCellAuthority.erase(stateIt);
            return;
        }

        updateCellSimulationInterest(cellDescription, state);
        sendCellActivityEvent(*player, cellDescription, state, false);
        broadcastCellActivityEvent(cellDescription, state);

        if (wasAuthority || !isShadowCellAuthorityCandidate(state, state.authority))
            refreshShadowCellAuthority(cellDescription, state, "current authority left", unassignedPacketGuid(), player->guid);
        else
            broadcastShadowCellAuthorityEvent(cellDescription, state);
    }

    void ServerSimulation::auditShadowCellAuthority(const std::string& cellDescription, const char* context) const
    {
        if (cellDescription.empty() || canAuthoritativelySimulateActors())
            return;

        const std::optional<PacketGuid> shadowAuthority = getShadowCellAuthority(cellDescription);
        const std::size_t shadowVisitorCount = getShadowCellVisitorCount(cellDescription);
        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);

        if (liveCell == nullptr)
        {
            if (shadowAuthority || shadowVisitorCount > 0)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "C++ shadow authority for cell %s has %zu visitor(s) and authority %s after %s, but no live "
                    "server cell was found",
                    cellDescription.c_str(), shadowVisitorCount, shadowAuthorityAuditName(shadowAuthority).c_str(),
                    context != nullptr ? context : "cell event");
            }
            return;
        }

        const PacketGuid liveAuthority = *liveCell->getAuthority();
        const bool shadowAssigned = shadowAuthority && mwmp::isPacketGuidAssigned(*shadowAuthority);
        const bool liveAssigned = mwmp::isPacketGuidAssigned(liveAuthority);
        if (shadowAssigned == liveAssigned && (!shadowAssigned || *shadowAuthority == liveAuthority))
            return;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "C++ shadow authority mismatch for cell %s after %s: shadow=%s live=%s visitors=%zu",
            cellDescription.c_str(), context != nullptr ? context : "cell event",
            shadowAuthorityAuditName(shadowAuthority).c_str(), shadowAuthorityAuditName(liveAuthority).c_str(),
            shadowVisitorCount);
    }

    std::optional<PacketGuid> ServerSimulation::getShadowCellAuthority(const std::string& cellDescription) const
    {
        const auto stateIt = mShadowCellAuthority.find(cellDescription);
        if (stateIt == mShadowCellAuthority.end()
            || !mwmp::isPacketGuidAssigned(stateIt->second.authority))
            return std::nullopt;

        return stateIt->second.authority;
    }

    std::size_t ServerSimulation::getShadowCellVisitorCount(const std::string& cellDescription) const
    {
        const auto stateIt = mShadowCellAuthority.find(cellDescription);
        if (stateIt == mShadowCellAuthority.end())
            return 0;

        return stateIt->second.visitors.size();
    }

    void ServerSimulation::sendLuaBridgeState(Player& player) const
    {
        sendRuntimeStatusEvent(player);

        for (const auto& [cellDescription, state] : mShadowCellAuthority)
        {
            if (!containsGuid(state.visitors, player.guid))
                continue;

            sendCellActivityEvent(player, cellDescription, state, true);
            sendShadowCellAuthorityEvent(player, cellDescription, state);
        }

        sendLuaBridgeReadyEvent(player);
    }

    SimulationRuntime& ServerSimulation::runtime()
    {
        return *mRuntime;
    }

    const SimulationRuntime& ServerSimulation::runtime() const
    {
        return *mRuntime;
    }

    bool ServerSimulation::canAuthoritativelySimulateActors() const
    {
        return mRuntime != nullptr && mRuntime->canOwnActorAuthority();
    }

    bool ServerSimulation::isShadowCellAuthorityCandidate(
        const ShadowCellAuthorityState& state, PacketGuid guid) const
    {
        if (!mwmp::isPacketGuidAssigned(guid) || !containsGuid(state.visitors, guid))
            return false;

        Player* player = Players::getPlayer(guid);
        return player != nullptr && player->getLoadState() != Player::KICKED;
    }

    PacketGuid ServerSimulation::getLowestPingShadowCellAuthority(
        const ShadowCellAuthorityState& state, PacketGuid excludedGuid) const
    {
        PacketGuid bestGuid = mwmp::unassignedPacketGuid();
        std::optional<int> bestPing;
        ServerNetworking* networking = ServerNetworking::getPtr();

        for (PacketGuid visitorGuid : state.visitors)
        {
            if (visitorGuid == excludedGuid || !isShadowCellAuthorityCandidate(state, visitorGuid))
                continue;

            const int ping = networking != nullptr ? networking->getAvgPing(visitorGuid) : 0;
            if (!bestPing || ping < *bestPing)
            {
                bestPing = ping;
                bestGuid = visitorGuid;
            }
        }

        return bestGuid;
    }

    PacketGuid ServerSimulation::refreshShadowCellAuthority(const std::string& cellDescription,
        ShadowCellAuthorityState& state, const char* reason, PacketGuid preferredGuid, PacketGuid excludedGuid)
    {
        if (canAuthoritativelySimulateActors())
        {
            const bool wasAssigned = mwmp::isPacketGuidAssigned(state.authority);
            state.authority = mwmp::unassignedPacketGuid();
            if (wasAssigned)
                broadcastShadowCellAuthorityEvent(cellDescription, state);
            return state.authority;
        }

        PacketGuid newAuthority = mwmp::unassignedPacketGuid();
        if (preferredGuid != excludedGuid && isShadowCellAuthorityCandidate(state, preferredGuid))
            newAuthority = preferredGuid;
        else
            newAuthority = getLowestPingShadowCellAuthority(state, excludedGuid);

        if (!mwmp::isPacketGuidAssigned(newAuthority))
        {
            if (mwmp::isPacketGuidAssigned(state.authority))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Cleared C++ shadow authority of cell %s because no valid visitors remain",
                    cellDescription.c_str());
            }
            state.authority = mwmp::unassignedPacketGuid();
            broadcastShadowCellAuthorityEvent(cellDescription, state);
            return state.authority;
        }

        const bool authorityChanged = state.authority != newAuthority;
        if (authorityChanged)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Assigning C++ shadow authority of cell %s to %s because %s",
                cellDescription.c_str(), shadowAuthorityName(newAuthority).c_str(),
                reason != nullptr ? reason : "authority was refreshed");
        }

        state.authority = newAuthority;
        if (authorityChanged)
        {
            applyShadowCellAuthorityToLiveCell(cellDescription, state);
            broadcastShadowCellAuthorityEvent(cellDescription, state);
        }
        return state.authority;
    }

    bool ServerSimulation::applyShadowCellAuthorityToLiveCell(const std::string& cellDescription,
        const ShadowCellAuthorityState& state, bool forceBroadcast) const
    {
        if (canAuthoritativelySimulateActors() || !mwmp::isPacketGuidAssigned(state.authority))
            return false;

        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        if (liveCell == nullptr)
            return false;

        if (!isShadowCellAuthorityCandidate(state, state.authority))
            return false;

        const bool authorityChanged = !liveCell->hasAuthority(state.authority);
        if (!authorityChanged && !forceBroadcast)
            return false;

        liveCell->setAuthority(state.authority);

        BaseActorList authorityList;
        authorityList.cell = liveCell->getCellData();
        authorityList.guid = state.authority;
        authorityList.baseActors.clear();
        authorityList.count = 0;

        ServerNetworking* networking = ServerNetworking::getPtr();
        if (networking == nullptr || networking->getActorPacketController() == nullptr)
            return false;

        ActorPacket* actorPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_AUTHORITY);
        if (actorPacket == nullptr)
            return false;

        actorPacket->setActorList(&authorityList);

        // Mirror the legacy Lua SendActorAuthority broadcast semantics while
        // the authority decision itself migrates into C++.
        actorPacket->Send(false);
        actorPacket->Send(true);

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Applied C++ client actor authority of cell %s to %s%s",
            cellDescription.c_str(), shadowAuthorityName(state.authority).c_str(),
            forceBroadcast && !authorityChanged ? " for joining visitor" : "");

        if (authorityChanged)
            requestActorListSnapshotFromAuthority(cellDescription, state, *liveCell,
                liveCell->hasActorListSnapshot() ? "authority changed" : "initial authority assignment");
        else if (!liveCell->hasActorListSnapshot())
            requestActorListSnapshotFromAuthority(cellDescription, state, *liveCell,
                "cell still needs an initial actor snapshot");

        return true;
    }

    bool ServerSimulation::requestActorListSnapshotFromAuthority(const std::string& cellDescription,
        const ShadowCellAuthorityState& state, Cell& liveCell, const char* reason) const
    {
        if (canAuthoritativelySimulateActors() || !mwmp::isPacketGuidAssigned(state.authority))
            return false;

        if (!isShadowCellAuthorityCandidate(state, state.authority))
            return false;

        if (liveCell.hasPendingActorListRequestFrom(state.authority))
            return false;

        if (liveCell.hasPendingActorListRequest() && liveCell.hasActorListSnapshot())
            return false;

        ServerNetworking* networking = ServerNetworking::getPtr();
        if (networking == nullptr || networking->getActorPacketController() == nullptr)
            return false;

        ActorPacket* actorPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_LIST);
        if (actorPacket == nullptr)
            return false;

        BaseActorList requestList;
        requestList.cell = liveCell.getCellData();
        requestList.guid = state.authority;
        requestList.action = BaseActorList::REQUEST;
        requestList.baseActors.clear();
        requestList.count = 0;
        requestList.isValid = true;

        liveCell.requestActorListFrom(state.authority);
        actorPacket->setActorList(&requestList);
        actorPacket->Send(state.authority);

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Requested C++ actor list snapshot for cell %s from %s because %s",
            cellDescription.c_str(), shadowAuthorityName(state.authority).c_str(),
            reason != nullptr ? reason : "the authority needs to refresh server state");
        return true;
    }

    void ServerSimulation::sendShadowCellAuthorityEvent(Player& player, const std::string& cellDescription,
        const ShadowCellAuthorityState& state) const
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED)
            return;

        const bool hasAuthority = mwmp::isPacketGuidAssigned(state.authority);
        const bool isAuthority = hasAuthority && state.authority == player.guid;
        const std::string authorityGuid = hasAuthority ? mwmp::packetGuidToString(state.authority) : "";
        const std::string authorityName = hasAuthority ? shadowAuthorityName(state.authority) : "";
        const Cell* serverCell = findLoadedServerCellByDescription(cellDescription);
        const std::string cellKey = serverCell != nullptr ? getLuaCellKey(serverCell->getCellData()) : "";
        const std::string serverCellKey = serverCell != nullptr ? getCellSimulationKey(serverCell->getCellData()) : "";

        std::string payload;
        payload.reserve(240 + cellDescription.size() + cellKey.size() + serverCellKey.size()
            + authorityGuid.size() + authorityName.size());
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"cell_authority\",\"cellDescription\":";
        payload += jsonString(cellDescription);
        payload += ",\"cellKey\":";
        payload += jsonString(cellKey);
        payload += ",\"serverCellKey\":";
        payload += jsonString(serverCellKey);
        payload += ",\"authorityGuid\":";
        payload += jsonString(authorityGuid);
        payload += ",\"authorityName\":";
        payload += jsonString(authorityName);
        payload += ",\"isAuthority\":";
        payload += jsonBool(isAuthority);
        payload += ",\"visitorCount\":";
        payload += std::to_string(state.visitors.size());
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(canAuthoritativelySimulateActors());
        payload += "}";

        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "cell_authority", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send C++ shadow authority event for cell %s to %s",
                cellDescription.c_str(), player.npc.mName.c_str());
        }
    }

    void ServerSimulation::broadcastShadowCellAuthorityEvent(
        const std::string& cellDescription, const ShadowCellAuthorityState& state) const
    {
        for (PacketGuid visitorGuid : state.visitors)
        {
            Player* visitor = Players::getPlayer(visitorGuid);
            if (visitor != nullptr)
                sendShadowCellAuthorityEvent(*visitor, cellDescription, state);
        }
    }

    bool ServerSimulation::updateCellSimulationInterest(const std::string& cellDescription,
        const ShadowCellAuthorityState& state) const
    {
        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        if (liveCell == nullptr)
            return false;

        const bool enabled = canAuthoritativelySimulateActors() && !state.visitors.empty();
        if (liveCell->hasSimulationInterest() != enabled)
        {
            liveCell->setSimulationInterest(enabled);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "C++ %s server simulation interest for cell %s with %zu active visitor(s)",
                enabled ? "enabled" : "disabled", cellDescription.c_str(), state.visitors.size());
        }

        return liveCell->hasSimulationInterest();
    }

    void ServerSimulation::applyRuntimeActorSnapshots(
        const std::vector<BaseActorList>& actorLists, float deltaSeconds)
    {
        if (actorLists.empty())
            return;

        CellController* cellController = CellController::get();
        ServerNetworking* networking = ServerNetworking::getPtr();
        if (cellController == nullptr || networking == nullptr || networking->getActorPacketController() == nullptr)
            return;

        ActorPacket* listPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_LIST);
        ActorPacket* positionPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_POSITION);
        ActorPacket* statsPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_STATS_DYNAMIC);
        if (listPacket == nullptr || positionPacket == nullptr || statsPacket == nullptr)
            return;

        const float sampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);

        for (const BaseActorList& runtimeList : actorLists)
        {
            if (runtimeList.baseActors.empty())
                continue;

            ESM::Cell lookupCell = runtimeList.cell;
            Cell* serverCell = cellController->getCell(&lookupCell);
            if (serverCell == nullptr)
                serverCell = cellController->addCell(lookupCell);
            if (serverCell == nullptr)
                continue;

            const bool hadActorListSnapshot = serverCell->hasActorListSnapshot();

            BaseActorList identityList = runtimeList;
            identityList.guid = unassignedPacketGuid();
            identityList.action = BaseActorList::SET;
            identityList.count = static_cast<unsigned int>(identityList.baseActors.size());
            serverCell->readActorList(ID_ACTOR_LIST, &identityList);

            if (!hadActorListSnapshot)
            {
                listPacket->setActorList(&identityList);
                serverCell->sendToLoaded(listPacket, &identityList);
            }

            BaseActorList positionList;
            positionList.cell = runtimeList.cell;
            positionList.guid = unassignedPacketGuid();
            positionList.action = BaseActorList::SET;
            positionList.isValid = true;

            BaseActorList statsList;
            statsList.cell = runtimeList.cell;
            statsList.guid = unassignedPacketGuid();
            statsList.action = BaseActorList::SET;
            statsList.isValid = true;

            for (const BaseActor& runtimeActor : runtimeList.baseActors)
            {
                BaseActor* cachedActor = serverCell->getActor(runtimeActor.refNum, runtimeActor.mpNum);

                if (runtimeActor.hasPositionData)
                {
                    BaseActor positionActor = runtimeActor;
                    positionActor.hasPositionData = true;
                    positionActor.positionSequence = cachedActor != nullptr && cachedActor->hasPositionData
                        ? cachedActor->positionSequence + 1
                        : 1;
                    positionActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                    positionActor.movementLatencySeconds = 0.f;
                    positionList.baseActors.push_back(std::move(positionActor));
                }

                if (runtimeActor.hasStatsDynamicData)
                {
                    BaseActor statsActor = runtimeActor;
                    statsActor.hasStatsDynamicData = true;
                    statsActor.statsDynamicSequence = cachedActor != nullptr && cachedActor->hasStatsDynamicData
                        ? cachedActor->statsDynamicSequence + 1
                        : 1;
                    statsList.baseActors.push_back(std::move(statsActor));
                }
            }

            positionList.count = static_cast<unsigned int>(positionList.baseActors.size());
            if (positionList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_POSITION, &positionList);
                positionPacket->setActorList(&positionList);
                serverCell->sendToLoaded(positionPacket, &positionList);
            }

            statsList.count = static_cast<unsigned int>(statsList.baseActors.size());
            if (statsList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_STATS_DYNAMIC, &statsList);
                statsPacket->setActorList(&statsList);
                serverCell->sendToLoaded(statsPacket, &statsList);
            }
        }
    }

    void ServerSimulation::sendCellActivityEvent(Player& player, const std::string& cellDescription,
        const ShadowCellAuthorityState& state, bool localPlayerLoaded) const
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED)
            return;

        const Cell* serverCell = findLoadedServerCellByDescription(cellDescription);
        const std::string cellKey = serverCell != nullptr ? getLuaCellKey(serverCell->getCellData()) : "";
        const std::string serverCellKey = serverCell != nullptr ? getCellSimulationKey(serverCell->getCellData()) : "";
        const bool simulationInterest = serverCell != nullptr && serverCell->hasSimulationInterest();

        std::string payload;
        payload.reserve(260 + cellDescription.size() + cellKey.size() + serverCellKey.size());
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"cell_activity\",\"cellDescription\":";
        payload += jsonString(cellDescription);
        payload += ",\"cellKey\":";
        payload += jsonString(cellKey);
        payload += ",\"serverCellKey\":";
        payload += jsonString(serverCellKey);
        payload += ",\"visitorCount\":";
        payload += std::to_string(state.visitors.size());
        payload += ",\"localPlayerLoaded\":";
        payload += jsonBool(localPlayerLoaded);
        payload += ",\"simulationInterest\":";
        payload += jsonBool(simulationInterest);
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(canAuthoritativelySimulateActors());
        payload += ",\"runtimeRequested\":";
        payload += jsonString(runtime().requestedName());
        payload += ",\"runtimeActive\":";
        payload += jsonString(runtime().activeName());
        payload += "}";

        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "cell_activity", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send C++ cell activity event for cell %s to %s",
                cellDescription.c_str(), player.npc.mName.c_str());
        }
    }

    void ServerSimulation::broadcastCellActivityEvent(
        const std::string& cellDescription, const ShadowCellAuthorityState& state) const
    {
        for (PacketGuid visitorGuid : state.visitors)
        {
            Player* visitor = Players::getPlayer(visitorGuid);
            if (visitor != nullptr)
                sendCellActivityEvent(*visitor, cellDescription, state, true);
        }
    }

    void ServerSimulation::sendRuntimeStatusEvent(Player& player) const
    {
        const SimulationRuntimeCapabilities& runtimeCapabilities = runtime().capabilities();
        const SimulationRuntimeTopology& runtimeTopology = runtime().topology();
        const SimulationRuntimeBootstrap& runtimeBootstrap = runtime().bootstrap();
        const std::string authorityBlockReason = runtimeAuthorityBlockReason(runtime());
        const ServerContentRegistryStatistics serverContent = ServerContentRegistry::get().statistics();
        const QuestDatabaseStatistics questDatabase = QuestDatabaseStore::get().statistics();
        const QuestEventJournalStatistics questEventJournal = QuestEventJournalStore::get().statistics();
        const CellController* cellController = CellController::get();
        std::size_t loadedCellCount = 0;
        if (cellController != nullptr)
            loadedCellCount = cellController->getCells().size();

        std::size_t playerTrackedCellCount = 0;
        for (const auto& [cellDescription, state] : mShadowCellAuthority)
        {
            static_cast<void>(cellDescription);
            if (containsGuid(state.visitors, player.guid))
                ++playerTrackedCellCount;
        }

        std::string payload;
        payload.reserve(980);
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"runtime_status\"";
        payload += ",\"runtimeRequested\":";
        payload += jsonString(runtime().requestedName());
        payload += ",\"runtimeActive\":";
        payload += jsonString(runtime().activeName());
        payload += ",\"openmwWorld\":";
        payload += jsonBool(runtime().hasOpenMwWorld());
        payload += ",\"canSimulateActors\":";
        payload += jsonBool(runtime().canSimulateActors());
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(canAuthoritativelySimulateActors());
        payload += ",\"serverActorAuthorityBlockedBy\":";
        payload += jsonString(authorityBlockReason);
        payload += ",\"loadedCellCount\":";
        payload += std::to_string(loadedCellCount);
        payload += ",\"trackedCellCount\":";
        payload += std::to_string(mShadowCellAuthority.size());
        payload += ",\"playerTrackedCellCount\":";
        payload += std::to_string(playerTrackedCellCount);
        payload += ",\"movementPolicy\":{";
        payload += "\"firstStepSeconds\":";
        payload += std::to_string(firstMovementStepSeconds);
        payload += ",\"maxStepSeconds\":";
        payload += std::to_string(maxMovementStepSeconds);
        payload += ",\"maxPlayerMovementUnitsPerSecond\":";
        payload += std::to_string(maxPlayerMovementUnitsPerSecond);
        payload += ",\"maxPlayerVerticalUnitsPerSecond\":";
        payload += std::to_string(maxPlayerVerticalUnitsPerSecond);
        payload += ",\"playerCorrectionAllowance\":";
        payload += std::to_string(playerMovementCorrectionAllowance);
        payload += ",\"maxSampleGraceSeconds\":";
        payload += std::to_string(maxPlayerSampleGraceSeconds);
        payload += ",\"maxLatencyGraceSeconds\":";
        payload += std::to_string(maxPlayerLatencyGraceSeconds);
        payload += ",\"maxPlausibilityDeltaSeconds\":";
        payload += std::to_string(maxPlayerPlausibilityDeltaSeconds);
        payload += ",\"luaMovementHealthFreshnessSeconds\":";
        payload += std::to_string(luaMovementHealthFreshnessWindow.count());
        payload += ",\"luaCellObservationFreshnessSeconds\":";
        payload += std::to_string(luaObservationFreshnessWindow.count());
        payload += "}";
        payload += ",\"serverContentRegistry\":{";
        payload += "\"backend\":";
        payload += jsonString(serverContent.backend);
        payload += ",\"attempted\":";
        payload += jsonBool(serverContent.attempted);
        payload += ",\"loaded\":";
        payload += jsonBool(serverContent.loaded);
        payload += ",\"path\":";
        payload += jsonString(Files::pathToUnicodeString(serverContent.path));
        payload += ",\"lastError\":";
        payload += jsonString(serverContent.lastError);
        payload += ",\"dataFileCount\":";
        payload += std::to_string(serverContent.dataFileCount);
        payload += ",\"checksumCount\":";
        payload += std::to_string(serverContent.checksumCount);
        payload += ",\"contentPreview\":";
        std::vector<std::string> contentFiles;
        contentFiles.reserve(
            std::min(ServerContentRegistry::get().dataFiles().size(), runtimeStatusContentPreviewLimit));
        for (const ServerDataFileRequirement& requirement : ServerContentRegistry::get().dataFiles())
        {
            if (contentFiles.size() >= runtimeStatusContentPreviewLimit)
                break;
            contentFiles.push_back(requirement.name);
        }
        appendJsonStringArray(payload, contentFiles);
        payload += ",\"contentPreviewTruncated\":";
        payload += jsonBool(ServerContentRegistry::get().dataFiles().size() > runtimeStatusContentPreviewLimit);
        payload += "}";
        payload += ",\"questDatabase\":{";
        payload += "\"backend\":";
        payload += jsonString(questDatabase.backend);
        payload += ",\"attempted\":";
        payload += jsonBool(questDatabase.attempted);
        payload += ",\"loaded\":";
        payload += jsonBool(questDatabase.loaded);
        payload += ",\"rootPath\":";
        payload += jsonString(Files::pathToUnicodeString(questDatabase.rootPath));
        payload += ",\"lastError\":";
        payload += jsonString(questDatabase.lastError);
        payload += ",\"manifestCount\":";
        payload += std::to_string(questDatabase.manifestCount);
        payload += ",\"packageCount\":";
        payload += std::to_string(questDatabase.packageCount);
        payload += ",\"questDefinitionCount\":";
        payload += std::to_string(questDatabase.questDefinitionCount);
        payload += ",\"questStepCount\":";
        payload += std::to_string(questDatabase.questStepCount);
        payload += ",\"dialogueTopicCount\":";
        payload += std::to_string(questDatabase.dialogueTopicCount);
        payload += ",\"dialogueResponseCount\":";
        payload += std::to_string(questDatabase.dialogueResponseCount);
        payload += ",\"conditionCount\":";
        payload += std::to_string(questDatabase.conditionCount);
        payload += ",\"questEffectCount\":";
        payload += std::to_string(questDatabase.questEffectCount);
        payload += ",\"legacyEffectCount\":";
        payload += std::to_string(questDatabase.legacyEffectCount);
        payload += "}";
        payload += ",\"questEventJournal\":{";
        payload += "\"backend\":";
        payload += jsonString(questEventJournal.backend);
        payload += ",\"attempted\":";
        payload += jsonBool(questEventJournal.attempted);
        payload += ",\"available\":";
        payload += jsonBool(questEventJournal.available);
        payload += ",\"path\":";
        payload += jsonString(Files::pathToUnicodeString(questEventJournal.path));
        payload += ",\"lastError\":";
        payload += jsonString(questEventJournal.lastError);
        payload += ",\"eventCount\":";
        payload += std::to_string(questEventJournal.eventCount);
        payload += ",\"writeFailures\":";
        payload += std::to_string(questEventJournal.writeFailures);
        payload += "}";
        payload += ",\"questRuntimeEvaluator\":{";
        payload += "\"journalConditions\":";
        payload += jsonBool(QuestRuntimeEvaluator::get().supportsJournalConditions());
        payload += ",\"legacyEffectAnalysis\":";
        payload += jsonBool(QuestRuntimeEvaluator::get().supportsLegacyEffectAnalysis());
        payload += ",\"serverExecutableEffects\":";
        payload += jsonBool(QuestEffectExecutor::get().supportsServerExecutableEffects());
        payload += ",\"runtimeAuthorityMetadata\":true";
        payload += ",\"effectIdempotencyKeys\":true";
        payload += ",\"effectReplayProtection\":";
        payload += jsonBool(QuestEffectExecutor::get().supportsEffectReplayProtection());
        payload += ",\"nativeRuntimeModel\":";
        payload += jsonString("server-owned-multiplayer-quest-v1");
        payload += ",\"unsupportedConditionsRejectAuthoritativeSelection\":true";
        payload += ",\"inventoryEffectsRequireTransactions\":true";
        payload += ",\"actorEffectsRequireServerAuthority\":true";
        payload += "}";
        payload += ",\"topology\":{";
        payload += "\"unifiedExecutable\":";
        payload += jsonBool(runtimeTopology.unifiedExecutable);
        payload += ",\"linksOpenMwCore\":";
        payload += jsonBool(runtimeTopology.linksOpenMwCore);
        payload += ",\"hasHeadlessOpenMwEngine\":";
        payload += jsonBool(runtimeTopology.hasHeadlessOpenMwEngine);
        payload += ",\"runsOpenMwLua\":";
        payload += jsonBool(runtimeTopology.runsOpenMwLua);
        payload += ",\"rendererClientProtocol\":";
        payload += jsonBool(runtimeTopology.rendererClientProtocol);
        payload += "}";
        payload += ",\"openMwBootstrap\":{";
        payload += "\"canConfigureOpenMwApplication\":";
        payload += jsonBool(runtimeBootstrap.canConfigureOpenMwApplication);
        payload += ",\"canLoadOpenMwApplicationSettings\":";
        payload += jsonBool(runtimeBootstrap.canLoadOpenMwApplicationSettings);
        payload += ",\"hasOpenMwContentPlan\":";
        payload += jsonBool(runtimeBootstrap.hasOpenMwContentPlan);
        payload += ",\"contentPlanMatchesServerRegistry\":";
        payload += jsonBool(runtimeBootstrap.contentPlanMatchesServerRegistry);
        payload += ",\"usedServerContentFallback\":";
        payload += jsonBool(runtimeBootstrap.usedServerContentFallback);
        payload += ",\"contentRegistryLoaded\":";
        payload += jsonBool(runtimeBootstrap.contentRegistryLoaded);
        payload += ",\"contentFileCount\":";
        payload += std::to_string(runtimeBootstrap.contentFileCount);
        payload += ",\"resolvedOpenMwDataDirCount\":";
        payload += std::to_string(runtimeBootstrap.resolvedOpenMwDataDirCount);
        payload += ",\"resolvedOpenMwContentFileCount\":";
        payload += std::to_string(runtimeBootstrap.resolvedOpenMwContentFileCount);
        payload += ",\"missingServerContentFileCount\":";
        payload += std::to_string(runtimeBootstrap.missingServerContentFileCount);
        payload += ",\"extraOpenMwContentFileCount\":";
        payload += std::to_string(runtimeBootstrap.extraOpenMwContentFileCount);
        payload += ",\"engineArgumentCount\":";
        payload += std::to_string(runtimeBootstrap.engineArgumentCount);
        payload += ",\"blockedBy\":";
        payload += jsonString(runtimeBootstrap.blockedBy);
        payload += "}";
        payload += ",\"capabilities\":{";
        payload += "\"ownsWorldState\":";
        payload += jsonBool(runtimeCapabilities.ownsWorldState);
        payload += ",\"resolvesCells\":";
        payload += jsonBool(runtimeCapabilities.resolvesCells);
        payload += ",\"runsScripts\":";
        payload += jsonBool(runtimeCapabilities.runsScripts);
        payload += ",\"runsActorAi\":";
        payload += jsonBool(runtimeCapabilities.runsActorAi);
        payload += ",\"ownsActorMovement\":";
        payload += jsonBool(runtimeCapabilities.ownsActorMovement);
        payload += ",\"ownsActorCombat\":";
        payload += jsonBool(runtimeCapabilities.ownsActorCombat);
        payload += "}}";

        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "runtime_status", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send CommunityMP runtime status event to %s", player.npc.mName.c_str());
        }
    }

    void ServerSimulation::sendLuaBridgeReadyEvent(Player& player) const
    {
        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "ready", "{\"schema\":1,\"kind\":\"ready\"}"))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send CommunityMP Lua ready event to %s", player.npc.mName.c_str());
        }
    }

    bool ServerSimulation::acceptServerAuthoredPlayerState(Player& player, bool cellChangePacket)
    {
        if (!player.hasFinitePositionPacket())
        {
            if (player.hasAcceptedPositionPacket)
                player.restoreAcceptedPositionPacket();
            return false;
        }

        player.hasAcceptedPositionPacket = false;
        if (!player.acceptPositionPacket())
            return false;

        PlayerMovementState& movementState = mPlayerMovementStates[player.guid];
        movementState.lastMovementPacket = Clock::now();
        if (cellChangePacket)
        {
            movementState.lastServerCellChangePacket = movementState.lastMovementPacket;
            movementState.lastServerCellChangePosition = player.position;
            movementState.lastServerCellChangeDirection = player.direction;
            movementState.lastServerCellChangePositionSequence = player.positionSequence;
            movementState.hasServerCellChangePacket = true;
            movementState.hasVisualPosition = false;
        }
        else
            movementState.hasServerCellChangePacket = false;

        mPlayerAcceptedCells[player.guid] = player.cell;
        return true;
    }

    void ServerSimulation::sendPlayerVisualStateToLoaded(Player& player, PlayerPacket& packet)
    {
        PlayerMovementState& movementState = mPlayerMovementStates[player.guid];
        movementState.lastVisualPosition = player.position;
        movementState.lastVisualPositionSequence = player.positionSequence;
        movementState.hasVisualPosition = true;

        packet.setPlayer(&player);
        player.sendToLoaded(&packet);
    }

    bool ServerSimulation::isRedundantServerAuthoredPosition(const Player& player) const
    {
        const auto movementStateIt = mPlayerMovementStates.find(player.guid);
        if (movementStateIt == mPlayerMovementStates.end())
            return false;

        const PlayerMovementState& movementState = movementStateIt->second;
        if (!movementState.hasServerCellChangePacket)
            return false;

        constexpr float redundantCellPositionWindowSeconds = 1.f;
        const float ageSeconds = std::chrono::duration<float>(
            Clock::now() - movementState.lastServerCellChangePacket).count();
        if (!std::isfinite(ageSeconds) || ageSeconds > redundantCellPositionWindowSeconds)
            return false;

        return player.positionSequence == movementState.lastServerCellChangePositionSequence
            && positionsMatchWithinEpsilon(player.position, movementState.lastServerCellChangePosition)
            && positionsMatchWithinEpsilon(player.direction, movementState.lastServerCellChangeDirection);
    }

    float ServerSimulation::clampDeltaSeconds(float seconds)
    {
        return clampMovementDeltaSeconds(seconds);
    }

    bool ServerSimulation::acceptActorCasts(BaseActorList& actorList, Cell& serverCell)
    {
        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (!isActorCombatSequenceAllowed(*currentActor, actor))
                continue;

            normalizeActorMovementSnapshot(&serverCell, actor);
            if (!actor.hasPositionData)
                continue;

            if (!isAcceptedActorCastObservation(serverCell, *currentActor, actor))
                continue;

            acceptActorCombatSequence(*currentActor, actor);
            acceptedActors.push_back(actor);
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
        return actorList.count != 0;
    }

    bool ServerSimulation::acceptPlayerCast(Player& caster)
    {
        return isAcceptedCastTarget(caster.cast, caster.cell, &caster, nullptr);
    }

    bool ServerSimulation::acceptActorAiSnapshot(BaseActorList& actorList, Cell& serverCell)
    {
        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        for (const BaseActor& actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (!hasValidActorAiSnapshot(serverCell, actor))
                continue;

            acceptedActors.push_back(buildServerAcceptedAiActor(serverCell, actor, *currentActor));
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
        if (actorList.count == 0)
            return false;

        serverCell.readActorList(ID_ACTOR_AI, &actorList);
        return true;
    }

    bool ServerSimulation::acceptActorAttacks(BaseActorList& actorList, Cell& serverCell)
    {
        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());

        Player* source = Players::getPlayer(actorList.guid);

        for (BaseActor actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
            if (!isClientActorControlUpdateAllowed(currentActor))
                continue;

            if (!isActorCombatSequenceAllowed(*currentActor, actor))
                continue;

            normalizeActorMovementSnapshot(&serverCell, actor);
            if (!actor.hasPositionData)
                continue;

            if (!isAcceptedActorAttackObservation(serverCell, *currentActor, actor))
                continue;

            acceptActorCombatSequence(*currentActor, actor);
            acceptedActors.push_back(actor);

            const Attack& attack = actor.attack;
            if (!canApplyServerAttackDamage(attack))
                continue;

            if (attack.target.isPlayer)
            {
                Player* target = Players::getPlayer(attack.target.guid);
                bool becameDead = false;
                if (target != nullptr && applyAttackDamageToPlayer(*target, attack, becameDead))
                {
                    broadcastPlayerStats(*target);
                    notifyPlayerStatsDynamic(*target);
                    if (becameDead)
                        notifyPlayerDeath(*target);
                }
                continue;
            }

            BaseActor* target = findActorTarget(serverCell, attack.target);
            if (target != nullptr && applyAttackDamageToActor(*target, attack))
            {
                broadcastActorStats(serverCell, *target);
                if (source != nullptr)
                    notifyActorStatsDynamic(*source, serverCell);
            }
        }

        actorList.baseActors = std::move(acceptedActors);
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
        return actorList.count != 0;
    }

    void ServerSimulation::applyPlayerAttack(Player& attacker)
    {
        const Attack& attack = attacker.attack;
        if (!canApplyServerAttackDamage(attack))
            return;

        if (attack.target.isPlayer)
        {
            Player* target = Players::getPlayer(attack.target.guid);
            bool becameDead = false;
            if (target != nullptr && applyAttackDamageToPlayer(*target, attack, becameDead))
            {
                broadcastPlayerStats(*target);
                notifyPlayerStatsDynamic(*target);
                if (becameDead)
                    notifyPlayerDeath(*target);
            }
            return;
        }

        auto [targetCell, targetActor] = findLoadedActorTarget(attacker, attack.target);
        if (targetCell != nullptr && targetActor != nullptr && applyAttackDamageToActor(*targetActor, attack))
        {
            broadcastActorStats(*targetCell, *targetActor);
            notifyActorStatsDynamic(attacker, *targetCell);
        }
    }

    bool ServerSimulation::acceptActorMovementSnapshot(ActorPacket& packet, BaseActorList& actorList, Cell& serverCell)
    {
        std::vector<BaseActor> acceptedActors;
        acceptedActors.reserve(actorList.baseActors.size());
        std::map<ActorIdentityKey, std::size_t> acceptedActorIndexes;

        std::vector<BaseActor> correctionActors;
        correctionActors.reserve(actorList.baseActors.size());
        std::map<ActorIdentityKey, std::size_t> correctionActorIndexes;

        const std::string cellKey = getCellSimulationKey(actorList.cell);
        const Clock::time_point now = Clock::now();

        const auto addPositionCorrection = [&](const BaseActor& actor) {
            acceptNewestPositionActor(correctionActors, correctionActorIndexes, actor);
        };

        for (const BaseActor& actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
            if (currentActor == nullptr)
                continue;

            if (canAuthoritativelySimulateActors() && serverCell.hasSimulationInterest()
                && hasServerOwnedActorMovement(*currentActor))
            {
                addPositionCorrection(*currentActor);
                continue;
            }

            if (!isFiniteActorMovementSnapshot(actor))
            {
                if (currentActor->hasPositionData)
                    addPositionCorrection(*currentActor);
                continue;
            }

            if (!isClientActorControlUpdateAllowed(currentActor))
            {
                if (currentActor->hasPositionData)
                    addPositionCorrection(*currentActor);
                continue;
            }

            const bool hasNewerPosition = !currentActor->hasPositionData
                || isNewerPositionSequence(actor.positionSequence, currentActor->positionSequence);

            if (!hasNewerPosition)
            {
                if (currentActor->hasPositionData)
                    addPositionCorrection(*currentActor);
                continue;
            }

            if (!canAuthoritativelySimulateActors() || !serverCell.hasSimulationInterest())
            {
                BaseActor acceptedActor = actor;
                acceptedActor.hasPositionData = true;
                acceptNewestPositionActor(acceptedActors, acceptedActorIndexes, acceptedActor);
                const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
                mActorMovementStates[actorKey].lastMovementPacket = now;
                continue;
            }

            BaseActor simulatedActor = actor;
            simulatedActor.hasPositionData = true;

            const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
            PlayerMovementState& movementState = mActorMovementStates[actorKey];
            const bool hasMovementClock = movementState.lastMovementPacket != Clock::time_point();
            const bool needsInitialSeed = !currentActor->hasPositionData || !hasMovementClock;

            if (needsInitialSeed)
            {
                movementState.lastMovementPacket = now;
                acceptNewestPositionActor(acceptedActors, acceptedActorIndexes, simulatedActor);
                continue;
            }

            const float deltaSeconds = clampDeltaSeconds(
                std::chrono::duration<float>(now - movementState.lastMovementPacket).count());
            movementState.lastMovementPacket = now;

            simulatedActor.direction = actor.direction;
            simulatedActor.position = simulateMovementPosition(
                currentActor->position, actor.position, simulatedActor.direction, deltaSeconds);

            acceptNewestPositionActor(acceptedActors, acceptedActorIndexes, simulatedActor);

            if (squaredDistance(actor.position, simulatedActor.position) > correctionDistanceSquared)
                addPositionCorrection(simulatedActor);
        }

        if (!correctionActors.empty())
        {
            BaseActorList correctionList = actorList;
            correctionList.baseActors = correctionActors;
            correctionList.count = static_cast<unsigned int>(correctionList.baseActors.size());
            packet.setActorList(&correctionList);
            packet.SendWithReliability(actorList.guid, PacketReliability::ReliableOrdered);
        }

        actorList.baseActors = acceptedActors;
        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
        if (actorList.count == 0)
            return false;

        serverCell.readActorList(ID_ACTOR_POSITION, &actorList);
        serverCell.sendToLoaded(&packet, &actorList);
        return true;
    }

    void ServerSimulation::tickActors(float deltaSeconds)
    {
        if (!canAuthoritativelySimulateActors())
            return;

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return;

        ActorPacket* actorPacket = ServerNetworking::get().getActorPacketController()->GetPacket(ID_ACTOR_POSITION);

        for (Cell* cell : cellController->getCells())
        {
            if (cell == nullptr)
                continue;

            if (!cell->hasSimulationInterest())
                continue;

            BaseActorList* storedActorList = cell->getActorList();
            if (storedActorList == nullptr || storedActorList->baseActors.empty())
                continue;

            const std::string cellKey = getCellSimulationKey(cell->getCellData());
            BaseActorList tickActorList;
            tickActorList.cell = cell->getCellData();
            tickActorList.guid = unassignedPacketGuid();

            for (BaseActor& actor : storedActorList->baseActors)
            {
                if (!actor.hasPositionData || !isClientActorControlUpdateAllowed(&actor))
                    continue;

                const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
                ESM::Position direction = actor.direction;
                const bool serverOwnsActorMovement = hasServerOwnedActorMovement(actor);
                const bool hasAiMovementIntent = buildAiMovementIntent(*cell, actor, direction);
                bool hasWanderMovementIntent = false;

                if (!hasAiMovementIntent && actor.hasAiData && actor.aiAction == BaseActorList::WANDER)
                {
                    ActorWanderState& wanderState = mActorWanderStates[actorKey];
                    hasWanderMovementIntent = buildWanderMovementIntent(
                        cellKey, actor.refNum, actor.mpNum, actor, wanderState, deltaSeconds, direction);
                }
                else
                    mActorWanderStates.erase(actorKey);

                const bool hasServerMovementIntent = hasAiMovementIntent || hasWanderMovementIntent;
                if (!hasServerMovementIntent)
                {
                    if (serverOwnsActorMovement)
                        direction = zeroPosition();
                    else
                        sanitizeFinitePosition(direction);
                }

                if (!hasMovementIntent(direction))
                {
                    if ((hasServerMovementIntent || serverOwnsActorMovement) && hasMovementIntent(actor.direction))
                    {
                        actor.direction = direction;
                        ++actor.positionSequence;
                        actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);
                        actor.movementLatencySeconds = 0.f;
                        actor.hasPositionData = true;
                        tickActorList.baseActors.push_back(actor);
                    }
                    continue;
                }

                actor.position = simulateMovementPosition(actor.position, actor.position, direction, deltaSeconds);
                actor.direction = direction;
                ++actor.positionSequence;
                actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);
                actor.movementLatencySeconds = 0.f;
                actor.hasPositionData = true;

                tickActorList.baseActors.push_back(actor);
            }

            tickActorList.count = static_cast<unsigned int>(tickActorList.baseActors.size());
            if (tickActorList.count == 0)
                continue;

            actorPacket->setActorList(&tickActorList);
            cell->sendToLoaded(actorPacket, &tickActorList);
        }
    }

    bool ServerSimulation::acceptPlayerMovementSnapshot(Player& player, PlayerPacket& packet)
    {
        if (!player.hasFinitePositionPacket())
        {
            if (player.hasAcceptedPositionPacket)
            {
                const ESM::Position attemptedPosition = player.position;
                const std::string cellDescription = player.cell.getDescription();
                player.restoreAcceptedPositionPacket();
                packet.SendWithReliability(player.guid, PacketReliability::ReliableOrdered);
                sendPlayerMovementCorrectionEvent(player, "non_finite_position", true,
                    std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::quiet_NaN(), attemptedPosition, player.position,
                    cellDescription, cellDescription);
            }
            return false;
        }

        if (player.hasStalePositionPacket())
        {
            player.restoreAcceptedPositionPacket();
            return false;
        }

        const Clock::time_point now = Clock::now();
        const ESM::Position clientPosition = player.position;
        ESM::Position clientDirection = player.direction;

        PlayerMovementState& movementState = mPlayerMovementStates[player.guid];
        const bool hasMovementClock = movementState.lastMovementPacket != Clock::time_point();
        const bool needsInitialSeed = !player.hasAcceptedPositionPacket || !hasMovementClock;

        if (needsInitialSeed)
        {
            if (!player.acceptPositionPacket())
                return false;

            movementState.lastMovementPacket = now;
            mPlayerAcceptedCells[player.guid] = player.cell;
            sendPlayerVisualStateToLoaded(player, packet);
            return true;
        }

        const float serverDeltaSeconds = std::chrono::duration<float>(now - movementState.lastMovementPacket).count();
        const float observedSampleIntervalSeconds = getObservedPlayerSampleIntervalSeconds(player);
        const float plausibilityDeltaSeconds = getPlayerPlausibilityDeltaSeconds(
            serverDeltaSeconds, observedSampleIntervalSeconds, estimateOneWayLatencySeconds(player.guid));
        movementState.lastMovementPacket = now;

        if (!isPlausiblePlayerMovement(player.acceptedPosition, clientPosition, plausibilityDeltaSeconds))
        {
            const bool likelyCellSpaceTransition = isLikelyCellSpaceTransitionSnapshot(
                player.acceptedPosition, clientPosition);

            player.restoreAcceptedPositionPacket();

            if (likelyCellSpaceTransition)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Ignoring implausible cell-less movement snapshot for %s; awaiting reliable cell change",
                    player.npc.mName.c_str());
                const std::string cellDescription = player.cell.getDescription();
                sendPlayerMovementCorrectionEvent(player, "cell_space_transition", false,
                    serverDeltaSeconds, observedSampleIntervalSeconds, plausibilityDeltaSeconds,
                    clientPosition, player.position, cellDescription, cellDescription);
                return false;
            }

            packet.setPlayer(&player);
            packet.SendWithReliability(player.guid, PacketReliability::ReliableOrdered);
            const std::string cellDescription = player.cell.getDescription();
            sendPlayerMovementCorrectionEvent(player, "implausible_movement", true,
                serverDeltaSeconds, observedSampleIntervalSeconds, plausibilityDeltaSeconds,
                clientPosition, player.position, cellDescription, cellDescription);
            return false;
        }

        sanitizeFinitePosition(clientDirection);
        normalizeHorizontalIntent(clientDirection.pos[0], clientDirection.pos[1]);

        player.position = clientPosition;
        player.direction = clientDirection;

        if (!player.acceptPositionPacket())
            return false;

        mPlayerAcceptedCells[player.guid] = player.cell;

        packet.setPlayer(&player);
        sendPlayerVisualStateToLoaded(player, packet);
        return true;
    }

    bool ServerSimulation::acceptPlayerCellChange(Player& player, PlayerPacket& packet)
    {
        const auto previousCellIt = mPlayerAcceptedCells.find(player.guid);
        const bool hasPreviousAcceptedCell = previousCellIt != mPlayerAcceptedCells.end();
        ESM::Cell previousAcceptedCell;
        if (hasPreviousAcceptedCell)
            previousAcceptedCell = previousCellIt->second;

        if (!player.hasFinitePositionPacket())
        {
            if (player.hasAcceptedPositionPacket)
            {
                if (hasPreviousAcceptedCell)
                    sendAcceptedPlayerCellCorrection(player, packet, previousAcceptedCell);
                else
                    player.restoreAcceptedPositionPacket();
            }
            return false;
        }

        if (hasPreviousAcceptedCell && !isCellChangePlausibleFromAcceptedState(player, previousAcceptedCell))
        {
            const LuaObservationCellCheck luaObservationCheck = checkFreshLuaObservationCell(player, player.cell);
            if (luaObservationConfirmsCell(luaObservationCheck))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Accepting cell change for %s to %s using fresh OpenMW Lua observation: observed=%s",
                    player.npc.mName.c_str(), player.cell.getDescription().c_str(),
                    describeLuaObservationCell(*luaObservationCheck.observation).c_str());
            }
            else
            {
                logLuaObservationCellCheckFailure(player, player.cell, luaObservationCheck);
                sendAcceptedPlayerCellCorrection(player, packet, previousAcceptedCell);
                return false;
            }
        }

        if (!acceptServerAuthoredPlayerState(player, true))
            return false;

        if (hasPreviousAcceptedCell)
            moveFollowingActorsAcrossPlayerCellChange(player, previousAcceptedCell);

        return true;
    }
}
