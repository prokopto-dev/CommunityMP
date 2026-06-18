#ifndef OPENMW_PROCESSOROBJECTDIALOGUECHOICE_HPP
#define OPENMW_PROCESSOROBJECTDIALOGUECHOICE_HPP

#include "../ObjectProcessor.hpp"
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <apps/openmw-mp/CommunityMpLuaEventSender.hpp>
#include <apps/openmw-mp/QuestDatabaseStore.hpp>
#include <apps/openmw-mp/QuestEffectExecutor.hpp>
#include <apps/openmw-mp/QuestRuntimeEvaluator.hpp>
#include <apps/openmw-mp/ServerNetworking.hpp>

namespace mwmp
{
    class ProcessorObjectDialogueChoice : public ObjectProcessor
    {
    public:
        ProcessorObjectDialogueChoice()
        {
            BPP_INIT(ID_OBJECT_DIALOGUE_CHOICE)
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            evaluateServerDialogueChoice(player, objectList);

            ServerEvents::objectEvent("OnObjectDialogueChoice", player.getId(), objectList.cell.getDescription().c_str());
        }

    private:
        static bool authoritativeDialogueEffectsEnabled()
        {
            const char* value = std::getenv("COMMUNITYMP_AUTHORITATIVE_DIALOGUE_EFFECTS");
            if (value == nullptr)
                return false;

            const std::string text(value);
            return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
        }

        static void appendJsonString(std::string& result, std::string_view value)
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

        static const char* jsonBool(bool value)
        {
            return value ? "true" : "false";
        }

        static void sendDialogueChoiceSummary(Player& player, const BaseObjectList& objectList, const BaseObject& object,
            const DialogueResponseRecord* selectedResponse, const QuestEffectExecutionPlan& selectedPlan,
            std::size_t responseCount, std::size_t authoritativeCandidates, bool applied)
        {
            std::string payload;
            payload.reserve(560 + object.topicId.size() + object.refId.size());
            payload += "{\"schema\":";
            payload += std::to_string(clientLuaEventSchemaVersion);
            payload += ",\"kind\":\"quest_dialogue_choice\"";
            payload += ",\"topic\":";
            appendJsonString(payload, object.topicId);
            payload += ",\"cellDescription\":";
            appendJsonString(payload, objectList.cell.getDescription());
            payload += ",\"actorRefId\":";
            appendJsonString(payload, object.refId);
            payload += ",\"refNum\":";
            payload += std::to_string(object.refNum);
            payload += ",\"mpNum\":";
            payload += std::to_string(object.mpNum);
            payload += ",\"responseCount\":";
            payload += std::to_string(responseCount);
            payload += ",\"authoritativeCandidates\":";
            payload += std::to_string(authoritativeCandidates);
            payload += ",\"selectedResponseId\":";
            appendJsonString(payload, selectedResponse != nullptr ? selectedResponse->responseId : "");
            payload += ",\"selectedFullyExecutable\":";
            payload += jsonBool(selectedResponse != nullptr && selectedPlan.fullyExecutable);
            payload += ",\"authoritativeApplyEnabled\":";
            payload += jsonBool(authoritativeDialogueEffectsEnabled());
            payload += ",\"applied\":";
            payload += jsonBool(applied);
            payload += ",\"plannedJournalEffects\":";
            payload += std::to_string(selectedPlan.journalEffects);
            payload += ",\"plannedTopicEffects\":";
            payload += std::to_string(selectedPlan.topicEffects);
            payload += ",\"plannedUnsupportedEffects\":";
            payload += std::to_string(selectedPlan.unsupportedEffects);
            payload += ",\"appliedEffects\":";
            payload += std::to_string(selectedPlan.appliedEffects);
            payload += ",\"skippedDuplicateEffects\":";
            payload += std::to_string(selectedPlan.skippedDuplicateEffects);
            payload += "}";

            static_cast<void>(CommunityMpLuaEventSender::sendToPlayer(
                player, "communitymp.server", "quest_dialogue_choice", std::move(payload)));
        }

        static void evaluateServerDialogueChoice(Player& player, BaseObjectList& objectList)
        {
            QuestDatabaseStore::get().ensureLoaded();
            const bool applyEffects = authoritativeDialogueEffectsEnabled();

            for (const BaseObject& object : objectList.baseObjects)
            {
                if (object.dialogueChoiceType != DialogueChoiceType::TOPIC || object.topicId.empty())
                    continue;

                const std::vector<DialogueResponseRecord> responses
                    = QuestDatabaseStore::get().findDialogueResponsesBySourceTopicId(object.topicId, "Topic");

                const DialogueResponseRecord* selectedResponse = nullptr;
                QuestEffectExecutionPlan selectedPlan;
                std::size_t authoritativeCandidates = 0;

                for (const DialogueResponseRecord& response : responses)
                {
                    const QuestConditionEvaluationResult conditions
                        = QuestRuntimeEvaluator::get().evaluateConditionsForPlayer(player, response.responseId);
                    if (!conditions.complete || !conditions.accepted)
                        continue;

                    ++authoritativeCandidates;
                    QuestEffectExecutionPlan plan = QuestEffectExecutor::get().buildPlan(response.responseId);
                    if (selectedResponse == nullptr)
                    {
                        selectedResponse = &response;
                        selectedPlan = std::move(plan);
                    }
                }

                bool applied = false;
                if (applyEffects && selectedResponse != nullptr && selectedPlan.fullyExecutable)
                {
                    selectedPlan = QuestEffectExecutor::get().applyServerExecutableEffects(
                        player, selectedResponse->responseId);
                    applied = selectedPlan.appliedEffects > 0 || selectedPlan.skippedDuplicateEffects > 0;
                }

                sendDialogueChoiceSummary(player, objectList, object, selectedResponse, selectedPlan, responses.size(),
                    authoritativeCandidates, applied);
            }
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTDIALOGUECHOICE_HPP
