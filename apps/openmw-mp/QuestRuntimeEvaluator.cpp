#include "QuestRuntimeEvaluator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <components/esm3/dialoguecondition.hpp>

#include "Player.hpp"
#include "PlayerQuestStateStore.hpp"
#include "QuestDatabaseStore.hpp"

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

    std::string trimmed(std::string_view value)
    {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
            ++first;

        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
            --last;

        return std::string(value.substr(first, last - first));
    }

    std::optional<double> parseNumber(std::string_view value)
    {
        const std::string text = trimmed(value);
        if (text.empty())
            return std::nullopt;

        char* end = nullptr;
        const double result = std::strtod(text.c_str(), &end);
        if (end == text.c_str())
            return std::nullopt;

        while (*end != '\0')
        {
            if (!std::isspace(static_cast<unsigned char>(*end)))
                return std::nullopt;
            ++end;
        }

        if (!std::isfinite(result))
            return std::nullopt;

        return result;
    }

    bool isJournalCondition(const mwmp::QuestConditionRecord& condition)
    {
        return normalizedLookupKey(condition.functionName) == "journal"
            || condition.functionCode == ESM::DialogueCondition::Function_Journal;
    }

    bool matchesComparison(double actual, double expected, const mwmp::QuestConditionRecord& condition)
    {
        const std::string comparison = normalizedLookupKey(condition.comparison);
        const int code = condition.comparisonCode;
        constexpr double epsilon = 0.0001;

        if (comparison == "eq" || code == ESM::DialogueCondition::Comp_Eq)
            return std::abs(actual - expected) <= epsilon;
        if (comparison == "ne" || code == ESM::DialogueCondition::Comp_Ne)
            return std::abs(actual - expected) > epsilon;
        if (comparison == "gt" || code == ESM::DialogueCondition::Comp_Gt)
            return actual > expected;
        if (comparison == "ge" || code == ESM::DialogueCondition::Comp_Ge)
            return actual >= expected;
        if (comparison == "lt" || comparison == "ls" || code == ESM::DialogueCondition::Comp_Ls)
            return actual < expected;
        if (comparison == "le" || code == ESM::DialogueCondition::Comp_Le)
            return actual <= expected;

        return false;
    }

    std::vector<std::string> splitScriptCommands(std::string_view script)
    {
        std::vector<std::string> result;
        std::istringstream stream{ std::string(script) };
        std::string line;
        while (std::getline(stream, line))
        {
            const std::string command = trimmed(line);
            if (command.empty() || command.starts_with(";"))
                continue;

            result.push_back(command);
        }
        return result;
    }

    bool startsWithCommand(std::string_view line, std::string_view command)
    {
        const std::string normalizedLine = normalizedLookupKey(line);
        const std::string normalizedCommand = normalizedLookupKey(command);
        if (!normalizedLine.starts_with(normalizedCommand))
            return false;

        if (normalizedLine.size() == normalizedCommand.size())
            return true;

        const char next = normalizedLine[normalizedCommand.size()];
        return std::isspace(static_cast<unsigned char>(next)) || next == '"' || next == '\'';
    }

    bool containsActorTarget(std::string_view line)
    {
        return normalizedLookupKey(line).find("->") != std::string::npos;
    }

    void classifyLegacyCommand(std::string_view command, mwmp::QuestLegacyEffectAnalysis& analysis)
    {
        if (startsWithCommand(command, "journal") || startsWithCommand(command, "setjournalindex"))
        {
            ++analysis.recognizedCommands;
            ++analysis.journalCommands;
            return;
        }

        if (startsWithCommand(command, "addtopic"))
        {
            ++analysis.recognizedCommands;
            ++analysis.topicCommands;
            return;
        }

        if (startsWithCommand(command, "goodbye") || startsWithCommand(command, "choice"))
        {
            ++analysis.recognizedCommands;
            ++analysis.dialogueFlowCommands;
            return;
        }

        if (startsWithCommand(command, "additem") || startsWithCommand(command, "removeitem")
            || startsWithCommand(command, "player->additem") || startsWithCommand(command, "player->removeitem"))
        {
            ++analysis.recognizedCommands;
            ++analysis.inventoryCommands;
            analysis.requiresInventoryTransaction = true;
            return;
        }

        if (startsWithCommand(command, "setfight") || startsWithCommand(command, "startcombat")
            || startsWithCommand(command, "moddisposition") || containsActorTarget(command))
        {
            ++analysis.recognizedCommands;
            ++analysis.actorCommands;
            analysis.requiresActorAuthority = true;
            return;
        }

        ++analysis.unsupportedCommands;
    }
}

namespace mwmp
{
    QuestRuntimeEvaluator& QuestRuntimeEvaluator::get()
    {
        static QuestRuntimeEvaluator evaluator;
        return evaluator;
    }

    QuestConditionEvaluationResult QuestRuntimeEvaluator::evaluateConditionsForPlayer(
        const ::Player& player, std::string_view ownerId) const
    {
        QuestConditionEvaluationResult result;
        const std::vector<QuestConditionRecord> conditions = QuestDatabaseStore::get().findConditionsByOwnerId(ownerId);
        result.totalConditions = conditions.size();

        for (const QuestConditionRecord& condition : conditions)
        {
            if (isJournalCondition(condition))
            {
                const std::optional<double> expected = parseNumber(condition.value);
                if (!expected || condition.variable.empty())
                {
                    ++result.unsupportedConditions;
                    result.complete = false;
                    result.accepted = false;
                    continue;
                }

                const double actual = PlayerQuestStateStore::get().getJournalIndex(player.guid, condition.variable)
                    .value_or(0);
                ++result.evaluatedConditions;
                if (!matchesComparison(actual, *expected, condition))
                {
                    ++result.failedConditions;
                    result.accepted = false;
                }
                continue;
            }

            ++result.unsupportedConditions;
            result.complete = false;
            result.accepted = false;
        }

        return result;
    }

    QuestLegacyEffectAnalysis QuestRuntimeEvaluator::analyzeLegacyEffects(std::string_view ownerId) const
    {
        QuestLegacyEffectAnalysis analysis;
        const std::vector<LegacyQuestEffectRecord> effects = QuestDatabaseStore::get().findLegacyEffectsByOwnerId(ownerId);
        analysis.effectCount = effects.size();

        for (const LegacyQuestEffectRecord& effect : effects)
        {
            if (effect.script.empty())
                continue;

            analysis.hasLegacyScript = true;
            for (const std::string& command : splitScriptCommands(effect.script))
                classifyLegacyCommand(command, analysis);
        }

        analysis.serverExecutable = analysis.hasLegacyScript
            && analysis.unsupportedCommands == 0
            && !analysis.requiresActorAuthority
            && !analysis.requiresInventoryTransaction;
        return analysis;
    }
}
