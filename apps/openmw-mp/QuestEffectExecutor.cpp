#include "QuestEffectExecutor.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

#include "Player.hpp"
#include "PlayerQuestStateStore.hpp"
#include "QuestDatabaseStore.hpp"
#include "ServerNetworking.hpp"

namespace
{
    std::string normalizedLookupKey(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    bool isServerExecutablePolicy(std::string_view executionPolicy)
    {
        return normalizedLookupKey(executionPolicy) == "server-executable";
    }

    bool requiresInventoryTransaction(const mwmp::QuestEffectRecord& effect)
    {
        const std::string transactionKind = normalizedLookupKey(effect.transactionKind);
        const std::string authorityRequirement = normalizedLookupKey(effect.authorityRequirement);
        return normalizedLookupKey(effect.executionPolicy) == "inventory-transaction-required"
            || transactionKind == "inventory"
            || authorityRequirement.find("inventory") != std::string::npos;
    }

    bool requiresActorAuthority(const mwmp::QuestEffectRecord& effect)
    {
        const std::string transactionKind = normalizedLookupKey(effect.transactionKind);
        const std::string authorityRequirement = normalizedLookupKey(effect.authorityRequirement);
        return normalizedLookupKey(effect.executionPolicy) == "actor-authority-required"
            || transactionKind == "actor-cell"
            || authorityRequirement.find("actor") != std::string::npos
            || authorityRequirement.find("cell") != std::string::npos;
    }

    mwmp::JournalItem makeJournalEntry(const mwmp::QuestEffectRecord& effect)
    {
        mwmp::JournalItem item;
        item.type = mwmp::JournalItem::ENTRY;
        item.quest = effect.quest;
        item.index = effect.index;
        item.actorRefId = effect.targetKind == "actor" ? effect.target : "";
        item.hasTimestamp = false;
        return item;
    }

    mwmp::Topic makeTopic(const mwmp::QuestEffectRecord& effect)
    {
        mwmp::Topic topic;
        topic.topicId = effect.topic;
        return topic;
    }

    void sendJournalChanges(Player& player, const std::vector<mwmp::JournalItem>& changes)
    {
        if (changes.empty())
            return;

        player.journalChanges = changes;
        player.journalChangesAreLoad = false;
        mwmp::PlayerPacket* packet = mwmp::ServerNetworking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_JOURNAL);
        packet->setPlayer(&player);
        packet->Send(false);
        mwmp::PlayerQuestStateStore::get().applyJournalChanges(player);
        player.journalChanges.clear();
        player.journalChangesAreLoad = false;
    }

    void sendTopicChanges(Player& player, const std::vector<mwmp::Topic>& changes)
    {
        if (changes.empty())
            return;

        player.topicChanges = changes;
        player.topicChangesAreLoad = false;
        mwmp::PlayerPacket* packet = mwmp::ServerNetworking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_TOPIC);
        packet->setPlayer(&player);
        packet->Send(false);
        mwmp::PlayerQuestStateStore::get().applyTopicChanges(player);
        player.topicChanges.clear();
        player.topicChangesAreLoad = false;
    }
}

namespace mwmp
{
    QuestEffectExecutor& QuestEffectExecutor::get()
    {
        static QuestEffectExecutor executor;
        return executor;
    }

    QuestEffectExecutionPlan QuestEffectExecutor::buildPlan(std::string_view ownerId) const
    {
        QuestEffectExecutionPlan plan;
        const std::vector<QuestEffectRecord> effects = QuestDatabaseStore::get().findQuestEffectsByOwnerId(ownerId);
        plan.totalEffects = effects.size();

        for (const QuestEffectRecord& effect : effects)
        {
            const std::string effectKind = normalizedLookupKey(effect.effectKind);
            const bool serverExecutable = isServerExecutablePolicy(effect.executionPolicy);

            if (effectKind == "journal.set")
            {
                ++plan.journalEffects;
                if (serverExecutable && !effect.quest.empty())
                {
                    ++plan.executableEffects;
                    plan.journalChanges.push_back(makeJournalEntry(effect));
                }
                else
                {
                    ++plan.unsupportedEffects;
                    plan.fullyExecutable = false;
                }
            }
            else if (effectKind == "topic.add")
            {
                ++plan.topicEffects;
                if (serverExecutable && !effect.topic.empty())
                {
                    ++plan.executableEffects;
                    plan.topicChanges.push_back(makeTopic(effect));
                }
                else
                {
                    ++plan.unsupportedEffects;
                    plan.fullyExecutable = false;
                }
            }
            else if (effectKind == "dialogue.goodbye")
            {
                ++plan.dialogueFlowEffects;
                plan.closesDialogue = true;
                if (serverExecutable)
                    ++plan.executableEffects;
                else
                {
                    ++plan.unsupportedEffects;
                    plan.fullyExecutable = false;
                }
            }
            else if (effectKind == "dialogue.choice")
            {
                ++plan.dialogueFlowEffects;
                plan.choiceCount += static_cast<std::size_t>(std::max(effect.choiceCount, 0));
                if (serverExecutable)
                    ++plan.executableEffects;
                else
                {
                    ++plan.unsupportedEffects;
                    plan.fullyExecutable = false;
                }
            }
            else if (effectKind.starts_with("inventory.") || requiresInventoryTransaction(effect))
            {
                ++plan.inventoryEffects;
                plan.requiresInventoryTransaction = true;
                plan.fullyExecutable = false;
            }
            else if (effectKind.starts_with("actor.") || requiresActorAuthority(effect))
            {
                ++plan.actorEffects;
                plan.requiresActorAuthority = true;
                plan.fullyExecutable = false;
            }
            else
            {
                ++plan.unsupportedEffects;
                plan.fullyExecutable = false;
            }
        }

        return plan;
    }

    QuestEffectExecutionPlan QuestEffectExecutor::applyServerExecutableEffects(
        ::Player& player, std::string_view ownerId) const
    {
        QuestEffectExecutionPlan plan = buildPlan(ownerId);
        sendJournalChanges(player, plan.journalChanges);
        sendTopicChanges(player, plan.topicChanges);
        return plan;
    }
}
