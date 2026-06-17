#ifndef OPENMW_PROCESSORPLAYERUPDATEINVENTORY_HPP
#define OPENMW_PROCESSORPLAYERUPDATEINVENTORY_HPP

#include "../PlayerProcessor.hpp"

#include "../../../mwbase/environment.hpp"
#include "../../../mwbase/windowmanager.hpp"

#include "../../../mwgui/mode.hpp"

namespace mwmp
{
    namespace
    {
        bool isUnsafeFullInventoryReloadGuiOpen()
        {
            auto windowManager = MWBase::Environment::get().getWindowManager();

            return windowManager->containsMode(MWGui::GM_Barter) || windowManager->containsMode(MWGui::GM_Dialogue) ||
                windowManager->containsMode(MWGui::GM_Container);
        }

        void closeUnsafeFullInventoryReloadGui()
        {
            auto windowManager = MWBase::Environment::get().getWindowManager();

            const bool wasItemDragDropEnabled = windowManager->isItemDragDropEnabled();
            windowManager->setItemDragDropEnabled(false);

            if (windowManager->containsMode(MWGui::GM_Barter))
                windowManager->removeGuiMode(MWGui::GM_Barter);
            if (windowManager->containsMode(MWGui::GM_Container))
                windowManager->removeGuiMode(MWGui::GM_Container);
            if (windowManager->containsMode(MWGui::GM_Companion))
                windowManager->removeGuiMode(MWGui::GM_Companion);
            if (windowManager->containsMode(MWGui::GM_Dialogue))
                windowManager->removeGuiMode(MWGui::GM_Dialogue);

            windowManager->setDragDrop(false);
            windowManager->setItemDragDropEnabled(wasItemDragDropEnabled);
        }
    }

    class ProcessorPlayerInventory final: public PlayerProcessor
    {
    public:
        ProcessorPlayerInventory()
        {
            BPP_INIT(ID_PLAYER_INVENTORY)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal()) return;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_INVENTORY about LocalPlayer from server");

            if (isRequest())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Ignoring server request for full local inventory snapshot; server is authoritative");
                return;
            }
            else
            {
                LocalPlayer &localPlayer = static_cast<LocalPlayer&>(*player);
                int inventoryAction = localPlayer.inventoryChanges.action;

                if (inventoryAction == InventoryChanges::SET && isUnsafeFullInventoryReloadGuiOpen())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                        "Closing barter/dialogue/container UI before applying authoritative local inventory SET");
                    closeUnsafeFullInventoryReloadGui();
                }

                if (!player->acceptInventoryPacket())
                    return;

                // Because we send PlayerInventory packets from the same OpenMW methods that we use to set the
                // items received, we need to set a boolean to prevent resending the items set here
                localPlayer.avoidSendingInventoryPackets = true;

                if (inventoryAction == InventoryChanges::ADD)
                    localPlayer.addItems();
                else if (inventoryAction == InventoryChanges::REMOVE)
                    localPlayer.removeItems();
                else // InventoryChanges::SET
                {
                    localPlayer.expectServerEquipmentReload();
                    localPlayer.setInventory();
                    localPlayer.restoreEquipmentFromInventory();
                }

                localPlayer.avoidSendingInventoryPackets = false;
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERUPDATEINVENTORY_HPP

