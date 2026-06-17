#include "actiontake.hpp"

#include <string>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwgui/inventorywindow.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#endif

#include "class.hpp"
#include "containerstore.hpp"

#ifdef BUILD_TES3MP_CLIENT
namespace
{
    void syncTes3mpWorldItemPickup(
        const MWWorld::Ptr& worldObject, const MWWorld::Ptr& inventoryObject, int count)
    {
        if (!mwmp::Main::isInitialized() || worldObject.isEmpty() || inventoryObject.isEmpty()
            || !worldObject.isInCell())
            return;

        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        if (localPlayer == nullptr || !localPlayer->isLoggedIn())
            return;

        if (worldObject.getCellRef().getRefId().serializeText().find("$dynamic") != std::string::npos)
            return;

        localPlayer->sendItemChange(inventoryObject, count, mwmp::InventoryChanges::ADD);

        mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->originClientScript.clear();
        objectList->addObjectGeneric(worldObject);
        objectList->sendObjectDelete();
    }
}
#endif

namespace MWWorld
{
    ActionTake::ActionTake(const MWWorld::Ptr& object)
        : Action(true, object)
    {
    }

    void ActionTake::executeImp(const Ptr& actor)
    {
        // When in GUI mode, we should use drag and drop
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            MWGui::GuiMode mode = MWBase::Environment::get().getWindowManager()->getMode();
            if (mode == MWGui::GM_Inventory || mode == MWGui::GM_Container)
            {
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->pickUpObject(getTarget());
                return;
            }
        }

        int count = getTarget().getCellRef().getCount();
        if (getTarget().getClass().isGold(getTarget()))
            count *= getTarget().getClass().getValue(getTarget());

        MWBase::Environment::get().getMechanicsManager()->itemTaken(actor, getTarget(), MWWorld::Ptr(), count);
        MWWorld::Ptr newitem = *actor.getClass().getContainerStore(actor).add(getTarget(), count);
#ifdef BUILD_TES3MP_CLIENT
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
            syncTes3mpWorldItemPickup(getTarget(), newitem, count);
#endif
        MWBase::Environment::get().getWorld()->deleteObject(getTarget());
        setTarget(newitem);
    }
}
