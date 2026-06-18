#ifndef OPENMW_MP_QUESTRUNTIMEEVALUATOR_HPP
#define OPENMW_MP_QUESTRUNTIMEEVALUATOR_HPP

#include <cstddef>
#include <string>
#include <string_view>

class Player;

namespace mwmp
{
    struct QuestConditionEvaluationResult
    {
        std::size_t totalConditions = 0;
        std::size_t evaluatedConditions = 0;
        std::size_t failedConditions = 0;
        std::size_t unsupportedConditions = 0;
        bool complete = true;
        bool accepted = true;
    };

    struct QuestLegacyEffectAnalysis
    {
        std::size_t effectCount = 0;
        std::size_t normalizedEffectCount = 0;
        std::size_t legacyScriptCount = 0;
        std::size_t recognizedCommands = 0;
        std::size_t journalCommands = 0;
        std::size_t topicCommands = 0;
        std::size_t inventoryCommands = 0;
        std::size_t dialogueFlowCommands = 0;
        std::size_t actorCommands = 0;
        std::size_t unsupportedCommands = 0;
        bool hasLegacyScript = false;
        bool serverExecutable = false;
        bool requiresInventoryTransaction = false;
        bool requiresActorAuthority = false;
    };

    class QuestRuntimeEvaluator
    {
    public:
        static QuestRuntimeEvaluator& get();

        QuestConditionEvaluationResult evaluateConditionsForPlayer(
            const ::Player& player, std::string_view ownerId) const;
        QuestLegacyEffectAnalysis analyzeLegacyEffects(std::string_view ownerId) const;

        bool supportsJournalConditions() const { return true; }
        bool supportsLegacyEffectAnalysis() const { return true; }

    private:
        QuestRuntimeEvaluator() = default;
    };
}

#endif // OPENMW_MP_QUESTRUNTIMEEVALUATOR_HPP
