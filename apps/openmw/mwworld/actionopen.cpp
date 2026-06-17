#include "actionopen.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/disease.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include <components/esm3/loadcell.hpp>

#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#endif

namespace MWWorld
{
#ifdef BUILD_TES3MP_CLIENT
    namespace
    {
        ESM::Cell makeTes3mpContainerLockCell(const MWWorld::Ptr& ptr)
        {
            ESM::Cell packetCell;

            if (ptr.isEmpty() || ptr.getCell() == nullptr || ptr.getCell()->getCell() == nullptr)
                return packetCell;

            const MWWorld::Cell& cell = *ptr.getCell()->getCell();
            if (cell.isExterior())
            {
                packetCell.mData.mX = cell.getGridX();
                packetCell.mData.mY = cell.getGridY();
            }
            else
            {
                packetCell.mData.mFlags = ESM::Cell::Interior;
                packetCell.mName = std::string(cell.getNameId());
            }

            packetCell.mRegion = cell.getRegion();
            packetCell.updateId();
            return packetCell;
        }

        bool requestTes3mpContainerLock(const MWWorld::Ptr& container)
        {
            if (!mwmp::Main::isInitialized() || container.isEmpty() || !container.isInCell())
                return false;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            if (localPlayer == nullptr || !localPlayer->isLoggedIn())
                return false;

            mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->originClientScript.clear();
            objectList->cell = makeTes3mpContainerLockCell(container);
            objectList->action = mwmp::BaseObjectList::REQUEST;
            objectList->containerSubAction = mwmp::BaseObjectList::LOCK_REQUEST;
            objectList->addBaseObject(objectList->getBaseObjectFromPtr(container));
            objectList->sendContainer();
            return true;
        }
    }
#endif

    ActionOpen::ActionOpen(const MWWorld::Ptr& container)
        : Action(false, container)
    {
    }

    void ActionOpen::executeImp(const MWWorld::Ptr& actor)
    {
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        if (actor != MWMechanics::getPlayer())
            return;

        if (!MWBase::Environment::get().getMechanicsManager()->onOpen(getTarget()))
            return;

        MWMechanics::diseaseContact(actor, getTarget());

#ifdef BUILD_TES3MP_CLIENT
        if (requestTes3mpContainerLock(getTarget()))
            return;
#endif

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Container, getTarget());
    }
}
