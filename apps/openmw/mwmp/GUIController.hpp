#ifndef OPENMW_GUICONTROLLER_HPP
#define OPENMW_GUICONTROLLER_HPP

#include <components/settings/settings.hpp>

#include <memory>
#include <string>
#include <vector>

#include "apps/openmw/mwgui/mode.hpp"
#include "apps/openmw/mwgui/textinput.hpp"

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include "GUI/PlayerMarkerCollection.hpp"

namespace MWGui
{
    class LocalMapBase;
    class MapWindow;
}

namespace mwmp
{
    class GUICharacterSelect;
    class GUIDialogList;
    class GUIChat;
    class GUIController
    {
    public:
        GUIController();
        ~GUIController();
        void cleanUp();

        void refreshGuiMode(MWGui::GuiMode guiMode);

        void setupChat();

        void printChatMessage(std::string &msg);
        void setChatVisible(bool chatVisible);

        void showMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox);
        void showCustomMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox);
        void showInputBox(const BasePlayer::GUIMessageBox &guiMessageBox);

        void showDialogList(const BasePlayer::GUIMessageBox &guiMessageBox);

        /// Returns 0 if there was no events
        bool pressedKey(int key);

        void changeChatMode();

        bool getChatEditState();
        void focusChatInput();

        void update(float dt);

        void processCustomMessageBoxInput(int pressedButton);

        void updatePlayersMarkers(MWGui::LocalMapBase *localMapBase);
        void updateGlobalMapMarkerTooltips(MWGui::MapWindow *pWindow);

        ESM::CustomMarker createMarker(const PacketGuid &guid);
        PlayerMarkerCollection mPlayerMarkers;

    private:
        GUIChat *mChat;
        std::vector<std::string> mPendingChatMessages;
        int keySay;
        int keyChatMode;

        long id;
        std::unique_ptr<MWGui::TextInputDialog> mInputBox;
        std::unique_ptr<GUICharacterSelect> mCharacterSelect;
        std::unique_ptr<GUIDialogList> mListBox;
        bool mCharacterPresentationActive = false;
        bool mRestoreHudVisible = true;
        bool mPreCharacterUiSuppressed = false;
        bool mPreCharacterRestoreHudVisible = true;
        void enterCharacterPresentation();
        void exitCharacterPresentation();
        bool shouldKeepCharacterPresentation() const;
        bool shouldSuppressPreCharacterUi() const;
        void onInputBoxDone(MWGui::WindowBase* parWindow);
        void submitInputBox(std::string textInput, bool passwordDialog);
        //MyGUI::Widget *oldFocusWidget, *currentFocusWidget;
    };
}

#endif //OPENMW_GUICONTROLLER_HPP

