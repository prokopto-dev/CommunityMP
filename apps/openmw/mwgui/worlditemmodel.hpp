#ifndef OPENMW_APPS_OPENMW_MWGUI_WORLDITEMMODEL_H
#define OPENMW_APPS_OPENMW_MWGUI_WORLDITEMMODEL_H

#include "itemmodel.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>

#ifdef BUILD_TES3MP_CLIENT
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#endif

#include <components/esm/refid.hpp>

#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>

#include <stdexcept>
#include <string>

namespace MWGui
{
    // Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
    class WorldItemModel : public ItemModel
    {
        MWWorld::Ptr dropItemImpl(const ItemStack& item, int count, bool copy)
        {
            MWBase::World& world = *MWBase::Environment::get().getWorld();

            const MWWorld::Ptr player = world.getPlayerPtr();

            world.breakInvisibility(player);

            const MWWorld::Ptr dropped = world.canPlaceObject(mCursorX, mCursorY)
                ? world.placeObject(item.mBase, mCursorX, mCursorY, count, copy)
                : world.dropObjectOnGround(player, item.mBase, count, copy);

            dropped.getCellRef().setOwner(ESM::RefId());

            return dropped;
        }

#ifdef BUILD_TES3MP_CLIENT
        bool canSyncTes3mpDrop(const MWWorld::Ptr& item) const
        {
            if (!mwmp::Main::isInitialized())
                return true;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            if (localPlayer == nullptr || !localPlayer->isLoggedIn())
                return true;

            if (item.getCellRef().getRefId().serializeText().find("$dynamic") == std::string::npos)
                return true;

            MWBase::Environment::get().getWindowManager()->messageBox(
                "You cannot place unsynchronized custom items in multiplayer.");
            return false;
        }

        void syncTes3mpDrop(const ItemStack& item, const MWWorld::Ptr& dropped, int count)
        {
            if (!mwmp::Main::isInitialized() || dropped.isEmpty())
                return;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            if (localPlayer == nullptr || !localPlayer->isLoggedIn())
                return;

            mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectPlace(dropped, true);

            if (objectList->baseObjects.empty())
                return;

            localPlayer->sendItemChange(item.mBase, count, mwmp::InventoryChanges::REMOVE);
            objectList->sendObjectPlace();
        }
#endif

    public:
        explicit WorldItemModel(float cursorX, float cursorY)
            : mCursorX(cursorX)
            , mCursorY(cursorY)
        {
        }

        ModelIndex getIndex(const ItemStack& /*item*/) override
        {
            throw std::runtime_error("WorldItemModel::getIndex is not implemented");
        }

        void update() override {}

        size_t getItemCount() override { return 0; }

        ItemStack getItem(ModelIndex /*index*/) override
        {
            throw std::runtime_error("WorldItemModel::getItem is not implemented");
        }

        bool usesContainer(const MWWorld::Ptr&) override { return false; }

        bool onDropItem(const MWWorld::Ptr& item, int /*count*/) override
        {
#ifdef BUILD_TES3MP_CLIENT
            return canSyncTes3mpDrop(item);
#else
            return true;
#endif
        }

    protected:
        MWWorld::Ptr addItem(const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            const int prevCount = item.mBase.getCellRef().getCount(false);
            const int intCount = static_cast<int>(count);
            item.mBase.getCellRef().setCount(intCount);
            MWWorld::Ptr ptr = dropItemImpl(item, intCount, false);
#ifdef BUILD_TES3MP_CLIENT
            syncTes3mpDrop(item, ptr, intCount);
#endif
            item.mBase.getCellRef().setCount(prevCount);
            return ptr;
        }

        MWWorld::Ptr copyItem(const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            const int intCount = static_cast<int>(count);
            MWWorld::Ptr ptr = dropItemImpl(item, intCount, true);
#ifdef BUILD_TES3MP_CLIENT
            syncTes3mpDrop(item, ptr, intCount);
#endif
            return ptr;
        }

        void removeItem(const ItemStack& /*item*/, size_t /*count*/) override
        {
            throw std::runtime_error("WorldItemModel::removeItem is not implemented");
        }

    private:
        float mCursorX;
        float mCursorY;
    };
}

#endif
