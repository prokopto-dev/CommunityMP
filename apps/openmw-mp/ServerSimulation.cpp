#include "ServerSimulation.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <components/openmw-mp/Base/ActorStatsAuthority.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
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
#include "ServerContentDatabase.hpp"
#include "ServerContentRegistry.hpp"
#include "ServerEventDispatcher.hpp"
#include "ServerNetworking.hpp"
#include "WorldDatabaseStore.hpp"
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
    constexpr float serverActorMeleeRange = 192.f;
    constexpr float serverActorMeleeRangeSquared = serverActorMeleeRange * serverActorMeleeRange;
    constexpr float serverActorMeleeMinimumDamage = 4.f;
    constexpr float serverActorMeleeMaximumFallbackDamage = 24.f;
    constexpr auto serverActorMeleeInterval = std::chrono::milliseconds(1500);
    constexpr auto runtimePlayerFatalSnapshotCellChangeGrace = std::chrono::seconds(3);
    constexpr auto luaObservationFreshnessWindow = std::chrono::seconds(5);
    constexpr float aiCoordinateStopDistance = 64.f;
    constexpr float aiTargetStopDistance = 128.f;
    constexpr float aiMinimumStopDistance = 48.f;
    constexpr float aiMaximumStopDistance = 2048.f;
    constexpr float aiWanderStopDistance = 32.f;
    constexpr float aiWanderMinimumDecisionSeconds = 2.f;
    constexpr float aiWanderMaximumDecisionSeconds = 8.f;
    constexpr float aiRouteDestinationRefreshDistance = 64.f;
    constexpr float aiRouteWaypointReachedDistance = 32.f;
    constexpr float twoPi = 6.28318530717958647692f;
    constexpr float followerCellChangeBehindDistance = 96.f;
    constexpr float followerCellChangeRowSpacing = 48.f;
    constexpr float followerCellChangeColumnSpacing = 48.f;
    constexpr float runtimeActorMovementEpsilonSquared = 4.f * 4.f;
    constexpr float runtimeActorDerivedDirectionMaximumDistanceSquared = 512.f * 512.f;
    constexpr float runtimeActorRotationEpsilonRadians = 0.001f;
    constexpr float runtimeActorDirectionEpsilonSquared = 0.0001f;
    constexpr std::uint32_t runtimeActorFallbackSnapshotThreshold = 15;
    constexpr std::size_t runtimeStatusContentPreviewLimit = 32;
    constexpr auto runtimeActorMovementHealthLogInterval = std::chrono::seconds(10);
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

    using ActorIdentityPair = std::pair<unsigned int, unsigned int>;

    ActorIdentityPair actorIdentityPair(const mwmp::BaseActor& actor)
    {
        return { actor.refNum, actor.mpNum };
    }

    bool sameEquipmentItem(const mwmp::Item& left, const mwmp::Item& right)
    {
        return left.refId == right.refId
            && left.count == right.count
            && left.charge == right.charge
            && left.enchantmentCharge == right.enchantmentCharge
            && left.soul == right.soul;
    }

    bool sameActorEquipment(const mwmp::BaseActor& left, const mwmp::BaseActor& right)
    {
        for (int slot = 0; slot < mwmp::equipmentSlotCount; ++slot)
        {
            if (!sameEquipmentItem(left.equipmentItems[slot], right.equipmentItems[slot]))
                return false;
        }

        return true;
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
        const mwmp::SimulationRuntimeWorldState& worldState = runtime.worldState();

        if (runtime.canOwnActorAuthority())
            return {};

        if (!topology.hasHeadlessOpenMwEngine)
            return "headless-openmw-engine-missing";
        if (!worldState.prepared)
            return "openmw-world-not-prepared";
        if (!worldState.persistent)
            return "openmw-world-save-not-bound";
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

    std::string runtimeWholeGameBlockReason(const mwmp::SimulationRuntime& runtime)
    {
        const std::string actorAuthorityBlockReason = runtimeAuthorityBlockReason(runtime);
        if (!actorAuthorityBlockReason.empty())
            return actorAuthorityBlockReason;

        const mwmp::SimulationRuntimeTopology& topology = runtime.topology();
        if (!topology.hasPersistentPlayerActors)
            return "persistent-server-player-actors-missing";
        if (topology.usesSinglePlayerProxy)
            return "single-server-player-proxy-active";
        if (!topology.rendererClientProtocol)
            return "renderer-client-protocol-missing";

        return {};
    }

    const char* cellSimulationAuthorityMode(bool serverActorAuthority)
    {
        return serverActorAuthority ? "server-simulation" : "client-snapshot-reporter";
    }

    const char* cellSimulationOwner(bool serverActorAuthority)
    {
        return serverActorAuthority ? "server" : "client-snapshot-fallback";
    }

    const char* localClientCellRole(bool serverActorAuthority, bool isSnapshotReporter)
    {
        if (serverActorAuthority)
            return "renderer";

        return isSnapshotReporter ? "snapshot-reporter" : "observer";
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

    std::string getCellSimulationKey(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return "exterior:" + std::to_string(cell.mData.mX) + "," + std::to_string(cell.mData.mY);

        return "interior:" + cell.mName;
    }

    std::string_view trimAsciiWhitespace(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);
        return value;
    }

    bool parseInt(std::string_view value, int& result)
    {
        value = trimAsciiWhitespace(value);
        if (value.empty())
            return false;

        const char* begin = value.data();
        const char* end = begin + value.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, result);
        return parsed.ec == std::errc() && parsed.ptr == end;
    }

    bool parseExteriorCellDescription(std::string_view description, int& x, int& y)
    {
        description = trimAsciiWhitespace(description);
        if (description.empty() || description.back() != ')')
            return false;

        const std::size_t open = description.rfind('(');
        if (open == std::string_view::npos || open + 1 >= description.size())
            return false;

        const std::string_view coordinates = description.substr(open + 1, description.size() - open - 2);
        const std::size_t comma = coordinates.find(',');
        if (comma == std::string_view::npos)
            return false;

        return parseInt(coordinates.substr(0, comma), x) && parseInt(coordinates.substr(comma + 1), y);
    }

    std::string getCellDescriptionSimulationKey(std::string_view cellDescription)
    {
        if (cellDescription.starts_with("exterior:") || cellDescription.starts_with("interior:"))
            return std::string(cellDescription);

        int x = 0;
        int y = 0;
        if (parseExteriorCellDescription(cellDescription, x, y))
            return "exterior:" + std::to_string(x) + "," + std::to_string(y);

        return "interior:" + std::string(cellDescription);
    }

    Cell* findLoadedServerCellByDescription(const std::string& cellDescription)
    {
        if (cellDescription.empty())
            return nullptr;

        const std::string cellKey = getCellDescriptionSimulationKey(cellDescription);
        for (Cell* cell : CellController::get()->getCells())
        {
            if (cell != nullptr && (cell->getShortDescription() == cellDescription
                    || getCellSimulationKey(cell->getCellData()) == cellKey))
                return cell;
        }

        return nullptr;
    }

    bool cellDescriptionMatches(const ESM::Cell& cell, const std::string& cellDescription)
    {
        return !cellDescription.empty() && (cell.getDescription() == cellDescription
            || getCellSimulationKey(cell) == getCellDescriptionSimulationKey(cellDescription));
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

    float squaredDirectionDelta(const ESM::Position& left, const ESM::Position& right)
    {
        float result = 0.f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float positionDelta = left.pos[axis] - right.pos[axis];
            const float rotationDelta = left.rot[axis] - right.rot[axis];
            result += positionDelta * positionDelta + rotationDelta * rotationDelta;
        }

        return result;
    }

    float normalizedAngleDelta(float left, float right)
    {
        if (!std::isfinite(left) || !std::isfinite(right))
            return std::numeric_limits<float>::infinity();

        float delta = std::fmod(left - right, twoPi);
        if (delta > twoPi * 0.5f)
            delta -= twoPi;
        else if (delta < -twoPi * 0.5f)
            delta += twoPi;

        return delta;
    }

    bool hasMeaningfulRotationChange(const ESM::Position& left, const ESM::Position& right)
    {
        float rotationDeltaSquared = 0.f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float delta = normalizedAngleDelta(left.rot[axis], right.rot[axis]);
            if (!std::isfinite(delta))
                return true;

            rotationDeltaSquared += delta * delta;
        }

        constexpr float rotationEpsilonSquared
            = runtimeActorRotationEpsilonRadians * runtimeActorRotationEpsilonRadians;
        return rotationDeltaSquared > rotationEpsilonSquared;
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

    float sanitizeMovementComponent(float value)
    {
        constexpr float movementEpsilon = 0.0001f;
        if (!std::isfinite(value) || std::abs(value) <= movementEpsilon)
            return 0.f;

        return value;
    }

    void clearRuntimeMovementIntent(ESM::Position& direction)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            direction.pos[axis] = 0.f;
            direction.rot[axis] = 0.f;
        }
    }

    float deriveMovementComponentFromDelta(float delta)
    {
        if (!std::isfinite(delta) || std::abs(delta) <= runtimeActorRotationEpsilonRadians)
            return 0.f;

        return delta > 0.f ? 1.f : -1.f;
    }

    enum class RuntimeMovementDirectionAdjustment
    {
        Unchanged,
        Cleared,
        Derived
    };

    RuntimeMovementDirectionAdjustment applyActualRuntimeMovementDirection(
        mwmp::BaseActor& actor, const mwmp::BaseActor* previousActor)
    {
        sanitizeFinitePosition(actor.direction);
        if (!actor.hasPositionData || previousActor == nullptr || !previousActor->hasPositionData)
        {
            clearRuntimeMovementIntent(actor.direction);
            return RuntimeMovementDirectionAdjustment::Cleared;
        }

        const float deltaX = actor.position.pos[0] - previousActor->position.pos[0];
        const float deltaY = actor.position.pos[1] - previousActor->position.pos[1];
        const float deltaZ = actor.position.pos[2] - previousActor->position.pos[2];
        const float horizontalDistanceSquared = squaredHorizontalLength(deltaX, deltaY);
        bool hasDerivedIntent = false;
        if (!std::isfinite(horizontalDistanceSquared) || horizontalDistanceSquared <= runtimeActorMovementEpsilonSquared
            || horizontalDistanceSquared > runtimeActorDerivedDirectionMaximumDistanceSquared)
        {
            actor.direction.pos[0] = 0.f;
            actor.direction.pos[1] = 0.f;
        }
        else
        {
            const float yaw = actor.position.rot[2];
            if (!std::isfinite(yaw))
            {
                actor.direction.pos[0] = 0.f;
                actor.direction.pos[1] = 0.f;
            }
            else
            {
                const float sinYaw = std::sin(yaw);
                const float cosYaw = std::cos(yaw);
                const float localSide = deltaX * cosYaw - deltaY * sinYaw;
                const float localForward = deltaX * sinYaw + deltaY * cosYaw;
                const float localDistance = std::sqrt(squaredHorizontalLength(localSide, localForward));
                if (!std::isfinite(localDistance) || localDistance <= 0.f)
                {
                    actor.direction.pos[0] = 0.f;
                    actor.direction.pos[1] = 0.f;
                }
                else
                {
                    actor.direction.pos[0] = sanitizeMovementComponent(localSide / localDistance);
                    actor.direction.pos[1] = sanitizeMovementComponent(localForward / localDistance);
                    hasDerivedIntent = actor.direction.pos[0] != 0.f || actor.direction.pos[1] != 0.f;
                }
            }
        }

        constexpr float runtimeActorVerticalMovementEpsilon = 0.5f;
        if (!std::isfinite(deltaZ) || std::abs(deltaZ) <= runtimeActorVerticalMovementEpsilon)
            actor.direction.pos[2] = 0.f;
        else
        {
            actor.direction.pos[2] = sanitizeMovementComponent(actor.direction.pos[2]);
            if (actor.direction.pos[2] == 0.f)
                actor.direction.pos[2] = deltaZ > 0.f ? 1.f : -1.f;
            hasDerivedIntent = true;
        }

        for (int axis = 0; axis < 3; ++axis)
        {
            const float rotationDelta = normalizedAngleDelta(actor.position.rot[axis], previousActor->position.rot[axis]);
            if (!std::isfinite(rotationDelta) || std::abs(rotationDelta) <= runtimeActorRotationEpsilonRadians)
                actor.direction.rot[axis] = 0.f;
            else
            {
                actor.direction.rot[axis] = sanitizeMovementComponent(actor.direction.rot[axis]);
                if (actor.direction.rot[axis] == 0.f)
                    actor.direction.rot[axis] = deriveMovementComponentFromDelta(rotationDelta);
                hasDerivedIntent = true;
            }
        }

        return hasDerivedIntent ? RuntimeMovementDirectionAdjustment::Derived
                                : RuntimeMovementDirectionAdjustment::Cleared;
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

    struct RuntimeActorCellSource
    {
        Cell* cell = nullptr;
        mwmp::BaseActor actor;
    };

    std::optional<RuntimeActorCellSource> findRuntimeActorSourceCell(
        CellController& cellController, const ESM::Cell& destinationCell, const mwmp::BaseActor& runtimeActor)
    {
        const std::string destinationKey = getCellSimulationKey(destinationCell);

        for (Cell* candidateCell : cellController.getCells())
        {
            if (candidateCell == nullptr)
                continue;

            if (getCellSimulationKey(candidateCell->getCellData()) == destinationKey)
                continue;

            if (mwmp::BaseActor* cachedActor = candidateCell->getActor(runtimeActor.refNum, runtimeActor.mpNum))
                return RuntimeActorCellSource{ candidateCell, *cachedActor };
        }

        return std::nullopt;
    }

    std::string normalizedWorldLookupKey(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char c : value)
        {
            if (c == '\\')
                result.push_back('/');
            else
                result.push_back(static_cast<char>(std::tolower(c)));
        }
        return result;
    }

    std::string getWorldDatabaseCellKey(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return "exterior:" + std::to_string(cell.mData.mX) + "," + std::to_string(cell.mData.mY);

        return "interior:" + normalizedWorldLookupKey(cell.mName);
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

    ESM::Position makeDoorDestinationPosition(const mwmp::WorldCellReferenceRecord& reference)
    {
        ESM::Position position;
        position.pos[0] = reference.doorDestPosX;
        position.pos[1] = reference.doorDestPosY;
        position.pos[2] = reference.doorDestPosZ;
        position.rot[0] = reference.doorDestRotX;
        position.rot[1] = reference.doorDestRotY;
        position.rot[2] = reference.doorDestRotZ;
        return position;
    }

    bool worldDoorDestinationMatchesCell(const mwmp::WorldCellReferenceRecord& reference, const ESM::Cell& destinationCell)
    {
        if (!reference.teleport)
            return false;

        if (destinationCell.isExterior())
            return isExteriorCellConsistentWithPosition(destinationCell, makeDoorDestinationPosition(reference));

        return !reference.destCell.empty()
            && normalizedWorldLookupKey(reference.destCell) == normalizedWorldLookupKey(destinationCell.mName);
    }

    bool worldDoorDestinationMatchesPosition(
        const mwmp::WorldCellReferenceRecord& reference, const ESM::Position& attemptedPosition)
    {
        constexpr float portalDestinationTolerance = 512.f;
        const ESM::Position destination = makeDoorDestinationPosition(reference);
        return squaredDistance(destination, attemptedPosition)
            <= portalDestinationTolerance * portalDestinationTolerance;
    }

    bool isImportedDoorCellChange(const ESM::Cell& sourceCell, const ESM::Cell& destinationCell,
        const ESM::Position& attemptedPosition)
    {
        mwmp::WorldDatabaseStore::get().ensureLoaded();
        const mwmp::WorldDatabaseStatistics stats = mwmp::WorldDatabaseStore::get().statistics();
        if (!stats.loaded)
            return false;

        const std::vector<mwmp::WorldCellReferenceRecord> references
            = mwmp::WorldDatabaseStore::get().findReferencesByCellKey(getWorldDatabaseCellKey(sourceCell));
        for (const mwmp::WorldCellReferenceRecord& reference : references)
        {
            if (reference.deleted || reference.baseRecordDeleted || reference.baseRecordCategory != "door"
                || !reference.teleport)
                continue;

            if (!worldDoorDestinationMatchesCell(reference, destinationCell))
                continue;

            if (worldDoorDestinationMatchesPosition(reference, attemptedPosition))
                return true;
        }

        return false;
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

        if (isImportedDoorCellChange(acceptedCell, player.cell, player.position))
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

    bool hasMeaningfulMovementIntent(const ESM::Position& direction)
    {
        const ESM::Position zero = {};
        const float deltaSquared = squaredDirectionDelta(direction, zero);
        return std::isfinite(deltaSquared) && deltaSquared > runtimeActorDirectionEpsilonSquared;
    }

    bool hasMeaningfulRuntimeTransformChange(const mwmp::BaseActor& actor, const mwmp::BaseActor& previousActor)
    {
        if (!actor.hasPositionData || !previousActor.hasPositionData)
            return true;

        const float distanceSquared = squaredDistance(actor.position, previousActor.position);
        if (!std::isfinite(distanceSquared) || distanceSquared > runtimeActorMovementEpsilonSquared)
            return true;

        if (hasMeaningfulRotationChange(actor.position, previousActor.position))
            return true;

        const float directionDeltaSquared = squaredDirectionDelta(actor.direction, previousActor.direction);
        return !std::isfinite(directionDeltaSquared) || directionDeltaSquared > runtimeActorDirectionEpsilonSquared;
    }

    bool hasMeaningfulRuntimeTransformDelta(const mwmp::BaseActor& actor, const mwmp::BaseActor& previousActor)
    {
        if (!actor.hasPositionData || !previousActor.hasPositionData)
            return false;

        const float distanceSquared = squaredDistance(actor.position, previousActor.position);
        if (std::isfinite(distanceSquared) && distanceSquared > runtimeActorMovementEpsilonSquared)
            return true;

        return hasMeaningfulRotationChange(actor.position, previousActor.position);
    }

    bool hasRuntimeAnimFlagsChange(const mwmp::BaseActor& actor, const mwmp::BaseActor& previousActor)
    {
        return actor.movementFlags != previousActor.movementFlags
            || actor.drawState != previousActor.drawState
            || actor.isJumping != previousActor.isJumping
            || actor.isFlying != previousActor.isFlying;
    }

    bool shouldSendRuntimePositionSnapshot(const mwmp::BaseActor* previousActor, const mwmp::BaseActor& actor)
    {
        if (!actor.hasPositionData)
            return false;

        if (previousActor == nullptr || !previousActor->hasPositionData)
            return true;

        if (hasMeaningfulMovementIntent(actor.direction))
            return true;

        return hasMeaningfulRuntimeTransformChange(actor, *previousActor);
    }

    bool shouldSendRuntimeAnimFlagsSnapshot(const mwmp::BaseActor* previousActor, const mwmp::BaseActor& actor)
    {
        if (!actor.hasAnimFlagsData)
            return false;

        if (previousActor == nullptr || !previousActor->hasAnimFlagsData)
            return true;

        return hasRuntimeAnimFlagsChange(actor, *previousActor);
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

    bool hasFiniteSimpleCreatureStats(const mwmp::SimpleCreatureStats& stats)
    {
        return mwmp::isFiniteDynamicStat(stats.mDynamic[0])
            && mwmp::isFiniteDynamicStat(stats.mDynamic[1])
            && mwmp::isFiniteDynamicStat(stats.mDynamic[2]);
    }

    mwmp::SimpleCreatureStats makeSimpleCreatureStats(const ESM::CreatureStats& stats)
    {
        mwmp::SimpleCreatureStats result;
        for (int i = 0; i < 3; ++i)
            result.mDynamic[i] = stats.mDynamic[i];
        result.mDead = stats.mDead;
        result.mDeathAnimationFinished = stats.mDeathAnimationFinished;
        return result;
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

    bool isLiveActorForServerMelee(const mwmp::BaseActor& actor)
    {
        if (actor.creatureStats.mDead)
            return false;

        if (actor.hasStatsDynamicData && mwmp::hasFiniteActorDynamicStats(actor)
            && actor.creatureStats.mDynamic[0].mCurrent <= healthDeadEpsilon)
            return false;

        return true;
    }

    float getActorFatigueDamageScale(const mwmp::BaseActor& actor)
    {
        if (!actor.hasStatsDynamicData || !mwmp::hasFiniteActorDynamicStats(actor))
            return 1.f;

        const ESM::StatState<float>& fatigue = actor.creatureStats.mDynamic[2];
        if (!std::isfinite(fatigue.mBase) || fatigue.mBase <= healthDeadEpsilon
            || !std::isfinite(fatigue.mCurrent))
            return 1.f;

        const float fatigueRatio = std::clamp(fatigue.mCurrent / fatigue.mBase, 0.f, 1.f);
        return 0.65f + 0.35f * fatigueRatio;
    }

    float getServerActorMeleeDamage(const mwmp::BaseActor& actor)
    {
        if (!actor.refId.empty())
        {
            mwmp::WorldDatabaseStore::get().ensureLoaded();
            if (const std::optional<mwmp::WorldActorProfileRecord> profile
                = mwmp::WorldDatabaseStore::get().findActorProfileByRecordKey(actor.refId))
            {
                int bestAttack = 0;
                for (const int attack : profile->attacks)
                    bestAttack = std::max(bestAttack, attack);

                if (bestAttack > 0)
                    return std::clamp(static_cast<float>(bestAttack) * getActorFatigueDamageScale(actor),
                        serverActorMeleeMinimumDamage, maxServerAttackDamage);

                if (profile->npc)
                {
                    const float combatRating = profile->combat > 0 ? static_cast<float>(profile->combat) : 30.f;
                    const float level = std::max(1.f, static_cast<float>(profile->level));
                    const float npcDamage = 3.f + level * 0.5f + combatRating * 0.12f;
                    return std::clamp(npcDamage * getActorFatigueDamageScale(actor),
                        serverActorMeleeMinimumDamage, serverActorMeleeMaximumFallbackDamage);
                }
            }
        }

        float fallbackDamage = 8.f;
        if (actor.hasStatsDynamicData && mwmp::hasFiniteActorDynamicStats(actor))
        {
            const ESM::StatState<float>& health = actor.creatureStats.mDynamic[0];
            const float baseHealth = std::max(health.mBase, health.mCurrent);
            if (std::isfinite(baseHealth) && baseHealth > healthDeadEpsilon)
                fallbackDamage = baseHealth * 0.08f;
        }

        return std::clamp(fallbackDamage * getActorFatigueDamageScale(actor),
            serverActorMeleeMinimumDamage, serverActorMeleeMaximumFallbackDamage);
    }

    mwmp::Attack makeServerActorMeleeAttack(const mwmp::BaseActor& actor, const mwmp::Target& target,
        const ESM::Position& hitPosition, const std::string& runtimeAttackAnimation)
    {
        mwmp::Attack attack;
        attack.target = target;
        attack.type = mwmp::Attack::MELEE;
        attack.attackAnimation = runtimeAttackAnimation.empty() ? "chop" : runtimeAttackAnimation;
        attack.hitPosition = hitPosition;
        attack.damage = getServerActorMeleeDamage(actor);
        attack.attackStrength = 1.f;
        attack.isHit = true;
        attack.success = true;
        attack.block = false;
        attack.pressed = false;
        attack.instant = true;
        return attack;
    }

    float getAiStopDistance(const mwmp::BaseActor& actor, bool coordinatePackage)
    {
        if (coordinatePackage)
            return aiCoordinateStopDistance;

        if (actor.aiDistance == 0)
            return aiTargetStopDistance;

        return std::clamp(static_cast<float>(actor.aiDistance), aiMinimumStopDistance, aiMaximumStopDistance);
    }

    void clearPathgridRouteState(mwmp::ActorPathgridRouteState& routeState)
    {
        routeState.waypoints.clear();
        routeState.nextWaypointIndex = 0;
        routeState.hasDestination = false;
        routeState.routeBlocked = false;
    }

    bool isEmptyPathgridRouteState(const mwmp::ActorPathgridRouteState& routeState)
    {
        return !routeState.hasDestination && !routeState.routeBlocked && routeState.waypoints.empty();
    }

    bool shouldRebuildPathgridRoute(const mwmp::ActorPathgridRouteState& routeState,
        const ESM::Position& destination)
    {
        if (!routeState.hasDestination)
            return true;

        const float deltaX = routeState.destination.pos[0] - destination.pos[0];
        const float deltaY = routeState.destination.pos[1] - destination.pos[1];
        const float distanceSquared = squaredHorizontalLength(deltaX, deltaY);
        return !std::isfinite(distanceSquared)
            || distanceSquared > aiRouteDestinationRefreshDistance * aiRouteDestinationRefreshDistance;
    }

    bool choosePathgridSteeringDestination(Cell& cell, mwmp::ActorPathgridRouteState& routeState,
        const ESM::Position& start, const ESM::Position& destination, float stopDistance,
        ESM::Position& steeringDestination, bool& routeBlocked)
    {
        routeBlocked = false;
        const mwmp::ServerPathgridNavigator& navigator = cell.getServerWorldPathgridNavigator();
        if (!navigator.hasPathgrid())
        {
            clearPathgridRouteState(routeState);
            return false;
        }

        if (!hasFiniteWorldPosition(start) || !hasFiniteWorldPosition(destination))
        {
            clearPathgridRouteState(routeState);
            return false;
        }

        if (shouldRebuildPathgridRoute(routeState, destination))
        {
            const mwmp::ServerPathgridRoute route = navigator.buildRoute(start, destination);
            routeState.destination = destination;
            sanitizeFinitePosition(routeState.destination);
            routeState.hasDestination = true;
            routeState.routeBlocked = !route.reachable;
            routeState.nextWaypointIndex = 0;
            routeState.waypoints.clear();

            if (route.reachable)
            {
                routeState.waypoints.reserve(route.waypoints.size());
                for (const mwmp::ServerPathgridWaypoint& waypoint : route.waypoints)
                {
                    ESM::Position position = waypoint.position;
                    sanitizeFinitePosition(position);
                    routeState.waypoints.push_back(position);
                }
            }
        }

        if (routeState.routeBlocked)
        {
            routeBlocked = true;
            return true;
        }

        const float waypointThreshold = std::min(stopDistance, aiRouteWaypointReachedDistance);
        const float waypointThresholdSquared = waypointThreshold * waypointThreshold;
        while (routeState.nextWaypointIndex < routeState.waypoints.size())
        {
            const ESM::Position& waypoint = routeState.waypoints[routeState.nextWaypointIndex];
            const float deltaX = waypoint.pos[0] - start.pos[0];
            const float deltaY = waypoint.pos[1] - start.pos[1];
            const float distanceSquared = squaredHorizontalLength(deltaX, deltaY);
            if (!std::isfinite(distanceSquared) || distanceSquared <= waypointThresholdSquared)
                ++routeState.nextWaypointIndex;
            else
                break;
        }

        if (routeState.nextWaypointIndex < routeState.waypoints.size())
        {
            steeringDestination = routeState.waypoints[routeState.nextWaypointIndex];
            return true;
        }

        steeringDestination = destination;
        return true;
    }

    bool buildAiMovementIntent(Cell& cell, mwmp::BaseActor& actor, mwmp::ActorPathgridRouteState& routeState,
        ESM::Position& direction)
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

        ESM::Position steeringDestination = destination;
        bool routeBlocked = false;
        const bool routeAvailable = choosePathgridSteeringDestination(
            cell, routeState, actor.position, destination, stopDistance, steeringDestination, routeBlocked);
        if (routeAvailable && routeBlocked)
            return true;

        const float steeringDeltaX = steeringDestination.pos[0] - actor.position.pos[0];
        const float steeringDeltaY = steeringDestination.pos[1] - actor.position.pos[1];
        const float steeringDistanceSquared = squaredHorizontalLength(steeringDeltaX, steeringDeltaY);
        if (!std::isfinite(steeringDistanceSquared) || steeringDistanceSquared <= 0.f)
            return true;

        actor.position.rot[2] = std::atan2(steeringDeltaX, steeringDeltaY);
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

    bool buildWanderMovementIntent(Cell& cell, const std::string& cellKey, unsigned int refNum, unsigned int mpNum,
        mwmp::BaseActor& actor, mwmp::ActorWanderState& wanderState, mwmp::ActorPathgridRouteState& routeState,
        float deltaSeconds, ESM::Position& direction)
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

        ESM::Position steeringDestination = wanderState.destination;
        bool routeBlocked = false;
        const bool routeAvailable = choosePathgridSteeringDestination(
            cell, routeState, actor.position, wanderState.destination, aiWanderStopDistance, steeringDestination,
            routeBlocked);
        if (routeAvailable && routeBlocked)
        {
            wanderState.hasDestination = false;
            return true;
        }

        const float steeringDeltaX = steeringDestination.pos[0] - actor.position.pos[0];
        const float steeringDeltaY = steeringDestination.pos[1] - actor.position.pos[1];
        const float steeringDistanceSquared = squaredHorizontalLength(steeringDeltaX, steeringDeltaY);
        if (!std::isfinite(steeringDistanceSquared) || steeringDistanceSquared <= 0.f)
            return true;

        actor.position.rot[2] = std::atan2(steeringDeltaX, steeringDeltaY);
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

    bool sameActorAiTarget(const mwmp::Target& left, const mwmp::Target& right)
    {
        return targetsReferToSameEntity(left, right);
    }

    bool sameActorAiCoordinates(const ESM::Position& left, const ESM::Position& right)
    {
        constexpr float coordinateEpsilon = 0.01f;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(left.pos[axis] - right.pos[axis]) > coordinateEpsilon)
                return false;
        }

        return true;
    }

    bool sameActorAi(const mwmp::BaseActor& left, const mwmp::BaseActor& right)
    {
        if (left.hasAiData != right.hasAiData)
            return false;

        if (!left.hasAiData)
            return true;

        if (left.aiAction != right.aiAction || left.aiDistance != right.aiDistance
            || left.aiDuration != right.aiDuration || left.aiShouldRepeat != right.aiShouldRepeat
            || left.hasAiTarget != right.hasAiTarget)
            return false;

        if (left.hasAiTarget && !sameActorAiTarget(left.aiTarget, right.aiTarget))
            return false;

        return sameActorAiCoordinates(left.aiCoordinates, right.aiCoordinates);
    }

    bool sameRuntimeAttackPresentation(const mwmp::BaseActor& left, const mwmp::BaseActor& right)
    {
        if (left.hasCombatData != right.hasCombatData)
            return false;

        if (!left.hasCombatData)
            return true;

        return left.attack.pressed == right.attack.pressed
            && left.attack.type == right.attack.type
            && left.attack.attackAnimation == right.attack.attackAnimation
            && sameActorAiTarget(left.attack.target, right.attack.target);
    }

    bool shouldSendRuntimeAttackSnapshot(const mwmp::BaseActor* previousActor, const mwmp::BaseActor& actor)
    {
        if (!actor.hasCombatData || actor.attack.type != mwmp::Attack::MELEE)
            return false;

        if (previousActor == nullptr || !previousActor->hasCombatData)
            return actor.attack.pressed;

        return !sameRuntimeAttackPresentation(*previousActor, actor);
    }

    mwmp::BaseActor buildServerAcceptedAiActor(Cell& cell, const mwmp::BaseActor& incomingActor,
        const mwmp::BaseActor& currentActor)
    {
        mwmp::BaseActor acceptedActor = incomingActor;
        acceptedActor.refId = currentActor.refId.empty() ? incomingActor.refId : currentActor.refId;
        acceptedActor.cell = cell.getCellData();
        acceptedActor.hasAiData = true;
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

    bool ensureActorDynamicStatsForDamage(mwmp::BaseActor& target)
    {
        if (target.hasStatsDynamicData && mwmp::hasFiniteActorDynamicStats(target))
            return true;

        if (target.mpNum != 0)
            return false;

        mwmp::WorldDatabaseStore::get().ensureLoaded();
        const std::vector<mwmp::WorldCellReferenceRecord> references
            = mwmp::WorldDatabaseStore::get().findReferencesByCellKey(getWorldDatabaseCellKey(target.cell));
        for (const mwmp::WorldCellReferenceRecord& reference : references)
        {
            if (reference.deleted || reference.tombstone || reference.count == 0)
                continue;

            if (reference.refNumIndex != target.refNum)
                continue;

            if (!target.refId.empty()
                && normalizedWorldLookupKey(reference.refId) != normalizedWorldLookupKey(target.refId))
                continue;

            if (!reference.baseActorStatsDynamicImported)
                return false;

            bool seenStats[3] = { false, false, false };
            ESM::StatState<float> seededStats[3];
            for (const mwmp::WorldActorStatsDynamicItem& item :
                mwmp::WorldDatabaseStore::get().findActorStatsDynamicByRecordKey(reference.baseRecordKey))
            {
                if (item.statIndex < 0 || item.statIndex > 2)
                    continue;

                ESM::StatState<float>& stat = seededStats[item.statIndex];
                stat.mBase = item.base;
                stat.mMod = item.mod;
                stat.mCurrent = item.current;
                stat.mDamage = item.damage;
                stat.mProgress = item.progress;
                seenStats[item.statIndex] = mwmp::isFiniteDynamicStat(stat);
            }

            if (!seenStats[0] || !seenStats[1] || !seenStats[2])
                return false;

            for (int statIndex = 0; statIndex < 3; ++statIndex)
                target.creatureStats.mDynamic[statIndex] = seededStats[statIndex];

            target.hasStatsDynamicData = true;
            if (target.statsDynamicSequence == 0)
                target.statsDynamicSequence = 1;

            target.creatureStats.mDead = target.creatureStats.mDynamic[0].mCurrent <= healthDeadEpsilon;
            return true;
        }

        return false;
    }

    bool applyHealthDamageToActor(mwmp::BaseActor& target, float damage)
    {
        if (!ensureActorDynamicStatsForDamage(target))
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
        if (!ensureActorDynamicStatsForDamage(target))
            return false;

        float& fatigue = target.creatureStats.mDynamic[2].mCurrent;
        if (!std::isfinite(fatigue))
            return false;

        fatigue = std::max(0.f, fatigue - damage);
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

    void broadcastActorAi(Cell& cell, const mwmp::BaseActor& target)
    {
        mwmp::BaseActorList aiList;
        aiList.cell = cell.getCellData();
        aiList.guid = mwmp::unassignedPacketGuid();
        aiList.action = mwmp::BaseActorList::SET;
        aiList.isValid = true;
        aiList.baseActors.push_back(target);
        aiList.count = static_cast<unsigned int>(aiList.baseActors.size());

        mwmp::ActorPacket* aiPacket = mwmp::ServerNetworking::get().getActorPacketController()->GetPacket(
            ID_ACTOR_AI);
        if (aiPacket == nullptr)
            return;

        aiPacket->setActorList(&aiList);
        cell.sendToLoaded(aiPacket, &aiList);
    }

    mwmp::Target makePlayerAiTarget(const Player& player)
    {
        mwmp::Target target;
        target.isPlayer = true;
        target.guid = player.guid;
        target.name = player.npc.mName;
        return target;
    }

    bool applyCombatTargetToActor(Cell& cell, mwmp::BaseActor& targetActor, const Player& attacker)
    {
        if (!isLivePlayerAiTarget(attacker) || !attacker.hasFinitePositionPacket())
            return false;

        if (getCellSimulationKey(attacker.cell) != getCellSimulationKey(cell.getCellData()))
            return false;

        const mwmp::Target aiTarget = makePlayerAiTarget(attacker);
        const bool alreadyTargetingPlayer = targetActor.hasAiData && targetActor.hasAiTarget
            && targetActor.aiAction == mwmp::BaseActorList::COMBAT
            && targetsReferToSameEntity(targetActor.aiTarget, aiTarget);

        targetActor.hasAiData = true;
        targetActor.aiAction = mwmp::BaseActorList::COMBAT;
        targetActor.aiDistance = 0;
        targetActor.aiDuration = 0;
        targetActor.aiShouldRepeat = true;
        targetActor.aiTarget = aiTarget;
        targetActor.hasAiTarget = true;
        return !alreadyTargetingPlayer;
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

        const SimulationRuntimeWorldState& worldState = mRuntime->worldState();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Server simulation runtime requested=%s active=%s openmwLinked=%s headlessEngine=%s openmwWorld=%s "
            "persistentWorld=%s loadedFromSave=%s initializedNewWorld=%s actorAuthority=%s blockedBy=%s savePath=%s",
            mRuntime->requestedName(), mRuntime->activeName(), mRuntime->topology().linksOpenMwCore ? "yes" : "no",
            mRuntime->hasHeadlessOpenMwEngine() ? "yes" : "no", mRuntime->hasOpenMwWorld() ? "yes" : "no",
            mRuntime->hasPersistentWorld() ? "yes" : "no", worldState.loadedFromSave ? "yes" : "no",
            worldState.initializedNewWorld ? "yes" : "no", mRuntime->canOwnActorAuthority() ? "yes" : "no",
            mRuntime->bootstrap().blockedBy.c_str(), worldState.savePath.c_str());
    }

    bool ServerSimulation::isRecentServerCellChange(const Player& player, Clock::time_point now) const
    {
        const auto movementStateIt = mPlayerMovementStates.find(player.guid);
        if (movementStateIt == mPlayerMovementStates.end())
            return false;

        const PlayerMovementState& movementState = movementStateIt->second;
        if (!movementState.hasServerCellChangePacket)
            return false;

        const auto age = now - movementState.lastServerCellChangePacket;
        return age >= Clock::duration::zero() && age <= runtimePlayerFatalSnapshotCellChangeGrace;
    }

    bool ServerSimulation::isServerActorCombatTargetingPlayer(const ESM::Cell& cell, const Player& player) const
    {
        Cell* liveCell = findLoadedServerCellByDescription(cell.getDescription());
        if (liveCell == nullptr)
            return false;

        BaseActorList* actorList = liveCell->getActorList();
        if (actorList == nullptr)
            return false;

        const Target playerTarget = makePlayerAiTarget(player);
        for (const BaseActor& actor : actorList->baseActors)
        {
            if (actor.creatureStats.mDead)
                continue;

            if (actor.hasAiData && actor.hasAiTarget && actor.aiAction == BaseActorList::COMBAT
                && targetsReferToSameEntity(actor.aiTarget, playerTarget))
                return true;
        }

        return false;
    }

    bool ServerSimulation::shouldSuppressTransientPlayerDeath(const Player& player, const ESM::Cell& cell,
        bool previousDead, float previousHealth, bool incomingDead, float incomingHealth, Clock::time_point now,
        const char* source) const
    {
        if (!std::isfinite(previousHealth) || !std::isfinite(incomingHealth))
            return false;

        const bool wasLive = !previousDead && previousHealth > healthDeadEpsilon;
        const bool incomingFatal = incomingDead || incomingHealth <= healthDeadEpsilon;
        if (!wasLive || !incomingFatal)
            return false;

        const bool recentCellChange = isRecentServerCellChange(player, now);
        const bool serverCombatTarget = isServerActorCombatTargetingPlayer(cell, player);
        if (!recentCellChange && serverCombatTarget)
            return false;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Suppressing transient fatal %s player snapshot for %s in %s: previousHealth=%.3f incomingHealth=%.3f "
            "recentCellChange=%s serverCombatTarget=%s",
            source, player.npc.mName.c_str(), cell.getDescription().c_str(), previousHealth, incomingHealth,
            recentCellChange ? "yes" : "no", serverCombatTarget ? "yes" : "no");
        return true;
    }

    bool ServerSimulation::applyServerActorMeleeIfReady(Cell& cell, const BaseActor& actor,
        const ActorMovementKey& actorKey, const ESM::Position& presentationPosition,
        std::uint32_t positionSequence, float sampleIntervalSeconds, Clock::time_point now,
        BaseActorList& attackList, const Attack* runtimeAttack)
    {
        if (!actor.hasPositionData || !hasFiniteWorldPosition(presentationPosition)
            || !actorHasServerCombatTarget(actor) || !actor.aiTarget.isPlayer || !isLiveActorForServerMelee(actor))
        {
            mServerActorCombatStates.erase(actorKey);
            return false;
        }

        ESM::Position targetPosition;
        if (!getAiTargetPosition(cell, actor.aiTarget, targetPosition))
        {
            mServerActorCombatStates.erase(actorKey);
            return false;
        }

        const float deltaX = targetPosition.pos[0] - presentationPosition.pos[0];
        const float deltaY = targetPosition.pos[1] - presentationPosition.pos[1];
        const float distanceSquared = squaredHorizontalLength(deltaX, deltaY);
        if (!std::isfinite(distanceSquared) || distanceSquared > serverActorMeleeRangeSquared)
            return false;

        ServerActorCombatState& combatState = mServerActorCombatStates[actorKey];
        if (!combatState.hasNextMeleeAttack)
        {
            combatState.nextMeleeAttack = now;
            combatState.hasNextMeleeAttack = true;
        }

        if (now < combatState.nextMeleeAttack)
            return false;

        combatState.nextMeleeAttack = now + serverActorMeleeInterval;
        const std::string attackAnimation = runtimeAttack != nullptr ? runtimeAttack->attackAnimation : std::string();
        Attack serverAttack = makeServerActorMeleeAttack(actor, actor.aiTarget, targetPosition, attackAnimation);

        Player* target = Players::getPlayer(serverAttack.target.guid);
        bool becameDead = false;
        if (target != nullptr && applyAttackDamageToPlayer(*target, serverAttack, becameDead))
        {
            broadcastPlayerStats(*target);
            notifyPlayerStatsDynamic(*target);
            if (becameDead)
                notifyPlayerDeath(*target);
        }

        BaseActor attackActor = actor;
        attackActor.cell = cell.getCellData();
        attackActor.hasCombatData = true;
        attackActor.combatSequence = actor.hasCombatData ? actor.combatSequence + 1 : 1;
        attackActor.attack = std::move(serverAttack);
        attackActor.hasPositionData = true;
        attackActor.position = presentationPosition;
        attackActor.positionSequence = positionSequence;
        attackActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
        attackActor.movementLatencySeconds = 0.f;
        attackList.baseActors.push_back(std::move(attackActor));
        return true;
    }

    void ServerSimulation::clearActorCombatTargetsForPlayer(Player& player, const char* reason)
    {
        if (!mwmp::isPacketGuidAssigned(player.guid))
            return;

        CellController* cellController = CellController::get();
        ServerNetworking* networking = ServerNetworking::getPtr();
        if (cellController == nullptr || networking == nullptr || networking->getActorPacketController() == nullptr)
            return;

        ActorPacket* aiPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_AI);
        ActorPacket* positionPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_POSITION);

        const Target playerTarget = makePlayerAiTarget(player);
        const float stopSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(actorTickIntervalSeconds);
        std::size_t clearedActorCount = 0;

        for (Cell* cell : cellController->getCells())
        {
            if (cell == nullptr)
                continue;

            BaseActorList* actorList = cell->getActorList();
            if (actorList == nullptr || actorList->baseActors.empty())
                continue;

            const std::string cellKey = getCellSimulationKey(cell->getCellData());
            BaseActorList aiList;
            aiList.cell = cell->getCellData();
            aiList.guid = unassignedPacketGuid();
            aiList.action = BaseActorList::SET;
            aiList.isValid = true;

            BaseActorList positionList;
            positionList.cell = cell->getCellData();
            positionList.guid = unassignedPacketGuid();
            positionList.action = BaseActorList::SET;
            positionList.isValid = true;

            for (BaseActor& actor : actorList->baseActors)
            {
                if (!actorHasServerCombatTarget(actor) || !targetsReferToSameEntity(actor.aiTarget, playerTarget))
                    continue;

                const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
                mServerActorCombatStates.erase(actorKey);
                mActorPathgridRouteStates.erase(actorKey);
                mActorWanderStates.erase(actorKey);
                mRuntimeActorMovementStates.erase(actorKey);
                mRuntimeClientAiPresentedActors.erase(actorKey);

                actor.hasAiData = true;
                actor.aiAction = BaseActorList::CANCEL;
                actor.aiDistance = 0;
                actor.aiDuration = 0;
                actor.aiShouldRepeat = false;
                actor.hasAiTarget = false;
                actor.aiTarget = Target();
                actor.aiCoordinates = zeroPosition();
                actor.direction = zeroPosition();

                BaseActor aiActor = actor;
                aiActor.hasAiData = true;
                aiActor.hasPositionData = false;
                aiList.baseActors.push_back(std::move(aiActor));

                if (actor.hasPositionData)
                {
                    ++actor.positionSequence;
                    actor.movementSampleIntervalSeconds = stopSampleIntervalSeconds;
                    actor.movementLatencySeconds = 0.f;

                    BaseActor positionActor = actor;
                    positionActor.hasPositionData = true;
                    positionList.baseActors.push_back(std::move(positionActor));
                }

                ++clearedActorCount;
            }

            aiList.count = static_cast<unsigned int>(aiList.baseActors.size());
            if (aiList.count != 0 && aiPacket != nullptr)
            {
                cell->readActorList(ID_ACTOR_AI, &aiList);
                aiPacket->setActorList(&aiList);
                cell->sendToLoaded(aiPacket, &aiList);
            }

            positionList.count = static_cast<unsigned int>(positionList.baseActors.size());
            if (positionList.count != 0 && positionPacket != nullptr)
            {
                cell->readActorList(ID_ACTOR_POSITION, &positionList);
                positionPacket->setActorList(&positionList);
                cell->sendToLoaded(positionPacket, &positionList);
            }
        }

        if (clearedActorCount != 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Cleared %zu actor combat target(s) for player %s because %s",
                clearedActorCount, player.npc.mName.c_str(), reason != nullptr ? reason : "the player state reset");
        }
    }

    void ServerSimulation::tick()
    {
        const Clock::time_point now = Clock::now();
        const float deltaSeconds = clampDeltaSeconds(std::chrono::duration<float>(now - mLastTick).count());
        mLastTick = now;

        updateRuntimeSimulationCells();
        mRuntime->tick(deltaSeconds);
        std::vector<BaseActorList> runtimeActorSnapshots;
        const bool exportedRuntimeActorSnapshots = mRuntime->collectActorSnapshots(runtimeActorSnapshots);
        if (exportedRuntimeActorSnapshots)
            applyRuntimeActorSnapshots(runtimeActorSnapshots, deltaSeconds);
        std::vector<SimulationPlayerSnapshot> runtimePlayerSnapshots;
        if (mRuntime->collectPlayerSnapshots(runtimePlayerSnapshots))
            applyRuntimePlayerSnapshots(runtimePlayerSnapshots);
        logRuntimeActorMovementHealthIfNeeded(now);

        if (!canAuthoritativelySimulateActors())
            return;

        mActorTickAccumulator += deltaSeconds;
        if (mActorTickAccumulator < actorTickIntervalSeconds)
            return;

        const float actorDeltaSeconds = std::min(mActorTickAccumulator, maxMovementStepSeconds);
        mActorTickAccumulator = 0.f;
        tickActors(actorDeltaSeconds, mRuntime->hasOpenMwWorld());
    }

    void ServerSimulation::removePlayer(PacketGuid guid)
    {
        mPlayerMovementStates.erase(guid);
        mPlayerAcceptedCells.erase(guid);
        for (auto it = mActorInteractionLeases.begin(); it != mActorInteractionLeases.end();)
        {
            if (it->second.playerGuid == guid)
                it = mActorInteractionLeases.erase(it);
            else
                ++it;
        }

        for (auto it = mShadowCellAuthority.begin(); it != mShadowCellAuthority.end();)
        {
            const std::string cellKey = it->first;
            ShadowCellAuthorityState& state = it->second;
            const std::string displayDescription = state.hasCell
                ? state.cell.getDescription()
                : (state.displayDescription.empty() ? cellKey : state.displayDescription);
            const bool wasAuthority = state.authority == guid;
            const bool wasVisitor = eraseGuid(state.visitors, guid);
            state.visitorLoadCounts.erase(guid);

            if (!wasVisitor && !wasAuthority)
            {
                ++it;
                continue;
            }

            if (state.visitors.empty())
            {
                updateCellSimulationInterest(cellKey, state);
                state.authority = mwmp::unassignedPacketGuid();
                clearLiveCellActorAuthority(cellKey, "final visitor disconnected");
                for (auto combatIt = mServerActorCombatStates.begin(); combatIt != mServerActorCombatStates.end();)
                {
                    if (combatIt->first.cellKey == cellKey)
                        combatIt = mServerActorCombatStates.erase(combatIt);
                    else
                        ++combatIt;
                }
                for (auto cancelIt = mRuntimeClientAiPresentedActors.begin();
                     cancelIt != mRuntimeClientAiPresentedActors.end();)
                {
                    if (cancelIt->cellKey == cellKey)
                        cancelIt = mRuntimeClientAiPresentedActors.erase(cancelIt);
                    else
                        ++cancelIt;
                }
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Cleared C++ snapshot reporter for cell %s because its final visitor disconnected",
                    displayDescription.c_str());
                it = mShadowCellAuthority.erase(it);
                continue;
            }

            updateCellSimulationInterest(cellKey, state);
            broadcastCellActivityEvent(displayDescription, state);

            if (wasAuthority || !isShadowCellAuthorityCandidate(state, state.authority))
                refreshShadowCellAuthority(cellKey, state, "current authority disconnected", unassignedPacketGuid(), guid);
            else
                broadcastShadowCellAuthorityEvent(displayDescription, state);

            ++it;
        }
    }

    void ServerSimulation::notePlayerDialogueChoice(Player& player, const BaseObjectList& objectList)
    {
        if (!mwmp::isPacketGuidAssigned(player.guid))
            return;

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return;

        ESM::Cell lookupCell = objectList.cell;
        Cell* serverCell = cellController->getCell(&lookupCell);
        if (serverCell == nullptr)
            return;

        const Clock::time_point now = Clock::now();
        const std::string cellKey = getCellSimulationKey(objectList.cell);
        for (const BaseObject& object : objectList.baseObjects)
        {
            if (object.isPlayer)
                continue;

            const ActorMovementKey actorKey{ cellKey, object.refNum, object.mpNum };
            const bool isDialogueStart = object.dialogueChoiceType == DialogueChoiceType::DIALOGUE_START;
            const bool isDialogueEnd = object.dialogueChoiceType == DialogueChoiceType::DIALOGUE_END;

            if (isDialogueEnd)
            {
                const auto leaseIt = mActorInteractionLeases.find(actorKey);
                if (leaseIt != mActorInteractionLeases.end()
                    && (!mwmp::isPacketGuidAssigned(leaseIt->second.playerGuid)
                        || leaseIt->second.playerGuid == player.guid))
                    mActorInteractionLeases.erase(leaseIt);
                continue;
            }

            BaseActor* actor = serverCell->getActor(object.refNum, object.mpNum);
            if (actor == nullptr)
                continue;

            const bool actorAlreadyLocked = isActorInteractionLocked(actorKey, now);
            auto existingLease = mActorInteractionLeases.find(actorKey);
            if (actorAlreadyLocked && existingLease != mActorInteractionLeases.end()
                && existingLease->second.playerGuid != player.guid)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Ignoring dialogue interaction from %s for busy actor %s %u-%u in %s",
                    player.npc.mName.c_str(), object.refId.c_str(), object.refNum, object.mpNum, cellKey.c_str());
                continue;
            }

            ActorInteractionLease& lease = mActorInteractionLeases[actorKey];
            lease.playerGuid = player.guid;
            lease.expiresAt = now + std::chrono::minutes(5);

            if (isDialogueStart)
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Started dialogue interaction lease for %s %u-%u in %s with player %s",
                    object.refId.c_str(), object.refNum, object.mpNum, cellKey.c_str(), player.npc.mName.c_str());

            stopActorForInteraction(*serverCell, *actor, actorKey);
        }
    }

    void ServerSimulation::noteCellLoadedByPlayer(unsigned short playerId, std::string cellDescription)
    {
        if (cellDescription.empty())
            return;

        Player* player = Players::getPlayer(playerId);
        if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid))
            return;

        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        const std::string cellKey = liveCell != nullptr
            ? getCellSimulationKey(liveCell->getCellData())
            : getCellDescriptionSimulationKey(cellDescription);
        for (auto it = mRuntimeClientAiPresentedActors.begin(); it != mRuntimeClientAiPresentedActors.end();)
        {
            if (it->cellKey == cellKey)
                it = mRuntimeClientAiPresentedActors.erase(it);
            else
                ++it;
        }
        ShadowCellAuthorityState& state = mShadowCellAuthority[cellKey];
        if (state.displayDescription.empty())
            state.displayDescription = cellDescription;
        const bool hadVisitors = !state.visitors.empty();
        std::uint32_t& visitorLoadCount = state.visitorLoadCounts[player->guid];
        const bool wasVisitor = visitorLoadCount > 0 || containsGuid(state.visitors, player->guid);
        ++visitorLoadCount;
        const bool previousAuthorityWasCandidate = isShadowCellAuthorityCandidate(state, state.authority);

        if (liveCell != nullptr)
        {
            state.cell = liveCell->getCellData();
            state.hasCell = true;
            state.displayDescription = liveCell->getShortDescription();
        }
        else if (cellDescriptionMatches(player->cell, cellDescription))
        {
            state.cell = player->cell;
            state.hasCell = true;
            state.displayDescription = player->cell.getDescription();
        }

        if (!containsGuid(state.visitors, player->guid))
            state.visitors.push_back(player->guid);

        const std::string displayDescription = state.hasCell ? state.cell.getDescription() : state.displayDescription;
        updateCellSimulationInterest(cellKey, state);

        if (canAuthoritativelySimulateActors())
        {
            liveCell = findLoadedServerCellByDescription(cellKey);
            if (liveCell != nullptr)
                liveCell->seedActorListFromServerWorldState();

            broadcastCellActivityEvent(displayDescription, state);
            state.authority = mwmp::unassignedPacketGuid();
            clearLiveCellActorAuthority(cellKey, "server actor authority owns the cell");
            broadcastShadowCellAuthorityEvent(displayDescription, state);
            return;
        }

        broadcastCellActivityEvent(displayDescription, state);

        if (!previousAuthorityWasCandidate)
        {
            const PacketGuid preferredGuid = hadVisitors ? mwmp::unassignedPacketGuid() : player->guid;
            refreshShadowCellAuthority(cellKey, state, "cell authority was missing or stale", preferredGuid);
        }
        else if (!wasVisitor)
        {
            applyShadowCellAuthorityToLiveCell(cellKey, state, true);
            sendShadowCellAuthorityEvent(*player, displayDescription, state);
        }
    }

    void ServerSimulation::noteCellUnloadedByPlayer(unsigned short playerId, std::string cellDescription)
    {
        if (cellDescription.empty())
            return;

        Player* player = Players::getPlayer(playerId);
        if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid))
            return;

        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        const std::string cellKey = liveCell != nullptr
            ? getCellSimulationKey(liveCell->getCellData())
            : getCellDescriptionSimulationKey(cellDescription);
        auto stateIt = mShadowCellAuthority.find(cellKey);
        if (stateIt == mShadowCellAuthority.end())
            return;

        ShadowCellAuthorityState& state = stateIt->second;
        const std::string displayDescription = state.hasCell
            ? state.cell.getDescription()
            : (state.displayDescription.empty() ? cellDescription : state.displayDescription);
        const bool wasAuthority = state.authority == player->guid;
        auto loadCountIt = state.visitorLoadCounts.find(player->guid);
        if (loadCountIt != state.visitorLoadCounts.end() && loadCountIt->second > 1)
        {
            --loadCountIt->second;
            return;
        }
        if (loadCountIt != state.visitorLoadCounts.end())
            state.visitorLoadCounts.erase(loadCountIt);
        const bool wasVisitor = eraseGuid(state.visitors, player->guid);

        if (!wasVisitor)
            return;

        for (auto it = mActorInteractionLeases.begin(); it != mActorInteractionLeases.end();)
        {
            if (it->second.playerGuid == player->guid)
                it = mActorInteractionLeases.erase(it);
            else
                ++it;
        }

        if (state.visitors.empty())
        {
            updateCellSimulationInterest(cellKey, state);
            state.authority = mwmp::unassignedPacketGuid();
            clearLiveCellActorAuthority(cellKey, "no valid visitors remain");
            sendCellActivityEvent(*player, displayDescription, state, false);
            for (auto combatIt = mServerActorCombatStates.begin(); combatIt != mServerActorCombatStates.end();)
            {
                if (combatIt->first.cellKey == cellKey)
                    combatIt = mServerActorCombatStates.erase(combatIt);
                else
                    ++combatIt;
            }
            for (auto cancelIt = mRuntimeClientAiPresentedActors.begin();
                 cancelIt != mRuntimeClientAiPresentedActors.end();)
            {
                if (cancelIt->cellKey == cellKey)
                    cancelIt = mRuntimeClientAiPresentedActors.erase(cancelIt);
                else
                    ++cancelIt;
            }
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Cleared C++ snapshot reporter for cell %s because no valid visitors remain",
                displayDescription.c_str());
            mShadowCellAuthority.erase(stateIt);
            return;
        }

        updateCellSimulationInterest(cellKey, state);
        sendCellActivityEvent(*player, displayDescription, state, false);
        broadcastCellActivityEvent(displayDescription, state);

        if (wasAuthority || !isShadowCellAuthorityCandidate(state, state.authority))
            refreshShadowCellAuthority(cellKey, state, "current authority left", unassignedPacketGuid(), player->guid);
        else
            broadcastShadowCellAuthorityEvent(displayDescription, state);
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
                    "C++ snapshot reporter for cell %s has %zu visitor(s) and reporter %s after %s, but no live "
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
            "C++ snapshot reporter mismatch for cell %s after %s: shadow=%s liveCellAuthority=%s visitors=%zu",
            cellDescription.c_str(), context != nullptr ? context : "cell event",
            shadowAuthorityAuditName(shadowAuthority).c_str(), shadowAuthorityAuditName(liveAuthority).c_str(),
            shadowVisitorCount);
    }

    std::optional<PacketGuid> ServerSimulation::getShadowCellAuthority(const std::string& cellDescription) const
    {
        const auto stateIt = mShadowCellAuthority.find(getCellDescriptionSimulationKey(cellDescription));
        if (stateIt == mShadowCellAuthority.end()
            || !mwmp::isPacketGuidAssigned(stateIt->second.authority))
            return std::nullopt;

        return stateIt->second.authority;
    }

    std::size_t ServerSimulation::getShadowCellVisitorCount(const std::string& cellDescription) const
    {
        const auto stateIt = mShadowCellAuthority.find(getCellDescriptionSimulationKey(cellDescription));
        if (stateIt == mShadowCellAuthority.end())
            return 0;

        return stateIt->second.visitors.size();
    }

    void ServerSimulation::sendLuaBridgeState(Player& player) const
    {
        sendRuntimeStatusEvent(player);

        for (const auto& [cellKey, state] : mShadowCellAuthority)
        {
            if (!containsGuid(state.visitors, player.guid))
                continue;

            const std::string cellDescription = state.hasCell
                ? state.cell.getDescription()
                : (state.displayDescription.empty() ? cellKey : state.displayDescription);
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

    bool ServerSimulation::runtimeOwnsActorCell(const Cell& serverCell) const
    {
        return mRuntime != nullptr && mRuntime->hasOpenMwWorld() && canAuthoritativelySimulateActors()
            && serverCell.hasSimulationInterest();
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

    PacketGuid ServerSimulation::refreshShadowCellAuthority(const std::string& cellKey,
        ShadowCellAuthorityState& state, const char* reason, PacketGuid preferredGuid, PacketGuid excludedGuid)
    {
        const std::string displayDescription = state.hasCell
            ? state.cell.getDescription()
            : (state.displayDescription.empty() ? cellKey : state.displayDescription);
        if (canAuthoritativelySimulateActors())
        {
            const bool wasAssigned = mwmp::isPacketGuidAssigned(state.authority);
            state.authority = mwmp::unassignedPacketGuid();
            const bool liveCleared = clearLiveCellActorAuthority(cellKey, "server actor authority owns the cell");
            if (wasAssigned || liveCleared)
                broadcastShadowCellAuthorityEvent(displayDescription, state);
            return state.authority;
        }

        PacketGuid newAuthority = mwmp::unassignedPacketGuid();
        if (preferredGuid != excludedGuid && isShadowCellAuthorityCandidate(state, preferredGuid))
            newAuthority = preferredGuid;
        else
            newAuthority = getLowestPingShadowCellAuthority(state, excludedGuid);

        if (!mwmp::isPacketGuidAssigned(newAuthority))
        {
            const bool wasAssigned = mwmp::isPacketGuidAssigned(state.authority);
            if (mwmp::isPacketGuidAssigned(state.authority))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Cleared C++ snapshot reporter for cell %s because no valid visitors remain",
                    displayDescription.c_str());
            }
            state.authority = mwmp::unassignedPacketGuid();
            const bool liveCleared = clearLiveCellActorAuthority(cellKey, "no valid authority candidate remains");
            if (wasAssigned || liveCleared)
                broadcastShadowCellAuthorityEvent(displayDescription, state);
            return state.authority;
        }

        const bool authorityChanged = state.authority != newAuthority;
        if (authorityChanged)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Assigning C++ snapshot reporter for cell %s to %s because %s",
                displayDescription.c_str(), shadowAuthorityName(newAuthority).c_str(),
                reason != nullptr ? reason : "authority was refreshed");
        }

        state.authority = newAuthority;
        if (authorityChanged)
        {
            applyShadowCellAuthorityToLiveCell(cellKey, state);
            broadcastShadowCellAuthorityEvent(displayDescription, state);
        }
        return state.authority;
    }

    bool ServerSimulation::clearLiveCellActorAuthority(const std::string& cellDescription, const char* reason) const
    {
        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        if (liveCell == nullptr)
            return false;

        const PacketGuid previousAuthority = *liveCell->getAuthority();
        if (!mwmp::isPacketGuidAssigned(previousAuthority))
            return false;

        liveCell->setAuthority(mwmp::unassignedPacketGuid());
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Cleared live actor authority of cell %s from %s because %s",
            liveCell->getShortDescription().c_str(), shadowAuthorityName(previousAuthority).c_str(),
            reason != nullptr ? reason : "authority was cleared");
        return true;
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
            "Applied C++ client snapshot reporter for cell %s to %s%s",
            liveCell->getShortDescription().c_str(), shadowAuthorityName(state.authority).c_str(),
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
            "Requested C++ actor list snapshot for cell %s from snapshot reporter %s because %s",
            liveCell.getShortDescription().c_str(), shadowAuthorityName(state.authority).c_str(),
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
        const bool serverActorAuthority = canAuthoritativelySimulateActors();
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
        payload += ",\"snapshotReporterGuid\":";
        payload += jsonString(authorityGuid);
        payload += ",\"snapshotReporterName\":";
        payload += jsonString(authorityName);
        payload += ",\"authorityMode\":";
        payload += jsonString(cellSimulationAuthorityMode(serverActorAuthority));
        payload += ",\"simulationOwner\":";
        payload += jsonString(cellSimulationOwner(serverActorAuthority));
        payload += ",\"clientRole\":";
        payload += jsonString(localClientCellRole(serverActorAuthority, isAuthority));
        payload += ",\"serverOwnsSimulation\":";
        payload += jsonBool(serverActorAuthority);
        payload += ",\"isAuthority\":";
        payload += jsonBool(isAuthority);
        payload += ",\"visitorCount\":";
        payload += std::to_string(state.visitors.size());
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(serverActorAuthority);
        payload += "}";

        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "cell_authority", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send C++ snapshot reporter event for cell %s to %s",
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
        const bool enabled = canAuthoritativelySimulateActors() && !state.visitors.empty();
        Cell* liveCell = findLoadedServerCellByDescription(cellDescription);
        const std::string displayDescription = state.hasCell
            ? state.cell.getDescription()
            : (state.displayDescription.empty() ? cellDescription : state.displayDescription);
        if (liveCell == nullptr && enabled && state.hasCell)
        {
            CellController* cellController = CellController::get();
            if (cellController != nullptr)
                liveCell = cellController->addCell(state.cell);
        }
        if (liveCell == nullptr)
            return false;

        if (liveCell->hasSimulationInterest() != enabled)
        {
            liveCell->setSimulationInterest(enabled);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "C++ %s server simulation interest for cell %s with %zu active visitor(s)",
                enabled ? "enabled" : "disabled", displayDescription.c_str(), state.visitors.size());
        }

        return liveCell->hasSimulationInterest();
    }

    bool ServerSimulation::ensurePlayerCurrentSimulationCell(Player& player, const char* reason)
    {
        if (!canAuthoritativelySimulateActors() || !mwmp::isPacketGuidAssigned(player.guid)
            || player.getLoadState() == Player::KICKED)
            return false;

        const std::string cellDescription = player.cell.getDescription();
        if (cellDescription.empty())
            return false;
        const std::string cellKey = getCellSimulationKey(player.cell);

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return false;

        Cell* liveCell = findLoadedServerCellByDescription(cellKey);
        if (liveCell == nullptr)
            liveCell = cellController->addCell(player.cell);
        if (liveCell == nullptr)
            return false;

        const auto stateIt = mShadowCellAuthority.find(cellKey);
        const bool stateTracksPlayer = stateIt != mShadowCellAuthority.end()
            && containsGuid(stateIt->second.visitors, player.guid);
        if (liveCell->hasPlayer(&player) && stateTracksPlayer)
            return false;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Repairing server simulation interest for current player cell %s player=%s reason=%s",
            cellDescription.c_str(), player.npc.mName.c_str(), reason != nullptr ? reason : "unspecified");

        if (!liveCell->hasPlayer(&player))
            liveCell->addPlayer(&player);
        else
            noteCellLoadedByPlayer(player.getId(), cellDescription);

        return true;
    }

    void ServerSimulation::reconcileCurrentPlayerSimulationCells(RuntimeFocusSelectionStats& focusSelectionStats)
    {
        TPlayers* players = Players::getPlayers();
        if (players == nullptr)
            return;

        for (const auto& [guid, player] : *players)
        {
            static_cast<void>(guid);
            if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid)
                || player->getLoadState() == Player::KICKED)
                continue;

            const std::string cellDescription = player->cell.getDescription();
            if (cellDescription.empty())
                continue;

            ++focusSelectionStats.currentPlayerCellCount;
            if (ensurePlayerCurrentSimulationCell(*player, "runtime focus reconciliation"))
            {
                ++focusSelectionStats.repairedCurrentPlayerCellCount;
                focusSelectionStats.lastRepairedCurrentPlayerCellDescription = player->cell.getDescription();
            }
        }
    }

    std::vector<SimulationPlayerTarget> ServerSimulation::collectRuntimePlayerActors() const
    {
        std::vector<SimulationPlayerTarget> result;
        TPlayers* players = Players::getPlayers();
        if (players == nullptr)
            return result;

        for (const auto& [guid, player] : *players)
        {
            static_cast<void>(guid);
            if (player == nullptr || !mwmp::isPacketGuidAssigned(player->guid)
                || player->getLoadState() == Player::KICKED)
                continue;

            if (!player->hasFinitePositionPacket() || player->cell.getDescription().empty())
                continue;

            SimulationPlayerTarget target;
            target.cell = player->cell;
            target.position = player->position;
            target.guid = player->guid;
            target.name = player->npc.mName;
            target.npc = player->npc;
            target.hasBaseInfo = true;
            if (!player->charClass.mId.empty())
            {
                target.classId = player->charClass.mId;
                target.hasClass = true;
            }
            if (player->hasAcceptedEquipmentPacket)
            {
                for (int slot = 0; slot < equipmentSlotCount; ++slot)
                    target.equipmentItems[slot] = player->equipmentItems[slot];
                target.hasEquipmentData = true;
            }
            target.hasPosition = true;
            if (player->hasFiniteDynamicStats())
            {
                target.creatureStats = makeSimpleCreatureStats(player->creatureStats);
                target.hasStatsDynamicData = true;
            }
            result.push_back(std::move(target));
        }

        return result;
    }

    void ServerSimulation::updateRuntimeSimulationCells()
    {
        if (mRuntime == nullptr || !canAuthoritativelySimulateActors())
            return;

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return;

        auto buildFocusForVisitor = [&](const ESM::Cell& cell, Player& visitor) -> std::optional<SimulationCellFocus> {
            if (visitor.getLoadState() == Player::KICKED)
                return std::nullopt;

            if (!visitor.hasFinitePositionPacket() || !isSameSimulationCell(visitor.cell, cell))
                return std::nullopt;

            SimulationCellFocus focus;
            focus.cell = cell;
            focus.position = visitor.position;
            focus.hasPosition = true;
            focus.playerGuid = visitor.guid;
            focus.playerName = visitor.npc.mName;
            focus.hasPlayer = true;
            if (visitor.hasFiniteDynamicStats())
            {
                focus.playerStats = makeSimpleCreatureStats(visitor.creatureStats);
                focus.hasPlayerStats = true;
            }
            if (visitor.hasAcceptedEquipmentPacket)
            {
                for (int slot = 0; slot < equipmentSlotCount; ++slot)
                    focus.playerEquipmentItems[slot] = visitor.equipmentItems[slot];
                focus.hasPlayerEquipmentData = true;
            }

            return focus;
        };

        auto selectRepresentativePlayerFocusForState = [&](const ShadowCellAuthorityState& state, const ESM::Cell& cell,
                                                           Cell* liveCell)
            -> std::optional<SimulationCellFocus> {
            std::map<PacketGuid, SimulationCellFocus> focusesByGuid;
            std::vector<PacketGuid> focusOrder;
            std::set<PacketGuid> seenVisitors;
            for (PacketGuid visitorGuid : state.visitors)
            {
                if (!seenVisitors.insert(visitorGuid).second)
                    continue;

                Player* visitor = Players::getPlayer(visitorGuid);
                if (visitor == nullptr)
                    continue;

                if (std::optional<SimulationCellFocus> focus = buildFocusForVisitor(cell, *visitor))
                {
                    focusOrder.push_back(visitorGuid);
                    focusesByGuid[visitorGuid] = std::move(*focus);
                }
            }

            if (focusOrder.empty())
                return std::nullopt;

            std::map<PacketGuid, std::size_t> combatTargetCounts;
            if (liveCell != nullptr)
            {
                if (BaseActorList* actorList = liveCell->getActorList())
                {
                    for (const BaseActor& actor : actorList->baseActors)
                    {
                        if (!actor.hasAiData || !actor.hasAiTarget
                            || actor.aiAction != BaseActorList::COMBAT || !actor.aiTarget.isPlayer
                            || !mwmp::isPacketGuidAssigned(actor.aiTarget.guid)
                            || focusesByGuid.find(actor.aiTarget.guid) == focusesByGuid.end())
                            continue;

                        ++combatTargetCounts[actor.aiTarget.guid];
                    }
                }
            }

            PacketGuid selectedGuid = focusOrder.front();
            std::size_t selectedCombatTargetCount = 0;
            for (PacketGuid visitorGuid : focusOrder)
            {
                const auto countIt = combatTargetCounts.find(visitorGuid);
                const std::size_t combatTargetCount = countIt != combatTargetCounts.end() ? countIt->second : 0;
                if (combatTargetCount > selectedCombatTargetCount)
                {
                    selectedGuid = visitorGuid;
                    selectedCombatTargetCount = combatTargetCount;
                }
            }

            return focusesByGuid[selectedGuid];
        };

        std::vector<SimulationCellFocus> simulationFocuses;
        std::set<std::string> focusedCellKeys;
        std::set<std::string> authorityOnlyCellKeys;
        RuntimeFocusSelectionStats focusSelectionStats;
        reconcileCurrentPlayerSimulationCells(focusSelectionStats);
        for (const auto& [cellKey, state] : mShadowCellAuthority)
        {
            if (state.visitors.empty() || !state.hasCell)
                continue;

            const std::string displayDescription = state.hasCell
                ? state.cell.getDescription()
                : (state.displayDescription.empty() ? cellKey : state.displayDescription);

            Cell* liveCell = findLoadedServerCellByDescription(cellKey);
            if (liveCell == nullptr)
                liveCell = cellController->addCell(state.cell);
            if (liveCell != nullptr && !liveCell->hasSimulationInterest())
                liveCell->setSimulationInterest(true);

            std::optional<SimulationCellFocus> playerFocus
                = selectRepresentativePlayerFocusForState(state, state.cell, liveCell);
            ++focusSelectionStats.candidateCellCount;
            if (playerFocus)
            {
                simulationFocuses.push_back(std::move(*playerFocus));
                focusedCellKeys.insert(cellKey);
                ++focusSelectionStats.directFocusCellCount;
                ++focusSelectionStats.directFocusPlayerCount;
            }
            else
            {
                authorityOnlyCellKeys.insert(cellKey);
                ++focusSelectionStats.authorityOnlyCellCount;
                ++focusSelectionStats.deferredLoadedCellCount;
                focusSelectionStats.lastAuthorityOnlyCellDescription = displayDescription;
                focusSelectionStats.lastDeferredLoadedCellDescription = displayDescription;
            }
        }

        for (Cell* cell : cellController->getCells())
        {
            if (cell == nullptr || !cell->hasSimulationInterest())
                continue;
            const std::string cellKey = getCellSimulationKey(cell->getCellData());
            if (focusedCellKeys.find(cellKey) != focusedCellKeys.end())
                continue;
            const std::string cellDescription = cell->getShortDescription();
            if (authorityOnlyCellKeys.find(cellKey) != authorityOnlyCellKeys.end())
                continue;

            const auto stateIt = mShadowCellAuthority.find(cellKey);
            if (stateIt != mShadowCellAuthority.end())
            {
                if (!stateIt->second.visitors.empty())
                {
                    ++focusSelectionStats.authorityOnlyCellCount;
                    focusSelectionStats.lastAuthorityOnlyCellDescription = cellDescription;
                    continue;
                }

                ++focusSelectionStats.staleSimulationInterestCellCount;
                focusSelectionStats.lastStaleSimulationInterestCellDescription = cellDescription;
                continue;
            }

            SimulationCellFocus focus;
            focus.cell = cell->getCellData();

            simulationFocuses.push_back(std::move(focus));
            ++focusSelectionStats.candidateCellCount;
            ++focusSelectionStats.scriptFocusCellCount;
            ++focusSelectionStats.scriptFocusWithoutPositionCellCount;
        }

        mRuntimeFocusSelectionStats = std::move(focusSelectionStats);
        mRuntime->setPlayerActors(collectRuntimePlayerActors());
        mRuntime->setSimulationCellFocuses(simulationFocuses);
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
        ActorPacket* cellChangePacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_CELL_CHANGE);
        ActorPacket* positionPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_POSITION);
        ActorPacket* animFlagsPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_ANIM_FLAGS);
        ActorPacket* statsPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_STATS_DYNAMIC);
        ActorPacket* equipmentPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_EQUIPMENT);
        ActorPacket* aiPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_AI);
        ActorPacket* attackPacket = networking->getActorPacketController()->GetPacket(ID_ACTOR_ATTACK);
        if (listPacket == nullptr || cellChangePacket == nullptr || positionPacket == nullptr || animFlagsPacket == nullptr
            || statsPacket == nullptr || equipmentPacket == nullptr || aiPacket == nullptr || attackPacket == nullptr)
            return;

        const Clock::time_point now = Clock::now();
        const float sampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);
        ++mRuntimeActorSnapshotStats.snapshotBatchCount;

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

            ++mRuntimeActorSnapshotStats.snapshotCellCount;
            mRuntimeActorSnapshotStats.snapshotActorCount += runtimeList.baseActors.size();
            mRuntimeActorSnapshotStats.lastSnapshotCellDescription = serverCell->getShortDescription();
            mRuntimeActorSnapshotStats.lastSnapshotActorCount = runtimeList.baseActors.size();
            const std::string runtimeCellKey = getCellSimulationKey(runtimeList.cell);
            const bool runtimeOwnsClientRenderedAi = canAuthoritativelySimulateActors()
                && mRuntime != nullptr && mRuntime->hasOpenMwWorld();

            const bool hadActorListSnapshot = serverCell->hasActorListSnapshot();
            std::map<ActorIdentityPair, BaseActor> cachedActorsBeforeIdentity;
            for (const BaseActor& runtimeActor : runtimeList.baseActors)
            {
                if (BaseActor* cachedActor = serverCell->getActor(runtimeActor.refNum, runtimeActor.mpNum))
                    cachedActorsBeforeIdentity.emplace(actorIdentityPair(runtimeActor), *cachedActor);
            }

            for (const BaseActor& runtimeActor : runtimeList.baseActors)
            {
                if (!runtimeActor.hasPositionData || !mwmp::isFiniteActorMovementSnapshot(runtimeActor))
                    continue;

                const std::optional<RuntimeActorCellSource> source
                    = findRuntimeActorSourceCell(*cellController, runtimeList.cell, runtimeActor);
                if (!source || source->cell == nullptr)
                    continue;

                BaseActor movedActor = runtimeActor;
                movedActor.cell = runtimeList.cell;
                movedActor.hasPositionData = true;
                movedActor.positionSequence = source->actor.hasPositionData
                    ? source->actor.positionSequence + 1
                    : 1;
                movedActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                movedActor.movementLatencySeconds = 0.f;
                movedActor.direction = zeroMovementDirectionLike(movedActor.position);

                BaseActorList cellChangeList;
                cellChangeList.cell = source->cell->getCellData();
                cellChangeList.guid = unassignedPacketGuid();
                cellChangeList.action = BaseActorList::SET;
                cellChangeList.isValid = true;
                cellChangeList.baseActors.push_back(std::move(movedActor));
                cellChangeList.count = static_cast<unsigned int>(cellChangeList.baseActors.size());

                ActorProcessor::cacheCellChange(cellChangeList);
                ActorProcessor::sendCellChangeToLoaded(*cellChangePacket, cellChangeList);
            }

            BaseActorList identityList = runtimeList;
            identityList.guid = unassignedPacketGuid();
            identityList.action = BaseActorList::SET;
            for (BaseActor& identityActor : identityList.baseActors)
            {
                const auto previousActorIt = cachedActorsBeforeIdentity.find(actorIdentityPair(identityActor));
                const BaseActor* previousActor = previousActorIt != cachedActorsBeforeIdentity.end()
                    ? &previousActorIt->second
                    : nullptr;
                static_cast<void>(applyActualRuntimeMovementDirection(identityActor, previousActor));
                identityActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                identityActor.movementLatencySeconds = 0.f;
            }
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

            BaseActorList animFlagsList;
            animFlagsList.cell = runtimeList.cell;
            animFlagsList.guid = unassignedPacketGuid();
            animFlagsList.action = BaseActorList::SET;
            animFlagsList.isValid = true;

            BaseActorList statsList;
            statsList.cell = runtimeList.cell;
            statsList.guid = unassignedPacketGuid();
            statsList.action = BaseActorList::SET;
            statsList.isValid = true;

            BaseActorList equipmentList;
            equipmentList.cell = runtimeList.cell;
            equipmentList.guid = unassignedPacketGuid();
            equipmentList.action = BaseActorList::SET;
            equipmentList.isValid = true;

            BaseActorList aiList;
            aiList.cell = runtimeList.cell;
            aiList.guid = unassignedPacketGuid();
            aiList.action = BaseActorList::SET;
            aiList.isValid = true;

            BaseActorList attackList;
            attackList.cell = runtimeList.cell;
            attackList.guid = unassignedPacketGuid();
            attackList.action = BaseActorList::SET;
            attackList.isValid = true;

            for (const BaseActor& runtimeActor : runtimeList.baseActors)
            {
                BaseActor* cachedActor = serverCell->getActor(runtimeActor.refNum, runtimeActor.mpNum);
                const auto previousActorIt = cachedActorsBeforeIdentity.find(actorIdentityPair(runtimeActor));
                const BaseActor* previousActor = previousActorIt != cachedActorsBeforeIdentity.end()
                    ? &previousActorIt->second
                    : nullptr;
                const std::uint32_t nextPositionSequence = cachedActor != nullptr && cachedActor->hasPositionData
                    ? cachedActor->positionSequence + 1
                    : 1;
                const ActorMovementKey actorKey{ runtimeCellKey, runtimeActor.refNum, runtimeActor.mpNum };
                const bool actorInteractionLocked = isActorInteractionLocked(actorKey, now)
                    && !(cachedActor != nullptr && actorHasServerCombatTarget(*cachedActor));
                const bool useRuntimeFallbackMovement = shouldUseRuntimeFallbackMovement(
                    actorKey, runtimeActor, cachedActor);
                const bool rawRuntimeMovementIntent = hasMeaningfulMovementIntent(runtimeActor.direction);
                const bool runtimeTransformDelta = previousActor != nullptr
                    && hasMeaningfulRuntimeTransformDelta(runtimeActor, *previousActor);
                if (rawRuntimeMovementIntent)
                    ++mRuntimeActorSnapshotStats.rawMovementIntentSnapshotCount;
                if (runtimeTransformDelta)
                    ++mRuntimeActorSnapshotStats.transformDeltaSnapshotCount;
                if (rawRuntimeMovementIntent && previousActor != nullptr && previousActor->hasPositionData
                    && !runtimeTransformDelta)
                {
                    ++mRuntimeActorSnapshotStats.rawMovementIntentWithoutTransformCount;
                    mRuntimeActorSnapshotStats.lastIntentWithoutTransformCellKey = runtimeCellKey;
                    mRuntimeActorSnapshotStats.lastIntentWithoutTransformRefNum = runtimeActor.refNum;
                    mRuntimeActorSnapshotStats.lastIntentWithoutTransformMpNum = runtimeActor.mpNum;
                }
                BaseActor visualActor = runtimeActor;
                switch (applyActualRuntimeMovementDirection(visualActor, previousActor))
                {
                    case RuntimeMovementDirectionAdjustment::Cleared:
                        ++mRuntimeActorSnapshotStats.visualDirectionClearedCount;
                        break;
                    case RuntimeMovementDirectionAdjustment::Derived:
                        ++mRuntimeActorSnapshotStats.visualDirectionDerivedCount;
                        break;
                    case RuntimeMovementDirectionAdjustment::Unchanged:
                        break;
                }
                if (actorInteractionLocked)
                {
                    mActorWanderStates.erase(actorKey);
                    mActorPathgridRouteStates.erase(actorKey);
                    mRuntimeActorMovementStates.erase(actorKey);
                    if (cachedActor != nullptr)
                    {
                        visualActor = *cachedActor;
                        visualActor.cell = runtimeList.cell;
                        visualActor.refId = cachedActor->refId.empty() ? runtimeActor.refId : cachedActor->refId;
                    }
                    visualActor.direction = zeroPosition();
                    visualActor.hasPositionData = visualActor.hasPositionData || runtimeActor.hasPositionData;
                }
                if (!actorInteractionLocked && useRuntimeFallbackMovement && cachedActor != nullptr)
                {
                    cachedActor->direction = runtimeActor.direction;
                    sanitizeFinitePosition(cachedActor->direction);
                }

                const bool sendInteractionStopSnapshot = actorInteractionLocked && visualActor.hasPositionData
                    && (cachedActor == nullptr || hasMovementIntent(cachedActor->direction)
                        || hasMeaningfulRuntimeTransformDelta(runtimeActor, visualActor)
                        || hasMeaningfulMovementIntent(runtimeActor.direction));
                const bool sendRuntimePositionSnapshot = runtimeActor.hasPositionData && !useRuntimeFallbackMovement
                    && !actorInteractionLocked
                    && shouldSendRuntimePositionSnapshot(previousActor, visualActor);
                if (sendRuntimePositionSnapshot || sendInteractionStopSnapshot)
                {
                    BaseActor positionActor = visualActor;
                    positionActor.hasPositionData = true;
                    positionActor.positionSequence = nextPositionSequence;
                    positionActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                    positionActor.movementLatencySeconds = 0.f;
                    positionList.baseActors.push_back(std::move(positionActor));
                }
                else if (runtimeActor.hasPositionData && !useRuntimeFallbackMovement)
                    ++mRuntimeActorSnapshotStats.redundantPositionSnapshotSuppressedCount;

                const bool sendRuntimeAnimFlagsSnapshot
                    = !actorInteractionLocked && shouldSendRuntimeAnimFlagsSnapshot(previousActor, visualActor);
                if (sendRuntimeAnimFlagsSnapshot)
                {
                    BaseActor animFlagsActor = visualActor;
                    animFlagsActor.hasAnimFlagsData = true;
                    animFlagsActor.animFlagsSequence = cachedActor != nullptr && cachedActor->hasAnimFlagsData
                        ? cachedActor->animFlagsSequence + 1
                        : 1;
                    if (animFlagsActor.hasPositionData)
                    {
                        if (useRuntimeFallbackMovement || !sendRuntimePositionSnapshot)
                            animFlagsActor.hasPositionData = false;
                        else
                        {
                            animFlagsActor.positionSequence = nextPositionSequence;
                            animFlagsActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                            animFlagsActor.movementLatencySeconds = 0.f;
                        }
                    }
                    animFlagsList.baseActors.push_back(std::move(animFlagsActor));
                }
                else if (runtimeActor.hasAnimFlagsData)
                    ++mRuntimeActorSnapshotStats.redundantAnimFlagsSnapshotSuppressedCount;

                if (runtimeActor.hasStatsDynamicData)
                {
                    BaseActor statsActor = runtimeActor;
                    statsActor.hasStatsDynamicData = true;
                    if (cachedActor != nullptr && cachedActor->hasStatsDynamicData
                        && mwmp::hasFiniteActorDynamicStats(*cachedActor)
                        && mwmp::hasFiniteActorDynamicStats(statsActor))
                    {
                        for (int statIndex = 0; statIndex < 3; ++statIndex)
                        {
                            ESM::StatState<float>& incomingStat = statsActor.creatureStats.mDynamic[statIndex];
                            const ESM::StatState<float>& cachedStat = cachedActor->creatureStats.mDynamic[statIndex];
                            incomingStat.mCurrent = std::min(incomingStat.mCurrent, cachedStat.mCurrent);
                        }

                        if (cachedActor->creatureStats.mDead
                            || cachedActor->creatureStats.mDynamic[0].mCurrent <= healthDeadEpsilon)
                        {
                            statsActor.creatureStats.mDead = true;
                            statsActor.creatureStats.mDeathAnimationFinished
                                = cachedActor->creatureStats.mDeathAnimationFinished
                                || statsActor.creatureStats.mDeathAnimationFinished;
                        }
                    }
                    statsActor.statsDynamicSequence = cachedActor != nullptr && cachedActor->hasStatsDynamicData
                        ? cachedActor->statsDynamicSequence + 1
                        : 1;
                    statsList.baseActors.push_back(std::move(statsActor));
                }

                if (runtimeActor.hasEquipmentData && hasValidActorEquipment(runtimeActor)
                    && (previousActor == nullptr || !previousActor->hasEquipmentData
                        || !sameActorEquipment(*previousActor, runtimeActor)))
                {
                    BaseActor equipmentActor = runtimeActor;
                    equipmentActor.hasEquipmentData = true;
                    equipmentActor.equipmentSequence = previousActor != nullptr && previousActor->hasEquipmentData
                        ? previousActor->equipmentSequence + 1
                        : 1;
                    equipmentList.baseActors.push_back(std::move(equipmentActor));
                }

                if (runtimeOwnsClientRenderedAi)
                {
                    const bool hasPresentedAi = mRuntimeClientAiPresentedActors.find(actorKey)
                        != mRuntimeClientAiPresentedActors.end();
                    const bool shouldPresentCombatAi = !actorInteractionLocked && cachedActor != nullptr
                        && actorHasServerCombatTarget(*cachedActor) && hasValidAiTarget(*serverCell, *cachedActor);
                    const bool shouldPresentCancelAi = !actorInteractionLocked && cachedActor != nullptr
                        && cachedActor->hasAiData && !shouldPresentCombatAi
                        && cachedActor->aiAction != BaseActorList::CANCEL;
                    const bool authoritativeAiChanged = cachedActor != nullptr
                        && (previousActor == nullptr || !sameActorAi(*previousActor, *cachedActor));
                    if ((shouldPresentCombatAi || shouldPresentCancelAi) && (!hasPresentedAi || authoritativeAiChanged))
                    {
                        BaseActor aiActor = visualActor;
                        aiActor.hasAiData = true;
                        aiActor.hasPositionData = false;
                        aiActor.aiCoordinates = zeroPosition();
                        if (shouldPresentCombatAi)
                        {
                            aiActor.hasAiTarget = true;
                            aiActor.aiTarget = cachedActor->aiTarget;
                            aiActor.aiAction = BaseActorList::COMBAT;
                            aiActor.aiDistance = cachedActor->aiDistance;
                            aiActor.aiDuration = cachedActor->aiDuration;
                            aiActor.aiShouldRepeat = cachedActor->aiShouldRepeat;
                        }
                        else
                        {
                            aiActor.hasAiTarget = false;
                            aiActor.aiTarget = Target();
                            aiActor.aiAction = BaseActorList::CANCEL;
                            aiActor.aiDistance = 0;
                            aiActor.aiDuration = 0;
                            aiActor.aiShouldRepeat = false;
                        }
                        aiActor.direction = zeroPosition();
                        aiList.baseActors.push_back(std::move(aiActor));
                        mRuntimeClientAiPresentedActors.insert(actorKey);
                    }
                    else if (actorInteractionLocked || cachedActor == nullptr || !cachedActor->hasAiData
                        || cachedActor->aiAction == BaseActorList::CANCEL)
                        mRuntimeClientAiPresentedActors.erase(actorKey);
                }
                else
                {
                    mRuntimeClientAiPresentedActors.erase(actorKey);
                    if (!actorInteractionLocked && runtimeActor.hasAiData && cachedActor != nullptr
                        && !(actorHasServerCombatTarget(*cachedActor) && hasValidAiTarget(*serverCell, *cachedActor)
                            && runtimeActor.aiAction != BaseActorList::COMBAT)
                        && hasValidActorAiSnapshot(*serverCell, runtimeActor))
                    {
                        BaseActor aiActor = buildServerAcceptedAiActor(*serverCell, runtimeActor, *cachedActor);
                        if (previousActor == nullptr || !sameActorAi(*previousActor, aiActor))
                            aiList.baseActors.push_back(std::move(aiActor));
                    }
                }

                bool sentServerAttackSnapshot = false;
                if (!actorInteractionLocked && cachedActor != nullptr && visualActor.hasPositionData
                    && actorHasServerCombatTarget(*cachedActor))
                {
                    BaseActor serverActor = *cachedActor;
                    serverActor.position = visualActor.position;
                    serverActor.hasPositionData = true;
                    serverActor.refId = cachedActor->refId.empty() ? runtimeActor.refId : cachedActor->refId;
                    sentServerAttackSnapshot = applyServerActorMeleeIfReady(*serverCell, serverActor, actorKey,
                        visualActor.position, nextPositionSequence, sampleIntervalSeconds, now, attackList,
                        runtimeActor.hasCombatData ? &runtimeActor.attack : nullptr);
                }
                else
                    mServerActorCombatStates.erase(actorKey);

                if (!sentServerAttackSnapshot && !actorInteractionLocked && visualActor.hasPositionData
                    && shouldSendRuntimeAttackSnapshot(previousActor, runtimeActor))
                {
                    BaseActor attackActor = visualActor;
                    attackActor.hasCombatData = true;
                    attackActor.combatSequence = cachedActor != nullptr && cachedActor->hasCombatData
                        ? cachedActor->combatSequence + 1
                        : 1;
                    attackActor.attack = runtimeActor.attack;
                    attackActor.attack.isHit = false;
                    attackActor.attack.success = false;
                    attackActor.attack.damage = 0.f;
                    attackActor.attack.block = false;
                    attackActor.hasPositionData = true;
                    attackActor.positionSequence = nextPositionSequence;
                    attackActor.movementSampleIntervalSeconds = sampleIntervalSeconds;
                    attackActor.movementLatencySeconds = 0.f;
                    attackList.baseActors.push_back(std::move(attackActor));
                }
            }

            positionList.count = static_cast<unsigned int>(positionList.baseActors.size());
            if (positionList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_POSITION, &positionList);
                positionPacket->setActorList(&positionList);
                serverCell->sendToLoaded(positionPacket, &positionList);
            }

            animFlagsList.count = static_cast<unsigned int>(animFlagsList.baseActors.size());
            if (animFlagsList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_ANIM_FLAGS, &animFlagsList);
                animFlagsPacket->setActorList(&animFlagsList);
                serverCell->sendToLoaded(animFlagsPacket, &animFlagsList);
            }

            statsList.count = static_cast<unsigned int>(statsList.baseActors.size());
            if (statsList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_STATS_DYNAMIC, &statsList);
                statsPacket->setActorList(&statsList);
                serverCell->sendToLoaded(statsPacket, &statsList);
            }

            equipmentList.count = static_cast<unsigned int>(equipmentList.baseActors.size());
            if (equipmentList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_EQUIPMENT, &equipmentList);
                equipmentPacket->setActorList(&equipmentList);
                serverCell->sendToLoaded(equipmentPacket, &equipmentList);
            }

            aiList.count = static_cast<unsigned int>(aiList.baseActors.size());
            if (aiList.count != 0)
            {
                if (!runtimeOwnsClientRenderedAi)
                    serverCell->readActorList(ID_ACTOR_AI, &aiList);
                aiPacket->setActorList(&aiList);
                serverCell->sendToLoaded(aiPacket, &aiList);
            }

            attackList.count = static_cast<unsigned int>(attackList.baseActors.size());
            if (attackList.count != 0)
            {
                serverCell->readActorList(ID_ACTOR_ATTACK, &attackList);
                attackPacket->setActorList(&attackList);
                serverCell->sendToLoaded(attackPacket, &attackList);
            }
        }
    }

    void ServerSimulation::applyRuntimePlayerSnapshots(const std::vector<SimulationPlayerSnapshot>& playerSnapshots)
    {
        if (playerSnapshots.empty())
            return;

        const Clock::time_point now = Clock::now();
        for (const SimulationPlayerSnapshot& snapshot : playerSnapshots)
        {
            if (!mwmp::isPacketGuidAssigned(snapshot.guid) || !snapshot.hasStatsDynamicData
                || !hasFiniteSimpleCreatureStats(snapshot.creatureStats))
                continue;

            Player* target = Players::getPlayer(snapshot.guid);
            if (target == nullptr || target->getLoadState() == Player::KICKED || !target->hasFiniteDynamicStats())
                continue;

            if (!isSameSimulationCell(target->cell, snapshot.cell))
                continue;

            const float currentHealth = target->creatureStats.mDynamic[0].mCurrent;
            const float incomingHealth = snapshot.creatureStats.mDynamic[0].mCurrent;
            const bool wasDead = target->creatureStats.mDead
                || currentHealth <= healthDeadEpsilon;
            const bool snapshotDead = snapshot.creatureStats.mDead
                || incomingHealth <= healthDeadEpsilon;
            if (shouldSuppressTransientPlayerDeath(*target, snapshot.cell, wasDead, currentHealth, snapshotDead,
                    incomingHealth, now, "OpenMW runtime"))
                continue;

            bool changed = false;
            std::vector<uint8_t> changedIndexes;

            const auto addChangedIndex = [&](uint8_t index) {
                if (std::find(changedIndexes.begin(), changedIndexes.end(), index) == changedIndexes.end())
                    changedIndexes.push_back(index);
            };

            for (uint8_t statIndex = 0; statIndex < 3; ++statIndex)
            {
                float incomingCurrent = snapshot.creatureStats.mDynamic[statIndex].mCurrent;
                if (statIndex == 0)
                    incomingCurrent = std::max(0.f, incomingCurrent);

                float& targetCurrent = target->creatureStats.mDynamic[statIndex].mCurrent;
                if (incomingCurrent + healthDeadEpsilon < targetCurrent)
                {
                    targetCurrent = incomingCurrent;
                    addChangedIndex(statIndex);
                    changed = true;
                }
            }

            if (snapshotDead && target->creatureStats.mDynamic[0].mCurrent <= healthDeadEpsilon)
            {
                if (target->creatureStats.mDynamic[0].mCurrent != 0.f)
                {
                    target->creatureStats.mDynamic[0].mCurrent = 0.f;
                    addChangedIndex(0);
                    changed = true;
                }
                if (!target->creatureStats.mDead)
                {
                    target->creatureStats.mDead = true;
                    addChangedIndex(0);
                    changed = true;
                }
                if (snapshot.creatureStats.mDeathAnimationFinished
                    && !target->creatureStats.mDeathAnimationFinished)
                {
                    target->creatureStats.mDeathAnimationFinished = true;
                    addChangedIndex(0);
                    changed = true;
                }
            }

            if (target->creatureStats.mDynamic[2].mCurrent <= 0.f)
                target->creatureStats.mKnockdown = true;

            if (!changed || changedIndexes.empty())
                continue;

            const bool becameDead = !wasDead && target->creatureStats.mDead;
            ++target->statsDynamicSequence;
            target->exchangeFullInfo = false;
            target->statsDynamicIndexChanges = std::move(changedIndexes);
            target->acceptCurrentStatsDynamicPacket();

            broadcastPlayerStats(*target);
            notifyPlayerStatsDynamic(*target);
            if (becameDead)
                notifyPlayerDeath(*target);
        }
    }

    void ServerSimulation::logRuntimeActorMovementHealthIfNeeded(Clock::time_point now)
    {
        const std::uint64_t currentIntentWithoutTransform
            = mRuntimeActorSnapshotStats.rawMovementIntentWithoutTransformCount;
        if (currentIntentWithoutTransform == mLastLoggedRawMovementIntentWithoutTransformCount)
            return;

        if (mLastRuntimeActorMovementHealthLog != Clock::time_point()
            && now - mLastRuntimeActorMovementHealthLog < runtimeActorMovementHealthLogInterval)
            return;

        const std::uint64_t newIntentWithoutTransform
            = currentIntentWithoutTransform - mLastLoggedRawMovementIntentWithoutTransformCount;
        mLastLoggedRawMovementIntentWithoutTransformCount = currentIntentWithoutTransform;
        mLastRuntimeActorMovementHealthLog = now;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "OpenMW runtime actor movement health: %llu new intent-without-transform sample(s), cumulative=%llu, "
            "rawIntent=%llu, transformDelta=%llu, lastActor=%s %u-%u",
            static_cast<unsigned long long>(newIntentWithoutTransform),
            static_cast<unsigned long long>(currentIntentWithoutTransform),
            static_cast<unsigned long long>(mRuntimeActorSnapshotStats.rawMovementIntentSnapshotCount),
            static_cast<unsigned long long>(mRuntimeActorSnapshotStats.transformDeltaSnapshotCount),
            mRuntimeActorSnapshotStats.lastIntentWithoutTransformCellKey.c_str(),
            mRuntimeActorSnapshotStats.lastIntentWithoutTransformRefNum,
            mRuntimeActorSnapshotStats.lastIntentWithoutTransformMpNum);
    }

    bool ServerSimulation::isActorInteractionLocked(const ActorMovementKey& actorKey, Clock::time_point now)
    {
        auto leaseIt = mActorInteractionLeases.find(actorKey);
        if (leaseIt == mActorInteractionLeases.end())
            return false;

        if (leaseIt->second.expiresAt <= now || Players::getPlayer(leaseIt->second.playerGuid) == nullptr)
        {
            mActorInteractionLeases.erase(leaseIt);
            return false;
        }

        return true;
    }

    void ServerSimulation::stopActorForInteraction(Cell& cell, BaseActor& actor, const ActorMovementKey& actorKey)
    {
        if (!actor.hasPositionData)
            return;

        mActorWanderStates.erase(actorKey);
        mActorPathgridRouteStates.erase(actorKey);
        mRuntimeActorMovementStates.erase(actorKey);

        if (!hasMovementIntent(actor.direction))
            return;

        actor.direction = zeroPosition();
        actor.movementSampleIntervalSeconds = mwmp::defaultMovementSampleIntervalSeconds;
        actor.movementLatencySeconds = 0.f;
        ++actor.positionSequence;
        actor.hasPositionData = true;

        BaseActorList stopList;
        stopList.cell = cell.getCellData();
        stopList.guid = unassignedPacketGuid();
        stopList.action = BaseActorList::SET;
        stopList.isValid = true;
        stopList.baseActors.push_back(actor);
        stopList.count = static_cast<unsigned int>(stopList.baseActors.size());

        ActorPacket* positionPacket = ServerNetworking::get().getActorPacketController()->GetPacket(ID_ACTOR_POSITION);
        if (positionPacket == nullptr)
            return;

        cell.readActorList(ID_ACTOR_POSITION, &stopList);
        positionPacket->setActorList(&stopList);
        cell.sendToLoaded(positionPacket, &stopList);
    }

    bool ServerSimulation::shouldUseRuntimeFallbackMovement(
        const ActorMovementKey& actorKey, const BaseActor& runtimeActor, const BaseActor* cachedActor)
    {
        if (mRuntime != nullptr && mRuntime->hasOpenMwWorld())
        {
            mRuntimeActorMovementStates.erase(actorKey);
            return false;
        }

        const bool hasImportedServerMovement = cachedActor != nullptr && hasServerOwnedActorMovement(*cachedActor);
        const bool hasRuntimeMovementIntent = hasMovementIntent(runtimeActor.direction);
        if (!runtimeActor.hasPositionData || !isFiniteActorMovementSnapshot(runtimeActor)
            || cachedActor == nullptr || (!hasImportedServerMovement && !hasRuntimeMovementIntent))
        {
            mRuntimeActorMovementStates.erase(actorKey);
            return false;
        }

        RuntimeActorMovementState& state = mRuntimeActorMovementStates[actorKey];
        const bool wasUsingFallback = state.useFallbackMovement;

        if (!state.hasRuntimePosition)
        {
            state.lastRuntimePosition = runtimeActor.position;
            state.stagnantSnapshotCount = 0;
            state.hasRuntimePosition = true;
            state.useFallbackMovement = false;
            return false;
        }

        const float runtimeDeltaSquared = squaredDistance(runtimeActor.position, state.lastRuntimePosition);
        if (std::isfinite(runtimeDeltaSquared) && runtimeDeltaSquared > runtimeActorMovementEpsilonSquared)
        {
            state.lastRuntimePosition = runtimeActor.position;
            state.stagnantSnapshotCount = 0;
            state.useFallbackMovement = false;

            if (wasUsingFallback)
            {
                ++mRuntimeActorSnapshotStats.fallbackMovementResumeCount;
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                    "Runtime actor movement resumed for %u-%u in %s; disabling server pathgrid fallback",
                    runtimeActor.refNum, runtimeActor.mpNum, actorKey.cellKey.c_str());
            }

            return false;
        }

        state.lastRuntimePosition = runtimeActor.position;
        if (state.stagnantSnapshotCount < runtimeActorFallbackSnapshotThreshold)
            ++state.stagnantSnapshotCount;

        state.useFallbackMovement = state.stagnantSnapshotCount >= runtimeActorFallbackSnapshotThreshold;
        if (state.useFallbackMovement && !wasUsingFallback)
        {
            ++mRuntimeActorSnapshotStats.fallbackMovementActivationCount;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Runtime actor %u-%u in %s is position-stagnant; enabling server pathgrid movement fallback",
                runtimeActor.refNum, runtimeActor.mpNum, actorKey.cellKey.c_str());
        }

        if (state.useFallbackMovement)
        {
            ++mRuntimeActorSnapshotStats.fallbackMovementSuppressedSnapshotCount;
            mRuntimeActorSnapshotStats.lastFallbackMovementCellKey = actorKey.cellKey;
            mRuntimeActorSnapshotStats.lastFallbackMovementRefNum = runtimeActor.refNum;
            mRuntimeActorSnapshotStats.lastFallbackMovementMpNum = runtimeActor.mpNum;
        }

        return state.useFallbackMovement;
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
        const bool serverActorAuthority = canAuthoritativelySimulateActors();
        const Cell::ServerWorldBootstrapStats* serverWorldStats = nullptr;
        if (serverCell != nullptr && serverCell->hasServerWorldStateBootstrap())
            serverWorldStats = &serverCell->getServerWorldBootstrapStats();

        std::string payload;
        payload.reserve(520 + cellDescription.size() + cellKey.size() + serverCellKey.size());
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
        payload += ",\"serverWorldBootstrapped\":";
        payload += jsonBool(serverWorldStats != nullptr);
        payload += ",\"serverWorldCellKey\":";
        payload += jsonString(serverWorldStats != nullptr ? serverWorldStats->cellKey : std::string{});
        payload += ",\"serverWorldReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->referenceCount : 0);
        payload += ",\"serverWorldActorReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorCount : 0);
        payload += ",\"serverWorldActorAiReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiCount : 0);
        payload += ",\"serverWorldActorAiPackageListCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiPackageListCount : 0);
        payload += ",\"serverWorldActorAiPackageItemCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiPackageItemCount : 0);
        payload += ",\"serverWorldActorAiTargetCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiTargetCount : 0);
        payload += ",\"serverWorldActorAiTargetResolvedCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiTargetResolvedCount : 0);
        payload += ",\"serverWorldActorAiTargetUnresolvedCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiTargetUnresolvedCount : 0);
        payload += ",\"serverWorldActorAiRoutePlanCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiRoutePlanCount : 0);
        payload += ",\"serverWorldActorAiRouteReachableCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiRouteReachableCount : 0);
        payload += ",\"serverWorldActorAiRouteUnreachableCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiRouteUnreachableCount : 0);
        payload += ",\"serverWorldActorAiRouteWaypointCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorAiRouteWaypointCount : 0);
        payload += ",\"serverWorldActorProfileReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorProfileCount : 0);
        payload += ",\"serverWorldActorProfileNpcCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorProfileNpcCount : 0);
        payload += ",\"serverWorldActorProfileCreatureCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorProfileCreatureCount : 0);
        payload += ",\"serverWorldActorProfileAutocalcNpcCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorProfileAutocalcNpcCount : 0);
        payload += ",\"serverWorldActorInventoryReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorInventoryCount : 0);
        payload += ",\"serverWorldActorInventoryItemCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorInventoryItemCount : 0);
        payload += ",\"serverWorldActorSpellbookReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorSpellbookCount : 0);
        payload += ",\"serverWorldActorSpellbookSpellCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorSpellbookSpellCount : 0);
        payload += ",\"serverWorldActorStatsDynamicReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorStatsDynamicCount : 0);
        payload += ",\"serverWorldActorStatsDynamicItemCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorStatsDynamicItemCount : 0);
        payload += ",\"serverWorldActorStatsDynamicAutocalcCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorStatsDynamicAutocalcCount : 0);
        payload += ",\"serverWorldActorEquipmentReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorEquipmentCount : 0);
        payload += ",\"serverWorldActorEquipmentItemCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->actorEquipmentItemCount : 0);
        payload += ",\"serverWorldPathgridAvailable\":";
        payload += jsonBool(serverWorldStats != nullptr && serverWorldStats->pathgridAvailable);
        payload += ",\"serverWorldPathgridPointCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridPointCount : 0);
        payload += ",\"serverWorldPathgridEdgeCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridEdgeCount : 0);
        payload += ",\"serverWorldPathgridUsableDirectedEdgeCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridUsableDirectedEdgeCount : 0);
        payload += ",\"serverWorldPathgridInvalidEdgeCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridInvalidEdgeCount : 0);
        payload += ",\"serverWorldPathgridConnectedComponentCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridConnectedComponentCount : 0);
        payload += ",\"serverWorldPathgridLargestConnectedComponentSize\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->pathgridLargestConnectedComponentSize : 0);
        payload += ",\"serverWorldObjectReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->objectCount : 0);
        payload += ",\"serverWorldActorListSeeded\":";
        payload += jsonBool(serverCell != nullptr && serverCell->hasServerWorldSeededActorList());
        payload += ",\"serverWorldSeededActorCount\":";
        payload += std::to_string(serverCell != nullptr ? serverCell->getServerWorldSeededActorCount() : 0);
        payload += ",\"serverWorldObjectListSeeded\":";
        payload += jsonBool(serverCell != nullptr && serverCell->hasServerWorldSeededObjectList());
        payload += ",\"serverWorldSeededObjectCount\":";
        payload += std::to_string(serverCell != nullptr ? serverCell->getServerWorldSeededObjectCount() : 0);
        payload += ",\"serverWorldContainerReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->containerCount : 0);
        payload += ",\"serverWorldDoorReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->doorCount : 0);
        payload += ",\"serverWorldUnresolvedReferenceCount\":";
        payload += std::to_string(serverWorldStats != nullptr ? serverWorldStats->unresolvedCount : 0);
        payload += ",\"authorityMode\":";
        payload += jsonString(cellSimulationAuthorityMode(serverActorAuthority));
        payload += ",\"simulationOwner\":";
        payload += jsonString(cellSimulationOwner(serverActorAuthority));
        payload += ",\"serverOwnsSimulation\":";
        payload += jsonBool(serverActorAuthority);
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(serverActorAuthority);
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
        const SimulationRuntimeWorldState& runtimeWorldState = runtime().worldState();
        const SimulationRuntimeFocusState& runtimeFocusState = runtime().focusState();
        const bool serverActorAuthority = canAuthoritativelySimulateActors();
        const std::string authorityBlockReason = runtimeAuthorityBlockReason(runtime());
        const std::string wholeGameBlockReason = runtimeWholeGameBlockReason(runtime());
        const ServerContentRegistryStatistics serverContent = ServerContentRegistry::get().statistics();
        const ServerContentDatabaseStatistics serverContentDatabase = ServerContentDatabase::get().statistics();
        WorldDatabaseStore::get().ensureLoaded();
        const WorldDatabaseStatistics worldDatabase = WorldDatabaseStore::get().statistics();
        const QuestDatabaseStatistics questDatabase = QuestDatabaseStore::get().statistics();
        const QuestEventJournalStatistics questEventJournal = QuestEventJournalStore::get().statistics();
        const CellController* cellController = CellController::get();
        std::size_t loadedCellCount = 0;
        std::size_t serverWorldBootstrappedCellCount = 0;
        std::size_t serverWorldReferenceCount = 0;
        std::size_t serverWorldActorReferenceCount = 0;
        std::size_t serverWorldActorAiReferenceCount = 0;
        std::size_t serverWorldActorAiPackageListCount = 0;
        std::size_t serverWorldActorAiPackageItemCount = 0;
        std::size_t serverWorldActorAiTargetCount = 0;
        std::size_t serverWorldActorAiTargetResolvedCount = 0;
        std::size_t serverWorldActorAiTargetUnresolvedCount = 0;
        std::size_t serverWorldActorAiRoutePlanCount = 0;
        std::size_t serverWorldActorAiRouteReachableCount = 0;
        std::size_t serverWorldActorAiRouteUnreachableCount = 0;
        std::size_t serverWorldActorAiRouteWaypointCount = 0;
        std::size_t serverWorldActorProfileReferenceCount = 0;
        std::size_t serverWorldActorProfileNpcCount = 0;
        std::size_t serverWorldActorProfileCreatureCount = 0;
        std::size_t serverWorldActorProfileAutocalcNpcCount = 0;
        std::size_t serverWorldActorInventoryReferenceCount = 0;
        std::size_t serverWorldActorInventoryItemCount = 0;
        std::size_t serverWorldActorSpellbookReferenceCount = 0;
        std::size_t serverWorldActorSpellbookSpellCount = 0;
        std::size_t serverWorldActorStatsDynamicReferenceCount = 0;
        std::size_t serverWorldActorStatsDynamicItemCount = 0;
        std::size_t serverWorldActorStatsDynamicAutocalcCount = 0;
        std::size_t serverWorldActorEquipmentReferenceCount = 0;
        std::size_t serverWorldActorEquipmentItemCount = 0;
        std::size_t serverWorldPathgridCellCount = 0;
        std::size_t serverWorldPathgridPointCount = 0;
        std::size_t serverWorldPathgridEdgeCount = 0;
        std::size_t serverWorldPathgridUsableDirectedEdgeCount = 0;
        std::size_t serverWorldPathgridInvalidEdgeCount = 0;
        std::size_t serverWorldPathgridConnectedComponentCount = 0;
        std::size_t serverWorldPathgridLargestConnectedComponentSize = 0;
        std::size_t serverWorldObjectReferenceCount = 0;
        std::size_t serverWorldContainerReferenceCount = 0;
        std::size_t serverWorldDoorReferenceCount = 0;
        std::size_t serverWorldItemReferenceCount = 0;
        std::size_t serverWorldUnresolvedReferenceCount = 0;
        std::size_t serverWorldAmbiguousReferenceCount = 0;
        std::size_t serverWorldActorSeededCellCount = 0;
        std::size_t serverWorldSeededActorCount = 0;
        std::size_t serverWorldObjectSeededCellCount = 0;
        std::size_t serverWorldSeededObjectCount = 0;
        if (cellController != nullptr)
        {
            loadedCellCount = cellController->getCells().size();
            for (const Cell* cell : cellController->getCells())
            {
                if (cell == nullptr || !cell->hasServerWorldStateBootstrap())
                    continue;

                const Cell::ServerWorldBootstrapStats& stats = cell->getServerWorldBootstrapStats();
                ++serverWorldBootstrappedCellCount;
                if (cell->hasServerWorldSeededActorList())
                {
                    ++serverWorldActorSeededCellCount;
                    serverWorldSeededActorCount += cell->getServerWorldSeededActorCount();
                }
                if (cell->hasServerWorldSeededObjectList())
                {
                    ++serverWorldObjectSeededCellCount;
                    serverWorldSeededObjectCount += cell->getServerWorldSeededObjectCount();
                }
                serverWorldReferenceCount += stats.referenceCount;
                serverWorldActorReferenceCount += stats.actorCount;
                serverWorldActorAiReferenceCount += stats.actorAiCount;
                serverWorldActorAiPackageListCount += stats.actorAiPackageListCount;
                serverWorldActorAiPackageItemCount += stats.actorAiPackageItemCount;
                serverWorldActorAiTargetCount += stats.actorAiTargetCount;
                serverWorldActorAiTargetResolvedCount += stats.actorAiTargetResolvedCount;
                serverWorldActorAiTargetUnresolvedCount += stats.actorAiTargetUnresolvedCount;
                serverWorldActorAiRoutePlanCount += stats.actorAiRoutePlanCount;
                serverWorldActorAiRouteReachableCount += stats.actorAiRouteReachableCount;
                serverWorldActorAiRouteUnreachableCount += stats.actorAiRouteUnreachableCount;
                serverWorldActorAiRouteWaypointCount += stats.actorAiRouteWaypointCount;
                serverWorldActorProfileReferenceCount += stats.actorProfileCount;
                serverWorldActorProfileNpcCount += stats.actorProfileNpcCount;
                serverWorldActorProfileCreatureCount += stats.actorProfileCreatureCount;
                serverWorldActorProfileAutocalcNpcCount += stats.actorProfileAutocalcNpcCount;
                serverWorldActorInventoryReferenceCount += stats.actorInventoryCount;
                serverWorldActorInventoryItemCount += stats.actorInventoryItemCount;
                serverWorldActorSpellbookReferenceCount += stats.actorSpellbookCount;
                serverWorldActorSpellbookSpellCount += stats.actorSpellbookSpellCount;
                serverWorldActorStatsDynamicReferenceCount += stats.actorStatsDynamicCount;
                serverWorldActorStatsDynamicItemCount += stats.actorStatsDynamicItemCount;
                serverWorldActorStatsDynamicAutocalcCount += stats.actorStatsDynamicAutocalcCount;
                serverWorldActorEquipmentReferenceCount += stats.actorEquipmentCount;
                serverWorldActorEquipmentItemCount += stats.actorEquipmentItemCount;
                if (stats.pathgridAvailable)
                    ++serverWorldPathgridCellCount;
                serverWorldPathgridPointCount += stats.pathgridPointCount;
                serverWorldPathgridEdgeCount += stats.pathgridEdgeCount;
                serverWorldPathgridUsableDirectedEdgeCount += stats.pathgridUsableDirectedEdgeCount;
                serverWorldPathgridInvalidEdgeCount += stats.pathgridInvalidEdgeCount;
                serverWorldPathgridConnectedComponentCount += stats.pathgridConnectedComponentCount;
                serverWorldPathgridLargestConnectedComponentSize = std::max(
                    serverWorldPathgridLargestConnectedComponentSize, stats.pathgridLargestConnectedComponentSize);
                serverWorldObjectReferenceCount += stats.objectCount;
                serverWorldContainerReferenceCount += stats.containerCount;
                serverWorldDoorReferenceCount += stats.doorCount;
                serverWorldItemReferenceCount += stats.itemCount;
                serverWorldUnresolvedReferenceCount += stats.unresolvedCount;
                serverWorldAmbiguousReferenceCount += stats.ambiguousCount;
            }
        }

        std::size_t playerTrackedCellCount = 0;
        std::size_t serverSimulationTrackedCellCount = 0;
        std::size_t serverSimulationIdentifiedCellCount = 0;
        std::size_t serverSimulationVisitorReferenceCount = 0;
        for (const auto& [cellDescription, state] : mShadowCellAuthority)
        {
            static_cast<void>(cellDescription);
            if (containsGuid(state.visitors, player.guid))
                ++playerTrackedCellCount;
            if (!state.visitors.empty())
            {
                ++serverSimulationTrackedCellCount;
                if (state.hasCell)
                    ++serverSimulationIdentifiedCellCount;
                serverSimulationVisitorReferenceCount += state.visitors.size();
            }
        }

        std::size_t serverActorPathgridRouteCacheCount = mActorPathgridRouteStates.size();
        std::size_t serverActorPathgridBlockedRouteCacheCount = 0;
        std::size_t serverActorPathgridRouteWaypointCacheCount = 0;
        for (const auto& [actorKey, routeState] : mActorPathgridRouteStates)
        {
            static_cast<void>(actorKey);
            if (routeState.routeBlocked)
                ++serverActorPathgridBlockedRouteCacheCount;
            serverActorPathgridRouteWaypointCacheCount += routeState.waypoints.size();
        }

        std::size_t runtimeActorFallbackMovementActorCount = 0;
        for (const auto& [actorKey, movementState] : mRuntimeActorMovementStates)
        {
            static_cast<void>(actorKey);
            if (movementState.useFallbackMovement)
                ++runtimeActorFallbackMovementActorCount;
        }

        std::string payload;
        payload.reserve(1100);
        payload += "{\"schema\":";
        payload += std::to_string(mwmp::clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"runtime_status\"";
        payload += ",\"runtimeRequested\":";
        payload += jsonString(runtime().requestedName());
        payload += ",\"runtimeActive\":";
        payload += jsonString(runtime().activeName());
        payload += ",\"openmwWorld\":";
        payload += jsonBool(runtime().hasOpenMwWorld());
        payload += ",\"persistentOpenMwWorld\":";
        payload += jsonBool(runtime().hasPersistentWorld());
        payload += ",\"openMwWorldPrepared\":";
        payload += jsonBool(runtimeWorldState.prepared);
        payload += ",\"openMwWorldLoadedFromSave\":";
        payload += jsonBool(runtimeWorldState.loadedFromSave);
        payload += ",\"openMwWorldInitializedNew\":";
        payload += jsonBool(runtimeWorldState.initializedNewWorld);
        payload += ",\"openMwWorldSavePath\":";
        payload += jsonString(runtimeWorldState.savePath);
        payload += ",\"openMwWorldManifestPath\":";
        payload += jsonString(runtimeWorldState.manifestPath);
        payload += ",\"canSimulateActors\":";
        payload += jsonBool(runtime().canSimulateActors());
        payload += ",\"cellAuthorityMode\":";
        payload += jsonString(cellSimulationAuthorityMode(serverActorAuthority));
        payload += ",\"simulationOwner\":";
        payload += jsonString(cellSimulationOwner(serverActorAuthority));
        payload += ",\"serverOwnsSimulation\":";
        payload += jsonBool(serverActorAuthority);
        payload += ",\"serverActorAuthority\":";
        payload += jsonBool(serverActorAuthority);
        payload += ",\"serverActorAuthorityBlockedBy\":";
        payload += jsonString(authorityBlockReason);
        payload += ",\"serverWholeGameReady\":";
        payload += jsonBool(wholeGameBlockReason.empty());
        payload += ",\"serverWholeGameBlockedBy\":";
        payload += jsonString(wholeGameBlockReason);
        payload += ",\"openMwRuntimeUsesSinglePlayerProxy\":";
        payload += jsonBool(runtimeTopology.usesSinglePlayerProxy);
        payload += ",\"openMwRuntimeHasPersistentPlayerActors\":";
        payload += jsonBool(runtimeTopology.hasPersistentPlayerActors);
        payload += ",\"serverActorPathgridRouteCacheCount\":";
        payload += std::to_string(serverActorPathgridRouteCacheCount);
        payload += ",\"serverActorPathgridBlockedRouteCacheCount\":";
        payload += std::to_string(serverActorPathgridBlockedRouteCacheCount);
        payload += ",\"serverActorPathgridRouteWaypointCacheCount\":";
        payload += std::to_string(serverActorPathgridRouteWaypointCacheCount);
        payload += ",\"loadedCellCount\":";
        payload += std::to_string(loadedCellCount);
        payload += ",\"serverSimulationTrackedCellCount\":";
        payload += std::to_string(serverSimulationTrackedCellCount);
        payload += ",\"serverSimulationIdentifiedCellCount\":";
        payload += std::to_string(serverSimulationIdentifiedCellCount);
        payload += ",\"serverSimulationVisitorReferenceCount\":";
        payload += std::to_string(serverSimulationVisitorReferenceCount);
        payload += ",\"playerTrackedCellCount\":";
        payload += std::to_string(playerTrackedCellCount);
        payload += ",\"openMwRuntimeFocusCellCount\":";
        payload += std::to_string(runtimeFocusState.configuredCellCount);
        payload += ",\"openMwRuntimePlayerFocusCount\":";
        payload += std::to_string(runtimeFocusState.configuredPlayerCount);
        payload += ",\"openMwRuntimePersistentPlayerActorCount\":";
        payload += std::to_string(runtimeFocusState.persistentPlayerActorCount);
        payload += ",\"openMwRuntimePlayerSnapshotCount\":";
        payload += std::to_string(runtimeFocusState.exportedPlayerSnapshotCount);
        payload += ",\"openMwRuntimePersistentPlayerActorSnapshotCount\":";
        payload += std::to_string(runtimeFocusState.persistentPlayerActorSnapshotCount);
        payload += ",\"openMwRuntimeVirtualPlayerSnapshotCount\":";
        payload += std::to_string(runtimeFocusState.virtualPlayerSnapshotCount);
        payload += ",\"openMwRuntimeExportedFocusPlayerSnapshot\":";
        payload += jsonBool(runtimeFocusState.exportedFocusPlayerSnapshot);
        payload += ",\"openMwRuntimeFocusCandidateCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.candidateCellCount);
        payload += ",\"openMwRuntimeDirectFocusCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.directFocusCellCount);
        payload += ",\"openMwRuntimeDirectFocusPlayerCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.directFocusPlayerCount);
        payload += ",\"openMwRuntimeDeferredLoadedCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.deferredLoadedCellCount);
        payload += ",\"openMwRuntimeScriptFocusCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.scriptFocusCellCount);
        payload += ",\"openMwRuntimeScriptFocusWithoutPositionCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.scriptFocusWithoutPositionCellCount);
        payload += ",\"openMwRuntimeAuthorityOnlyCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.authorityOnlyCellCount);
        payload += ",\"openMwRuntimeStaleSimulationInterestCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.staleSimulationInterestCellCount);
        payload += ",\"openMwRuntimeCurrentPlayerCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.currentPlayerCellCount);
        payload += ",\"openMwRuntimeRepairedCurrentPlayerCellCount\":";
        payload += std::to_string(mRuntimeFocusSelectionStats.repairedCurrentPlayerCellCount);
        payload += ",\"openMwRuntimeLastDeferredLoadedCell\":";
        payload += jsonString(mRuntimeFocusSelectionStats.lastDeferredLoadedCellDescription);
        payload += ",\"openMwRuntimeLastAuthorityOnlyCell\":";
        payload += jsonString(mRuntimeFocusSelectionStats.lastAuthorityOnlyCellDescription);
        payload += ",\"openMwRuntimeLastStaleSimulationInterestCell\":";
        payload += jsonString(mRuntimeFocusSelectionStats.lastStaleSimulationInterestCellDescription);
        payload += ",\"openMwRuntimeLastRepairedCurrentPlayerCell\":";
        payload += jsonString(mRuntimeFocusSelectionStats.lastRepairedCurrentPlayerCellDescription);
        payload += ",\"openMwRuntimeFocusAttemptCount\":";
        payload += std::to_string(runtimeFocusState.focusAttemptCount);
        payload += ",\"openMwRuntimeFocusSuccessCount\":";
        payload += std::to_string(runtimeFocusState.focusSuccessCount);
        payload += ",\"openMwRuntimeFocusFailureCount\":";
        payload += std::to_string(runtimeFocusState.focusFailureCount);
        payload += ",\"openMwRuntimeFocusCatchupClampCount\":";
        payload += std::to_string(runtimeFocusState.focusCatchupClampCount);
        payload += ",\"openMwRuntimeLastFocusCell\":";
        payload += jsonString(runtimeFocusState.lastCellDescription);
        payload += ",\"openMwRuntimeLastFocusSimulationDeltaSeconds\":";
        payload += std::to_string(runtimeFocusState.lastSimulationDeltaSeconds);
        payload += ",\"openMwRuntimeLastFocusClockDeltaSeconds\":";
        payload += std::to_string(runtimeFocusState.lastClockDeltaSeconds);
        payload += ",\"openMwRuntimeLastFocusQueuedDeltaSeconds\":";
        payload += std::to_string(runtimeFocusState.lastQueuedDeltaSeconds);
        payload += ",\"openMwRuntimeLastFocusHadPosition\":";
        payload += jsonBool(runtimeFocusState.lastFocusHadPosition);
        payload += ",\"openMwRuntimeLastFocusSucceeded\":";
        payload += jsonBool(runtimeFocusState.lastFocusSucceeded);
        payload += ",\"openMwRuntimeActorSnapshotBatchCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.snapshotBatchCount);
        payload += ",\"openMwRuntimeActorSnapshotCellCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.snapshotCellCount);
        payload += ",\"openMwRuntimeActorSnapshotActorCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.snapshotActorCount);
        payload += ",\"openMwRuntimeLastActorSnapshotCell\":";
        payload += jsonString(mRuntimeActorSnapshotStats.lastSnapshotCellDescription);
        payload += ",\"openMwRuntimeLastActorSnapshotActorCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.lastSnapshotActorCount);
        payload += ",\"openMwRuntimeRejectedClientActorMovementPackets\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rejectedClientActorMovementPackets);
        payload += ",\"openMwRuntimeRejectedClientActorAiPackets\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rejectedClientActorAiPackets);
        payload += ",\"openMwRuntimeRejectedClientActorAttackPackets\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rejectedClientActorAttackPackets);
        payload += ",\"openMwRuntimeRejectedClientActorCastPackets\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rejectedClientActorCastPackets);
        payload += ",\"openMwRuntimeRejectedClientActorAuthorityPackets\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rejectedClientActorAuthorityPackets);
        payload += ",\"openMwRuntimeLastRejectedClientActorPacket\":";
        payload += jsonString(mRuntimeActorSnapshotStats.lastRejectedClientActorPacket);
        payload += ",\"openMwRuntimeLastRejectedClientActorCell\":";
        payload += jsonString(mRuntimeActorSnapshotStats.lastRejectedClientActorCellDescription);
        payload += ",\"openMwRuntimeFallbackMovementActorCount\":";
        payload += std::to_string(runtimeActorFallbackMovementActorCount);
        payload += ",\"openMwRuntimeFallbackMovementActivationCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.fallbackMovementActivationCount);
        payload += ",\"openMwRuntimeFallbackMovementResumeCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.fallbackMovementResumeCount);
        payload += ",\"openMwRuntimeFallbackMovementSuppressedSnapshotCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.fallbackMovementSuppressedSnapshotCount);
        payload += ",\"openMwRuntimeVisualDirectionClearedCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.visualDirectionClearedCount);
        payload += ",\"openMwRuntimeVisualDirectionDerivedCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.visualDirectionDerivedCount);
        payload += ",\"openMwRuntimeRedundantPositionSnapshotSuppressedCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.redundantPositionSnapshotSuppressedCount);
        payload += ",\"openMwRuntimeRedundantAnimFlagsSnapshotSuppressedCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.redundantAnimFlagsSnapshotSuppressedCount);
        payload += ",\"openMwRuntimeRawMovementIntentSnapshotCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rawMovementIntentSnapshotCount);
        payload += ",\"openMwRuntimeTransformDeltaSnapshotCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.transformDeltaSnapshotCount);
        payload += ",\"openMwRuntimeRawMovementIntentWithoutTransformCount\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.rawMovementIntentWithoutTransformCount);
        payload += ",\"openMwRuntimeLastIntentWithoutTransformCellKey\":";
        payload += jsonString(mRuntimeActorSnapshotStats.lastIntentWithoutTransformCellKey);
        payload += ",\"openMwRuntimeLastIntentWithoutTransformRefNum\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.lastIntentWithoutTransformRefNum);
        payload += ",\"openMwRuntimeLastIntentWithoutTransformMpNum\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.lastIntentWithoutTransformMpNum);
        payload += ",\"openMwRuntimeLastFallbackMovementCellKey\":";
        payload += jsonString(mRuntimeActorSnapshotStats.lastFallbackMovementCellKey);
        payload += ",\"openMwRuntimeLastFallbackMovementRefNum\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.lastFallbackMovementRefNum);
        payload += ",\"openMwRuntimeLastFallbackMovementMpNum\":";
        payload += std::to_string(mRuntimeActorSnapshotStats.lastFallbackMovementMpNum);
        payload += ",\"serverWorldBootstrappedCellCount\":";
        payload += std::to_string(serverWorldBootstrappedCellCount);
        payload += ",\"serverWorldReferenceCount\":";
        payload += std::to_string(serverWorldReferenceCount);
        payload += ",\"serverWorldActorReferenceCount\":";
        payload += std::to_string(serverWorldActorReferenceCount);
        payload += ",\"serverWorldActorAiReferenceCount\":";
        payload += std::to_string(serverWorldActorAiReferenceCount);
        payload += ",\"serverWorldActorAiPackageListCount\":";
        payload += std::to_string(serverWorldActorAiPackageListCount);
        payload += ",\"serverWorldActorAiPackageItemCount\":";
        payload += std::to_string(serverWorldActorAiPackageItemCount);
        payload += ",\"serverWorldActorAiTargetCount\":";
        payload += std::to_string(serverWorldActorAiTargetCount);
        payload += ",\"serverWorldActorAiTargetResolvedCount\":";
        payload += std::to_string(serverWorldActorAiTargetResolvedCount);
        payload += ",\"serverWorldActorAiTargetUnresolvedCount\":";
        payload += std::to_string(serverWorldActorAiTargetUnresolvedCount);
        payload += ",\"serverWorldActorAiRoutePlanCount\":";
        payload += std::to_string(serverWorldActorAiRoutePlanCount);
        payload += ",\"serverWorldActorAiRouteReachableCount\":";
        payload += std::to_string(serverWorldActorAiRouteReachableCount);
        payload += ",\"serverWorldActorAiRouteUnreachableCount\":";
        payload += std::to_string(serverWorldActorAiRouteUnreachableCount);
        payload += ",\"serverWorldActorAiRouteWaypointCount\":";
        payload += std::to_string(serverWorldActorAiRouteWaypointCount);
        payload += ",\"serverWorldActorProfileReferenceCount\":";
        payload += std::to_string(serverWorldActorProfileReferenceCount);
        payload += ",\"serverWorldActorProfileNpcCount\":";
        payload += std::to_string(serverWorldActorProfileNpcCount);
        payload += ",\"serverWorldActorProfileCreatureCount\":";
        payload += std::to_string(serverWorldActorProfileCreatureCount);
        payload += ",\"serverWorldActorProfileAutocalcNpcCount\":";
        payload += std::to_string(serverWorldActorProfileAutocalcNpcCount);
        payload += ",\"serverWorldActorInventoryReferenceCount\":";
        payload += std::to_string(serverWorldActorInventoryReferenceCount);
        payload += ",\"serverWorldActorInventoryItemCount\":";
        payload += std::to_string(serverWorldActorInventoryItemCount);
        payload += ",\"serverWorldActorSpellbookReferenceCount\":";
        payload += std::to_string(serverWorldActorSpellbookReferenceCount);
        payload += ",\"serverWorldActorSpellbookSpellCount\":";
        payload += std::to_string(serverWorldActorSpellbookSpellCount);
        payload += ",\"serverWorldActorStatsDynamicReferenceCount\":";
        payload += std::to_string(serverWorldActorStatsDynamicReferenceCount);
        payload += ",\"serverWorldActorStatsDynamicItemCount\":";
        payload += std::to_string(serverWorldActorStatsDynamicItemCount);
        payload += ",\"serverWorldActorStatsDynamicAutocalcCount\":";
        payload += std::to_string(serverWorldActorStatsDynamicAutocalcCount);
        payload += ",\"serverWorldActorEquipmentReferenceCount\":";
        payload += std::to_string(serverWorldActorEquipmentReferenceCount);
        payload += ",\"serverWorldActorEquipmentItemCount\":";
        payload += std::to_string(serverWorldActorEquipmentItemCount);
        payload += ",\"serverWorldPathgridCellCount\":";
        payload += std::to_string(serverWorldPathgridCellCount);
        payload += ",\"serverWorldPathgridPointCount\":";
        payload += std::to_string(serverWorldPathgridPointCount);
        payload += ",\"serverWorldPathgridEdgeCount\":";
        payload += std::to_string(serverWorldPathgridEdgeCount);
        payload += ",\"serverWorldPathgridUsableDirectedEdgeCount\":";
        payload += std::to_string(serverWorldPathgridUsableDirectedEdgeCount);
        payload += ",\"serverWorldPathgridInvalidEdgeCount\":";
        payload += std::to_string(serverWorldPathgridInvalidEdgeCount);
        payload += ",\"serverWorldPathgridConnectedComponentCount\":";
        payload += std::to_string(serverWorldPathgridConnectedComponentCount);
        payload += ",\"serverWorldPathgridLargestConnectedComponentSize\":";
        payload += std::to_string(serverWorldPathgridLargestConnectedComponentSize);
        payload += ",\"serverWorldObjectReferenceCount\":";
        payload += std::to_string(serverWorldObjectReferenceCount);
        payload += ",\"serverWorldActorSeededCellCount\":";
        payload += std::to_string(serverWorldActorSeededCellCount);
        payload += ",\"serverWorldSeededActorCount\":";
        payload += std::to_string(serverWorldSeededActorCount);
        payload += ",\"serverWorldObjectSeededCellCount\":";
        payload += std::to_string(serverWorldObjectSeededCellCount);
        payload += ",\"serverWorldSeededObjectCount\":";
        payload += std::to_string(serverWorldSeededObjectCount);
        payload += ",\"serverWorldContainerReferenceCount\":";
        payload += std::to_string(serverWorldContainerReferenceCount);
        payload += ",\"serverWorldDoorReferenceCount\":";
        payload += std::to_string(serverWorldDoorReferenceCount);
        payload += ",\"serverWorldItemReferenceCount\":";
        payload += std::to_string(serverWorldItemReferenceCount);
        payload += ",\"serverWorldUnresolvedReferenceCount\":";
        payload += std::to_string(serverWorldUnresolvedReferenceCount);
        payload += ",\"serverWorldAmbiguousReferenceCount\":";
        payload += std::to_string(serverWorldAmbiguousReferenceCount);
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
        payload += ",\"loadOrderPath\":";
        payload += jsonString(Files::pathToUnicodeString(serverContent.loadOrderPath));
        payload += ",\"loadOrderSource\":";
        payload += jsonString(serverContent.loadOrderSource);
        payload += ",\"loadOrderAttempted\":";
        payload += jsonBool(serverContent.loadOrderAttempted);
        payload += ",\"loadOrderLoaded\":";
        payload += jsonBool(serverContent.loadOrderLoaded);
        payload += ",\"serverLoadOrderLoaded\":";
        payload += jsonBool(serverContent.serverLoadOrderLoaded);
        payload += ",\"loadOrderEntryCount\":";
        payload += std::to_string(serverContent.loadOrderEntryCount);
        payload += ",\"loadOrderAppliedCount\":";
        payload += std::to_string(serverContent.loadOrderAppliedCount);
        payload += ",\"loadOrderDuplicateCount\":";
        payload += std::to_string(serverContent.loadOrderDuplicateCount);
        payload += ",\"loadOrderMissingRegistryCount\":";
        payload += std::to_string(serverContent.loadOrderMissingRegistryCount);
        payload += ",\"loadOrderMissingConfigCount\":";
        payload += std::to_string(serverContent.loadOrderMissingConfigCount);
        payload += ",\"dataFileCount\":";
        payload += std::to_string(serverContent.dataFileCount);
        payload += ",\"checksumCount\":";
        payload += std::to_string(serverContent.checksumCount);
        payload += ",\"enrichedFromOpenMwContentPlan\":";
        payload += jsonBool(serverContent.enrichedFromOpenMwContentPlan);
        payload += ",\"contentPlanFileCount\":";
        payload += std::to_string(serverContent.contentPlanFileCount);
        payload += ",\"contentPlanOrderAppliedCount\":";
        payload += std::to_string(serverContent.contentPlanOrderAppliedCount);
        payload += ",\"computedChecksumCount\":";
        payload += std::to_string(serverContent.computedChecksumCount);
        payload += ",\"unresolvedContentFileCount\":";
        payload += std::to_string(serverContent.unresolvedContentFileCount);
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
        payload += ",\"serverContentDatabase\":{";
        payload += "\"backend\":";
        payload += jsonString(serverContentDatabase.backend);
        payload += ",\"attempted\":";
        payload += jsonBool(serverContentDatabase.attempted);
        payload += ",\"available\":";
        payload += jsonBool(serverContentDatabase.available);
        payload += ",\"changed\":";
        payload += jsonBool(serverContentDatabase.changed);
        payload += ",\"contentPlanFingerprint\":";
        payload += jsonString(serverContentDatabase.contentPlanFingerprint);
        payload += ",\"worldDatabaseFingerprint\":";
        payload += jsonString(serverContentDatabase.worldDatabaseFingerprint);
        payload += ",\"serverWorldCompatibilityFingerprint\":";
        payload += jsonString(serverContentDatabase.serverWorldCompatibilityFingerprint);
        payload += ",\"rootPath\":";
        payload += jsonString(Files::pathToUnicodeString(serverContentDatabase.rootPath));
        payload += ",\"manifestPath\":";
        payload += jsonString(Files::pathToUnicodeString(serverContentDatabase.manifestPath));
        payload += ",\"generatedQuestDatabasePath\":";
        payload += jsonString(Files::pathToUnicodeString(serverContentDatabase.generatedQuestDatabasePath));
        payload += ",\"lastError\":";
        payload += jsonString(serverContentDatabase.lastError);
        payload += ",\"loadOrderSource\":";
        payload += jsonString(serverContentDatabase.loadOrderSource);
        payload += ",\"loadOrderRule\":";
        payload += jsonString(serverContentDatabase.loadOrderRule);
        payload += ",\"dataDirCount\":";
        payload += std::to_string(serverContentDatabase.dataDirCount);
        payload += ",\"loadOrderEntryCount\":";
        payload += std::to_string(serverContentDatabase.loadOrderEntryCount);
        payload += ",\"contentFileCount\":";
        payload += std::to_string(serverContentDatabase.contentFileCount);
        payload += ",\"esmLikeContentFileCount\":";
        payload += std::to_string(serverContentDatabase.esmLikeContentFileCount);
        payload += ",\"resolvedContentFileCount\":";
        payload += std::to_string(serverContentDatabase.resolvedContentFileCount);
        payload += ",\"unresolvedContentFileCount\":";
        payload += std::to_string(serverContentDatabase.unresolvedContentFileCount);
        payload += ",\"checksumCount\":";
        payload += std::to_string(serverContentDatabase.checksumCount);
        payload += ",\"recordIndexCount\":";
        payload += std::to_string(serverContentDatabase.recordIndexCount);
        payload += ",\"recordKeyCount\":";
        payload += std::to_string(serverContentDatabase.recordKeyCount);
        payload += ",\"recordUnkeyedCount\":";
        payload += std::to_string(serverContentDatabase.recordUnkeyedCount);
        payload += ",\"recordWinnerCount\":";
        payload += std::to_string(serverContentDatabase.recordWinnerCount);
        payload += ",\"recordWinnerDeletedCount\":";
        payload += std::to_string(serverContentDatabase.recordWinnerDeletedCount);
        payload += ",\"recordImportErrorCount\":";
        payload += std::to_string(serverContentDatabase.recordImportErrorCount);
        payload += ",\"actorProfileRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorProfileRecordCount);
        payload += ",\"actorProfileNpcCount\":";
        payload += std::to_string(serverContentDatabase.actorProfileNpcCount);
        payload += ",\"actorProfileCreatureCount\":";
        payload += std::to_string(serverContentDatabase.actorProfileCreatureCount);
        payload += ",\"actorProfileAutocalcNpcCount\":";
        payload += std::to_string(serverContentDatabase.actorProfileAutocalcNpcCount);
        payload += ",\"actorAiPackageRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorAiPackageRecordCount);
        payload += ",\"actorAiPackageItemCount\":";
        payload += std::to_string(serverContentDatabase.actorAiPackageItemCount);
        payload += ",\"actorInventoryRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorInventoryRecordCount);
        payload += ",\"actorInventoryItemCount\":";
        payload += std::to_string(serverContentDatabase.actorInventoryItemCount);
        payload += ",\"actorSpellbookRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorSpellbookRecordCount);
        payload += ",\"actorSpellbookSpellCount\":";
        payload += std::to_string(serverContentDatabase.actorSpellbookSpellCount);
        payload += ",\"actorStatsDynamicRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorStatsDynamicRecordCount);
        payload += ",\"actorStatsDynamicItemCount\":";
        payload += std::to_string(serverContentDatabase.actorStatsDynamicItemCount);
        payload += ",\"itemEquipmentRecordCount\":";
        payload += std::to_string(serverContentDatabase.itemEquipmentRecordCount);
        payload += ",\"actorEquipmentRecordCount\":";
        payload += std::to_string(serverContentDatabase.actorEquipmentRecordCount);
        payload += ",\"actorEquipmentItemCount\":";
        payload += std::to_string(serverContentDatabase.actorEquipmentItemCount);
        payload += ",\"containerInventoryRecordCount\":";
        payload += std::to_string(serverContentDatabase.containerInventoryRecordCount);
        payload += ",\"containerInventoryItemCount\":";
        payload += std::to_string(serverContentDatabase.containerInventoryItemCount);
        payload += ",\"pathgridRecordCount\":";
        payload += std::to_string(serverContentDatabase.pathgridRecordCount);
        payload += ",\"pathgridPointCount\":";
        payload += std::to_string(serverContentDatabase.pathgridPointCount);
        payload += ",\"pathgridEdgeCount\":";
        payload += std::to_string(serverContentDatabase.pathgridEdgeCount);
        payload += ",\"cellRecordCount\":";
        payload += std::to_string(serverContentDatabase.cellRecordCount);
        payload += ",\"cellReferenceCount\":";
        payload += std::to_string(serverContentDatabase.cellReferenceCount);
        payload += ",\"cellReferenceMovedCount\":";
        payload += std::to_string(serverContentDatabase.cellReferenceMovedCount);
        payload += ",\"cellReferenceDeletedCount\":";
        payload += std::to_string(serverContentDatabase.cellReferenceDeletedCount);
        payload += ",\"cellReferenceWinnerCount\":";
        payload += std::to_string(serverContentDatabase.cellReferenceWinnerCount);
        payload += ",\"cellReferenceWinnerDeletedCount\":";
        payload += std::to_string(serverContentDatabase.cellReferenceWinnerDeletedCount);
        payload += ",\"cellImportErrorCount\":";
        payload += std::to_string(serverContentDatabase.cellImportErrorCount);
        payload += ",\"questSourceRowCount\":";
        payload += std::to_string(serverContentDatabase.questSourceRowCount);
        payload += ",\"questSourcePackageCount\":";
        payload += std::to_string(serverContentDatabase.questSourcePackageCount);
        payload += ",\"questSourceDialogueCount\":";
        payload += std::to_string(serverContentDatabase.questSourceDialogueCount);
        payload += ",\"questSourceInfoCount\":";
        payload += std::to_string(serverContentDatabase.questSourceInfoCount);
        payload += ",\"questSourceImportErrorCount\":";
        payload += std::to_string(serverContentDatabase.questSourceImportErrorCount);
        payload += ",\"generatedQuestDatabasePackageCount\":";
        payload += std::to_string(serverContentDatabase.generatedQuestDatabasePackageCount);
        payload += ",\"generatedQuestDefinitionCount\":";
        payload += std::to_string(serverContentDatabase.generatedQuestDefinitionCount);
        payload += ",\"generatedQuestStepCount\":";
        payload += std::to_string(serverContentDatabase.generatedQuestStepCount);
        payload += ",\"generatedDialogueTopicCount\":";
        payload += std::to_string(serverContentDatabase.generatedDialogueTopicCount);
        payload += ",\"generatedDialogueResponseCount\":";
        payload += std::to_string(serverContentDatabase.generatedDialogueResponseCount);
        payload += ",\"generatedConditionCount\":";
        payload += std::to_string(serverContentDatabase.generatedConditionCount);
        payload += ",\"generatedQuestEffectCount\":";
        payload += std::to_string(serverContentDatabase.generatedQuestEffectCount);
        payload += ",\"generatedLegacyEffectCount\":";
        payload += std::to_string(serverContentDatabase.generatedLegacyEffectCount);
        payload += ",\"generatedQuestDatabaseImportErrorCount\":";
        payload += std::to_string(serverContentDatabase.generatedQuestDatabaseImportErrorCount);
        payload += ",\"archiveCount\":";
        payload += std::to_string(serverContentDatabase.archiveCount);
        payload += ",\"resolvedArchiveCount\":";
        payload += std::to_string(serverContentDatabase.resolvedArchiveCount);
        payload += ",\"unresolvedArchiveCount\":";
        payload += std::to_string(serverContentDatabase.unresolvedArchiveCount);
        payload += ",\"archiveFileCount\":";
        payload += std::to_string(serverContentDatabase.archiveFileCount);
        payload += ",\"assetProviderCount\":";
        payload += std::to_string(serverContentDatabase.assetProviderCount);
        payload += ",\"resolvedAssetCount\":";
        payload += std::to_string(serverContentDatabase.resolvedAssetCount);
        payload += ",\"assetImportErrorCount\":";
        payload += std::to_string(serverContentDatabase.assetImportErrorCount);
        payload += ",\"tableCount\":";
        payload += std::to_string(serverContentDatabase.tableCount);
        payload += "}";
        payload += ",\"worldDatabase\":{";
        payload += "\"backend\":";
        payload += jsonString(worldDatabase.backend);
        payload += ",\"attempted\":";
        payload += jsonBool(worldDatabase.attempted);
        payload += ",\"loaded\":";
        payload += jsonBool(worldDatabase.loaded);
        payload += ",\"rootPath\":";
        payload += jsonString(Files::pathToUnicodeString(worldDatabase.rootPath));
        payload += ",\"lastError\":";
        payload += jsonString(worldDatabase.lastError);
        payload += ",\"loadOrderSource\":";
        payload += jsonString(worldDatabase.loadOrderSource);
        payload += ",\"loadOrderRule\":";
        payload += jsonString(worldDatabase.loadOrderRule);
        payload += ",\"manifestCount\":";
        payload += std::to_string(worldDatabase.manifestCount);
        payload += ",\"loadOrderEntryCount\":";
        payload += std::to_string(worldDatabase.loadOrderEntryCount);
        payload += ",\"builtinContentFileCount\":";
        payload += std::to_string(worldDatabase.builtinContentFileCount);
        payload += ",\"esmLikeContentFileCount\":";
        payload += std::to_string(worldDatabase.esmLikeContentFileCount);
        payload += ",\"cellRecordCount\":";
        payload += std::to_string(worldDatabase.cellRecordCount);
        payload += ",\"activeCellRecordCount\":";
        payload += std::to_string(worldDatabase.activeCellRecordCount);
        payload += ",\"cellReferenceCount\":";
        payload += std::to_string(worldDatabase.cellReferenceCount);
        payload += ",\"activeCellReferenceCount\":";
        payload += std::to_string(worldDatabase.activeCellReferenceCount);
        payload += ",\"cellReferenceDeletedCount\":";
        payload += std::to_string(worldDatabase.cellReferenceDeletedCount);
        payload += ",\"cellReferenceMovedCount\":";
        payload += std::to_string(worldDatabase.cellReferenceMovedCount);
        payload += ",\"cellReferenceTeleportCount\":";
        payload += std::to_string(worldDatabase.cellReferenceTeleportCount);
        payload += ",\"cellReferenceIndexedCellCount\":";
        payload += std::to_string(worldDatabase.cellReferenceIndexedCellCount);
        payload += ",\"recordWinnerCount\":";
        payload += std::to_string(worldDatabase.recordWinnerCount);
        payload += ",\"recordWinnerDeletedCount\":";
        payload += std::to_string(worldDatabase.recordWinnerDeletedCount);
        payload += ",\"actorProfileRecordCount\":";
        payload += std::to_string(worldDatabase.actorProfileRecordCount);
        payload += ",\"actorProfileNpcCount\":";
        payload += std::to_string(worldDatabase.actorProfileNpcCount);
        payload += ",\"actorProfileCreatureCount\":";
        payload += std::to_string(worldDatabase.actorProfileCreatureCount);
        payload += ",\"actorProfileAutocalcNpcCount\":";
        payload += std::to_string(worldDatabase.actorProfileAutocalcNpcCount);
        payload += ",\"actorAiPackageRecordCount\":";
        payload += std::to_string(worldDatabase.actorAiPackageRecordCount);
        payload += ",\"actorAiPackageItemCount\":";
        payload += std::to_string(worldDatabase.actorAiPackageItemCount);
        payload += ",\"actorInventoryRecordCount\":";
        payload += std::to_string(worldDatabase.actorInventoryRecordCount);
        payload += ",\"actorInventoryItemCount\":";
        payload += std::to_string(worldDatabase.actorInventoryItemCount);
        payload += ",\"actorSpellbookRecordCount\":";
        payload += std::to_string(worldDatabase.actorSpellbookRecordCount);
        payload += ",\"actorSpellbookSpellCount\":";
        payload += std::to_string(worldDatabase.actorSpellbookSpellCount);
        payload += ",\"actorStatsDynamicRecordCount\":";
        payload += std::to_string(worldDatabase.actorStatsDynamicRecordCount);
        payload += ",\"actorStatsDynamicItemCount\":";
        payload += std::to_string(worldDatabase.actorStatsDynamicItemCount);
        payload += ",\"actorEquipmentRecordCount\":";
        payload += std::to_string(worldDatabase.actorEquipmentRecordCount);
        payload += ",\"actorEquipmentItemCount\":";
        payload += std::to_string(worldDatabase.actorEquipmentItemCount);
        payload += ",\"containerInventoryRecordCount\":";
        payload += std::to_string(worldDatabase.containerInventoryRecordCount);
        payload += ",\"containerInventoryItemCount\":";
        payload += std::to_string(worldDatabase.containerInventoryItemCount);
        payload += ",\"pathgridRecordCount\":";
        payload += std::to_string(worldDatabase.pathgridRecordCount);
        payload += ",\"pathgridPointCount\":";
        payload += std::to_string(worldDatabase.pathgridPointCount);
        payload += ",\"pathgridEdgeCount\":";
        payload += std::to_string(worldDatabase.pathgridEdgeCount);
        payload += ",\"pathgridIndexedCellCount\":";
        payload += std::to_string(worldDatabase.pathgridIndexedCellCount);
        payload += ",\"baseRecordResolvedReferenceCount\":";
        payload += std::to_string(worldDatabase.baseRecordResolvedReferenceCount);
        payload += ",\"baseRecordUnresolvedReferenceCount\":";
        payload += std::to_string(worldDatabase.baseRecordUnresolvedReferenceCount);
        payload += ",\"baseRecordAmbiguousReferenceCount\":";
        payload += std::to_string(worldDatabase.baseRecordAmbiguousReferenceCount);
        payload += ",\"baseRecordDeletedReferenceCount\":";
        payload += std::to_string(worldDatabase.baseRecordDeletedReferenceCount);
        payload += ",\"actorReferenceCount\":";
        payload += std::to_string(worldDatabase.actorReferenceCount);
        payload += ",\"containerReferenceCount\":";
        payload += std::to_string(worldDatabase.containerReferenceCount);
        payload += ",\"doorReferenceCount\":";
        payload += std::to_string(worldDatabase.doorReferenceCount);
        payload += ",\"itemReferenceCount\":";
        payload += std::to_string(worldDatabase.itemReferenceCount);
        payload += ",\"staticReferenceCount\":";
        payload += std::to_string(worldDatabase.staticReferenceCount);
        payload += ",\"activatorReferenceCount\":";
        payload += std::to_string(worldDatabase.activatorReferenceCount);
        payload += ",\"levelledItemReferenceCount\":";
        payload += std::to_string(worldDatabase.levelledItemReferenceCount);
        payload += ",\"otherReferenceCount\":";
        payload += std::to_string(worldDatabase.otherReferenceCount);
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
        payload += ",\"usesSinglePlayerProxy\":";
        payload += jsonBool(runtimeTopology.usesSinglePlayerProxy);
        payload += ",\"hasPersistentPlayerActors\":";
        payload += jsonBool(runtimeTopology.hasPersistentPlayerActors);
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
        payload += ",\"openMwWorldState\":{";
        payload += "\"prepared\":";
        payload += jsonBool(runtimeWorldState.prepared);
        payload += ",\"persistent\":";
        payload += jsonBool(runtimeWorldState.persistent);
        payload += ",\"loadedFromSave\":";
        payload += jsonBool(runtimeWorldState.loadedFromSave);
        payload += ",\"initializedNewWorld\":";
        payload += jsonBool(runtimeWorldState.initializedNewWorld);
        payload += ",\"savePath\":";
        payload += jsonString(runtimeWorldState.savePath);
        payload += ",\"manifestPath\":";
        payload += jsonString(runtimeWorldState.manifestPath);
        payload += ",\"contentPlanFingerprint\":";
        payload += jsonString(runtimeWorldState.contentPlanFingerprint);
        payload += ",\"worldDatabaseFingerprint\":";
        payload += jsonString(runtimeWorldState.worldDatabaseFingerprint);
        payload += ",\"serverWorldCompatibilityFingerprint\":";
        payload += jsonString(runtimeWorldState.serverWorldCompatibilityFingerprint);
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

    bool ServerSimulation::acceptPlayerStatsDynamic(Player& player)
    {
        if (player.hasAcceptedStatsDynamicPacket && player.hasFiniteDynamicStats()
            && shouldSuppressTransientPlayerDeath(player, player.cell, player.acceptedStatsDynamicDead,
                player.acceptedStatsDynamic[0].mCurrent, player.creatureStats.mDead,
                player.creatureStats.mDynamic[0].mCurrent, Clock::now(), "client stats-dynamic"))
        {
            player.restoreAcceptedStatsDynamicPacket();
            return false;
        }

        return player.acceptStatsDynamicPacket(true);
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
        if (runtimeOwnsActorCell(serverCell))
        {
            ++mRuntimeActorSnapshotStats.rejectedClientActorCastPackets;
            return false;
        }

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

    bool ServerSimulation::rejectClientActorPacketForServerOwnedCell(
        const Cell* serverCell, const BaseActorList& actorList, const char* packetName)
    {
        if (!canAuthoritativelySimulateActors())
            return false;

        const bool serverOwnsCell = serverCell == nullptr || runtimeOwnsActorCell(*serverCell);
        if (!serverOwnsCell)
            return false;

        ++mRuntimeActorSnapshotStats.rejectedClientActorAuthorityPackets;
        mRuntimeActorSnapshotStats.lastRejectedClientActorPacket = packetName != nullptr ? packetName : "";
        mRuntimeActorSnapshotStats.lastRejectedClientActorCellDescription = actorList.cell.getDescription();
        return true;
    }

    bool ServerSimulation::acceptActorAiSnapshot(BaseActorList& actorList, Cell& serverCell)
    {
        if (runtimeOwnsActorCell(serverCell))
        {
            ++mRuntimeActorSnapshotStats.rejectedClientActorAiPackets;
            return false;
        }

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
        if (runtimeOwnsActorCell(serverCell))
        {
            ++mRuntimeActorSnapshotStats.rejectedClientActorAttackPackets;
            return false;
        }

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
        if (attack.target.isPlayer)
        {
            if (!canApplyServerAttackDamage(attack))
                return;

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
        if (targetCell == nullptr || targetActor == nullptr)
            return;

        const bool aiChanged = applyCombatTargetToActor(*targetCell, *targetActor, attacker);
        if (aiChanged)
        {
            const ActorMovementKey actorKey{ getCellSimulationKey(targetCell->getCellData()),
                targetActor->refNum, targetActor->mpNum };
            mRuntimeClientAiPresentedActors.erase(actorKey);
        }
        if (mRuntime != nullptr && mRuntime->canOwnActorAuthority() && attacker.hasFinitePositionPacket()
            && getCellSimulationKey(attacker.cell) == getCellSimulationKey(targetCell->getCellData()))
        {
            SimulationActorTarget runtimeActor;
            runtimeActor.cell = targetCell->getCellData();
            runtimeActor.refId = targetActor->refId;
            runtimeActor.refNum = targetActor->refNum;
            runtimeActor.mpNum = targetActor->mpNum;

            SimulationPlayerTarget runtimePlayer;
            runtimePlayer.cell = attacker.cell;
            runtimePlayer.position = attacker.position;
            runtimePlayer.guid = attacker.guid;
            runtimePlayer.name = attacker.npc.mName;
            runtimePlayer.npc = attacker.npc;
            runtimePlayer.hasBaseInfo = true;
            if (!attacker.charClass.mId.empty())
            {
                runtimePlayer.classId = attacker.charClass.mId;
                runtimePlayer.hasClass = true;
            }
            if (attacker.hasAcceptedEquipmentPacket)
            {
                for (int slot = 0; slot < equipmentSlotCount; ++slot)
                    runtimePlayer.equipmentItems[slot] = attacker.equipmentItems[slot];
                runtimePlayer.hasEquipmentData = true;
            }
            runtimePlayer.hasPosition = true;
            if (attacker.hasFiniteDynamicStats())
            {
                runtimePlayer.creatureStats = makeSimpleCreatureStats(attacker.creatureStats);
                runtimePlayer.hasStatsDynamicData = true;
            }

            static_cast<void>(mRuntime->startActorCombatWithPlayer(runtimeActor, runtimePlayer));
        }

        if (aiChanged)
            broadcastActorAi(*targetCell, *targetActor);

        if (!canApplyServerAttackDamage(attack))
            return;

        if (applyAttackDamageToActor(*targetActor, attack))
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

        if (runtimeOwnsActorCell(serverCell))
        {
            ++mRuntimeActorSnapshotStats.rejectedClientActorMovementPackets;
            for (const BaseActor& actor : actorList.baseActors)
            {
                BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
                if (currentActor != nullptr && currentActor->hasPositionData)
                    addPositionCorrection(*currentActor);
            }

            if (!correctionActors.empty())
            {
                BaseActorList correctionList = actorList;
                correctionList.baseActors = correctionActors;
                correctionList.count = static_cast<unsigned int>(correctionList.baseActors.size());
                packet.setActorList(&correctionList);
                packet.SendWithReliability(actorList.guid, PacketReliability::ReliableOrdered);
            }

            actorList.baseActors.clear();
            actorList.count = 0;
            return false;
        }

        for (const BaseActor& actor : actorList.baseActors)
        {
            BaseActor* currentActor = serverCell.getActor(actor.refNum, actor.mpNum);
            if (currentActor == nullptr)
                continue;

            const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
            if (isActorInteractionLocked(actorKey, now))
            {
                if (currentActor->hasPositionData)
                    addPositionCorrection(*currentActor);
                continue;
            }

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
                mActorMovementStates[actorKey].lastMovementPacket = now;
                continue;
            }

            BaseActor simulatedActor = actor;
            simulatedActor.hasPositionData = true;

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

    void ServerSimulation::tickActors(float deltaSeconds, bool runtimeFallbackOnly)
    {
        if (!canAuthoritativelySimulateActors())
            return;

        CellController* cellController = CellController::get();
        if (cellController == nullptr)
            return;

        ActorPacket* actorPacket = ServerNetworking::get().getActorPacketController()->GetPacket(ID_ACTOR_POSITION);
        ActorPacket* attackPacket = ServerNetworking::get().getActorPacketController()->GetPacket(ID_ACTOR_ATTACK);
        if (actorPacket == nullptr || attackPacket == nullptr)
            return;

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

            BaseActorList attackActorList;
            attackActorList.cell = cell->getCellData();
            attackActorList.guid = unassignedPacketGuid();
            attackActorList.action = BaseActorList::SET;
            attackActorList.isValid = true;

            for (BaseActor& actor : storedActorList->baseActors)
            {
                if (!actor.hasPositionData || !isClientActorControlUpdateAllowed(&actor))
                    continue;

                const ActorMovementKey actorKey{ cellKey, actor.refNum, actor.mpNum };
                if (isActorInteractionLocked(actorKey, Clock::now()))
                {
                    const bool wasMoving = hasMovementIntent(actor.direction);
                    mActorWanderStates.erase(actorKey);
                    mActorPathgridRouteStates.erase(actorKey);
                    mRuntimeActorMovementStates.erase(actorKey);
                    actor.direction = zeroPosition();
                    if (wasMoving)
                    {
                        ++actor.positionSequence;
                        actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);
                        actor.movementLatencySeconds = 0.f;
                        actor.hasPositionData = true;
                        tickActorList.baseActors.push_back(actor);
                    }
                    continue;
                }

                if (runtimeFallbackOnly)
                {
                    const auto runtimeStateIt = mRuntimeActorMovementStates.find(actorKey);
                    if (runtimeStateIt == mRuntimeActorMovementStates.end()
                        || !runtimeStateIt->second.useFallbackMovement)
                    {
                        static_cast<void>(applyServerActorMeleeIfReady(*cell, actor, actorKey, actor.position,
                            actor.positionSequence, mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds),
                            Clock::now(), attackActorList));
                        continue;
                    }
                }

                ESM::Position direction = actor.direction;
                const bool serverOwnsActorMovement = hasServerOwnedActorMovement(actor);
                ActorPathgridRouteState& routeState = mActorPathgridRouteStates[actorKey];
                const bool hasAiMovementIntent = buildAiMovementIntent(*cell, actor, routeState, direction);
                bool hasWanderMovementIntent = false;

                if (!hasAiMovementIntent && actor.hasAiData && actor.aiAction == BaseActorList::WANDER)
                {
                    ActorWanderState& wanderState = mActorWanderStates[actorKey];
                    hasWanderMovementIntent = buildWanderMovementIntent(
                        *cell, cellKey, actor.refNum, actor.mpNum, actor, wanderState, routeState, deltaSeconds,
                        direction);
                }
                else
                    mActorWanderStates.erase(actorKey);

                const bool hasServerMovementIntent = hasAiMovementIntent || hasWanderMovementIntent;
                if (!hasServerMovementIntent || isEmptyPathgridRouteState(routeState))
                    mActorPathgridRouteStates.erase(actorKey);

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
                    static_cast<void>(applyServerActorMeleeIfReady(*cell, actor, actorKey, actor.position,
                        actor.positionSequence, mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds),
                        Clock::now(), attackActorList));
                    continue;
                }

                actor.position = simulateMovementPosition(actor.position, actor.position, direction, deltaSeconds);
                actor.direction = direction;
                ++actor.positionSequence;
                actor.movementSampleIntervalSeconds = mwmp::sanitizeMovementSampleIntervalSeconds(deltaSeconds);
                actor.movementLatencySeconds = 0.f;
                actor.hasPositionData = true;

                tickActorList.baseActors.push_back(actor);
                static_cast<void>(applyServerActorMeleeIfReady(*cell, actor, actorKey, actor.position,
                    actor.positionSequence, actor.movementSampleIntervalSeconds, Clock::now(), attackActorList));
            }

            tickActorList.count = static_cast<unsigned int>(tickActorList.baseActors.size());
            if (tickActorList.count != 0)
            {
                actorPacket->setActorList(&tickActorList);
                cell->sendToLoaded(actorPacket, &tickActorList);
            }

            attackActorList.count = static_cast<unsigned int>(attackActorList.baseActors.size());
            if (attackActorList.count != 0)
            {
                cell->readActorList(ID_ACTOR_ATTACK, &attackActorList);
                attackPacket->setActorList(&attackActorList);
                cell->sendToLoaded(attackPacket, &attackActorList);
            }
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

        static_cast<void>(ensurePlayerCurrentSimulationCell(player, "accepted player cell change"));

        if (hasPreviousAcceptedCell)
            moveFollowingActorsAcrossPlayerCellChange(player, previousAcceptedCell);

        return true;
    }
}
