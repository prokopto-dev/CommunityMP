#include "resurrect.hpp"

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadnpc.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"

namespace
{
    template <class T>
    void updateBaseRecord(const MWWorld::Ptr& ptr)
    {
        const auto& store = *MWBase::Environment::get().getESMStore();
        const T* base = store.get<T>().find(ptr.getCellRef().getRefId());
        ptr.get<T>()->mBase = base;
    }
}

namespace MWMechanics
{
    void resurrect(const MWWorld::Ptr& ptr)
    {
        if (!ptr.getClass().isActor())
            return;

        if (ptr == MWMechanics::getPlayer())
        {
            MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
            if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Ended)
                MWBase::Environment::get().getStateManager()->resumeGame();
        }
        else if (ptr.getClass().getCreatureStats(ptr).isDead())
        {
            bool wasEnabled = ptr.getRefData().isEnabled();
            MWBase::Environment::get().getWorld()->undeleteObject(ptr);
            auto windowManager = MWBase::Environment::get().getWindowManager();
            bool wasOpen = windowManager->containsMode(MWGui::GM_Container);
            windowManager->onDeleteCustomData(ptr);
            // HACK: disable/enable object to re-add it to the scene properly (need a new Animation).
            MWBase::Environment::get().getWorld()->disable(ptr);
            // The actor's base record may have changed after this specific reference was created.
            // So we need to update to the current version.
            if (ptr.getClass().isNpc())
                updateBaseRecord<ESM::NPC>(ptr);
            else
                updateBaseRecord<ESM::Creature>(ptr);
            if (wasOpen && !windowManager->containsMode(MWGui::GM_Container))
            {
                // Reopen the loot GUI if it was closed because we resurrected the actor we were looting.
                MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);
                windowManager->forceLootMode(ptr);
            }
            else
            {
                MWBase::Environment::get().getWorld()->removeContainerScripts(ptr);
                // Resets runtime state such as inventory, stats and AI. Does not reset position in the world.
                ptr.getRefData().setCustomData(nullptr);
            }
            if (wasEnabled)
                MWBase::Environment::get().getWorld()->enable(ptr);
        }
    }
}
