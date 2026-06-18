#ifndef OPENMW_MP_QUESTEFFECTEXECUTOR_HPP
#define OPENMW_MP_QUESTEFFECTEXECUTOR_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Base/BasePlayer.hpp>

class Player;

namespace mwmp
{
    struct QuestEffectExecutionPlan
    {
        std::size_t totalEffects = 0;
        std::size_t executableEffects = 0;
        std::size_t journalEffects = 0;
        std::size_t topicEffects = 0;
        std::size_t dialogueFlowEffects = 0;
        std::size_t inventoryEffects = 0;
        std::size_t actorEffects = 0;
        std::size_t unsupportedEffects = 0;
        std::size_t appliedEffects = 0;
        std::size_t skippedDuplicateEffects = 0;
        bool fullyExecutable = true;
        bool requiresInventoryTransaction = false;
        bool requiresActorAuthority = false;
        bool closesDialogue = false;
        std::size_t choiceCount = 0;
        std::vector<JournalItem> journalChanges;
        std::vector<Topic> topicChanges;
    };

    class QuestEffectExecutor
    {
    public:
        static QuestEffectExecutor& get();

        QuestEffectExecutionPlan buildPlan(std::string_view ownerId) const;
        QuestEffectExecutionPlan applyServerExecutableEffects(::Player& player, std::string_view ownerId) const;
        bool supportsServerExecutableEffects() const { return true; }
        bool supportsEffectReplayProtection() const { return true; }

    private:
        QuestEffectExecutor() = default;
    };
}

#endif // OPENMW_MP_QUESTEFFECTEXECUTOR_HPP
