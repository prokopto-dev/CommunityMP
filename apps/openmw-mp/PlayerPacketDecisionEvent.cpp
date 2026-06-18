#include "PlayerPacketDecisionEvent.hpp"

#include <string>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "CommunityMpLuaEventSender.hpp"
#include "Player.hpp"

namespace
{
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

    const char* jsonBool(bool value)
    {
        return value ? "true" : "false";
    }
}

namespace mwmp
{
    void sendPlayerPacketDecisionEvent(Player& player, const PlayerPacketDecisionEvent& event)
    {
        if (!player.isHandshaked() || player.getLoadState() != Player::POSTLOADED)
            return;

        std::string payload;
        payload.reserve(360 + event.packetName.size() + event.reason.size());
        payload += "{\"schema\":";
        payload += std::to_string(clientLuaEventSchemaVersion);
        payload += ",\"kind\":\"player_packet_decision\",\"packet\":";
        appendJsonString(payload, event.packetName);
        payload += ",\"reason\":";
        appendJsonString(payload, event.reason);
        payload += ",\"accepted\":";
        payload += jsonBool(event.accepted);
        payload += ",\"corrected\":";
        payload += jsonBool(event.corrected);
        payload += ",\"attemptedSequence\":";
        payload += std::to_string(event.attemptedSequence);
        payload += ",\"authoritativeSequence\":";
        payload += std::to_string(event.authoritativeSequence);
        payload += ",\"acceptedSequence\":";
        payload += std::to_string(event.acceptedSequence);
        payload += ",\"attemptedAction\":";
        payload += std::to_string(event.attemptedAction);
        payload += ",\"authoritativeAction\":";
        payload += std::to_string(event.authoritativeAction);
        payload += ",\"attemptedItemCount\":";
        payload += std::to_string(event.attemptedItemCount);
        payload += ",\"authoritativeItemCount\":";
        payload += std::to_string(event.authoritativeItemCount);
        payload += ",\"attemptedChangedSlotCount\":";
        payload += std::to_string(event.attemptedChangedSlotCount);
        payload += ",\"authoritativeChangedSlotCount\":";
        payload += std::to_string(event.authoritativeChangedSlotCount);
        payload += ",\"exchangeFullInfo\":";
        payload += jsonBool(event.exchangeFullInfo);
        payload += "}";

        if (!CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "player_packet_decision", std::move(payload)))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Failed to send player packet decision event for %s packet=%.*s",
                player.npc.mName.c_str(), static_cast<int>(event.packetName.size()), event.packetName.data());
        }
    }
}
