#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>

#include <vector>

#include <SDL_system.h>

#include <MyGUI_Exception.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_RotatingSkin.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextIterator.h>

#include <extern/PicoSHA2/picosha2.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/inputmanager.hpp"

#include "../mwgui/mapwindow.hpp"

#include "../mwworld/worldimp.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/cellstore.hpp"

#include "GUIController.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "GUI/GUICharacterSelect.hpp"
#include "GUI/PlayerMarkerCollection.hpp"
#include "GUI/GUIDialogList.hpp"
#include "GUI/GUIChat.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"


mwmp::GUIController::GUIController(): mInputBox(nullptr), mCharacterSelect(nullptr), mListBox(nullptr)
{
    mChat = nullptr;
    keySay = SDL_SCANCODE_Y;
    keyChatMode = SDL_SCANCODE_F2;
}

mwmp::GUIController::~GUIController()
{

}

void mwmp::GUIController::cleanUp()
{
    mPlayerMarkers.clear();
    mPendingChatMessages.clear();
    exitCharacterPresentation();
    mPreCharacterUiSuppressed = false;
    if (mChat != nullptr)
        delete mChat;
    mChat = nullptr;
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    if (mCharacterSelect)
        windowManager->removeDialog(std::move(mCharacterSelect));
    if (mListBox)
        windowManager->removeDialog(std::move(mListBox));
}

void mwmp::GUIController::refreshGuiMode(MWGui::GuiMode guiMode)
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(guiMode))
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(guiMode);
        MWBase::Environment::get().getWindowManager()->pushGuiMode(guiMode);
    }
}

void mwmp::GUIController::setupChat()
{
    assert(mChat == nullptr);

    float chatDelay = Settings::Manager::getFloat("delay", "Chat");
    int chatY = Settings::Manager::getInt("y", "Chat");
    int chatX = Settings::Manager::getInt("x", "Chat");
    int chatW = Settings::Manager::getInt("w", "Chat");
    int chatH = Settings::Manager::getInt("h", "Chat");

    keySay = SDL_GetScancodeFromName(Settings::Manager::getString("keySay", "Chat").c_str());
    keyChatMode = SDL_GetScancodeFromName(Settings::Manager::getString("keyChatMode", "Chat").c_str());

    const bool customChatLayoutAvailable = GUIChat::hasCustomLayoutOverride();
    try
    {
        mChat = new GUIChat(chatX, chatY, chatW, chatH);
    }
    catch (const MyGUI::Exception& e)
    {
        if (!customChatLayoutAvailable)
            throw;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Failed to construct custom %s chat layout, falling back to default layout: %s", Branding::productName,
            e.what());
        GUIChat::disableCustomLayoutOverride();
        mChat = new GUIChat(chatX, chatY, chatW, chatH);
    }
    mChat->setDelay(chatDelay);

    for (const std::string& message : mPendingChatMessages)
        mChat->print(message);

    mPendingChatMessages.clear();
}

void mwmp::GUIController::printChatMessage(std::string &msg)
{
    if (mChat != nullptr)
    {
        mChat->print(msg);
        return;
    }

    mPendingChatMessages.push_back(msg);
}


void mwmp::GUIController::setChatVisible(bool chatVisible)
{
    if (mChat != nullptr)
    {
        if (chatVisible && mChat->windowState == GUIChat::CHAT_DISABLED)
            mChat->windowState = GUIChat::CHAT_ENABLED;

        mChat->setVisible(chatVisible);
    }
}

void mwmp::GUIController::showDialogList(const mwmp::BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    const bool isCharacterList = GUIDialogList::isCharacterListDialog(guiMessageBox.id);
    
    if (mListBox)
        windowManager->removeDialog(std::move(mListBox));
    if (mCharacterSelect)
        windowManager->removeDialog(std::move(mCharacterSelect));

    std::vector<std::string> list;

    std::string buf;

    for (const auto &data : guiMessageBox.data)
    {
        if (data == '\n')
        {
            list.push_back(buf);
            buf.erase();
            continue;
        }
        buf += data;
    }

    list.push_back(buf);

    if (isCharacterList)
        enterCharacterPresentation();
    else
        exitCharacterPresentation();

    if (isCharacterList && GUICharacterSelect::hasCustomLayoutOverride())
    {
        try
        {
            mCharacterSelect = std::make_unique<GUICharacterSelect>(guiMessageBox.label, list, guiMessageBox.note);
            mCharacterSelect->setVisible(true);
            return;
        }
        catch (const MyGUI::Exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                "Failed to construct CommunityMP character select layout, falling back to default list dialog: %s",
                e.what());
            GUICharacterSelect::disableCustomLayoutOverride();
        }
    }

    mListBox = std::make_unique<GUIDialogList>(guiMessageBox.label, list, guiMessageBox.id);

    mListBox->setVisible(true);
}

void mwmp::GUIController::showMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->messageBox(guiMessageBox.label);
}

std::vector<std::string> splitString(const std::string &str, char delim = ';')
{
    std::istringstream ss(str);
    std::vector<std::string> result;
    std::string token;
    while (std::getline(ss, token, delim))
        result.push_back(token);
    return result;
}

void mwmp::GUIController::showCustomMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    std::vector<std::string> buttons = splitString(guiMessageBox.buttons);
    windowManager->interactiveMessageBox(guiMessageBox.label, buttons, false, true);
}

void mwmp::GUIController::showInputBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    if (guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog)
    {
        std::string pendingPassword = Main::takePendingAccountPassword();
        if (!pendingPassword.empty())
        {
            submitInputBox(std::move(pendingPassword), true);
            return;
        }
    }

    if (mInputBox)
        windowManager->removeDialog(std::move(mInputBox));
    mInputBox = std::make_unique<MWGui::TextInputDialog>();

    mInputBox->setEditPassword(guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog);

    mInputBox->setTextLabel(guiMessageBox.label);
    mInputBox->setTextNote(guiMessageBox.note);

    mInputBox->eventDone += MyGUI::newDelegate(this, &GUIController::onInputBoxDone);

    mInputBox->setVisible(true);
}

void mwmp::GUIController::onInputBoxDone(MWGui::WindowBase *parWindow)
{
    std::string textInput = mInputBox->getTextInput();
    const bool passwordDialog = Main::get().getLocalPlayer()->guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog;

    submitInputBox(std::move(textInput), passwordDialog);

    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeDialog(std::move(mInputBox));
}

void mwmp::GUIController::submitInputBox(std::string textInput, bool passwordDialog)
{
    LocalPlayer *localPlayer = Main::get().getLocalPlayer();

    // Send input for password dialogs after it's been hashed and rehashed, for some slight
    // extra security that doesn't require the client to keep storing a salt
    if (passwordDialog)
    {
        textInput = picosha2::hash256_hex_string(textInput);
        textInput = picosha2::hash256_hex_string(textInput + picosha2::hash256_hex_string(picosha2::hash256_hex_string((textInput))));
    }

    localPlayer->guiMessageBox.data = textInput;

    PlayerPacket *playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(Main::get().getLocalPlayer());
    playerPacket->Send();
}

bool mwmp::GUIController::pressedKey(int key)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    if (mChat == nullptr || windowManager->isConsoleMode() || windowManager->getMode() != MWGui::GM_None)
        return false;
    if (key == keyChatMode)
    {
        mChat->pressedChatMode();
        return true;
    }
    else if (key == keySay)
    {
        mChat->pressedSay();
        return true;
    }
    return false;
}

void mwmp::GUIController::changeChatMode()
{
    if (mChat == nullptr)
        return;

    mChat->pressedChatMode();
}

bool mwmp::GUIController::getChatEditState()
{
    if (mChat == nullptr)
        return false;

    return mChat->editState;
}

void mwmp::GUIController::focusChatInput()
{
    if (mChat != nullptr)
        mChat->focusInput();
}

void mwmp::GUIController::update(float dt)
{
    if (mChat != nullptr)
        mChat->update(dt);

    if (mCharacterSelect != nullptr)
    {
        mCharacterSelect->update(dt);
        if (mCharacterSelect->isSubmitted())
        {
            exitCharacterPresentation();
            MWBase::Environment::get().getWindowManager()->removeDialog(std::move(mCharacterSelect));
        }
    }

    if (mCharacterPresentationActive)
    {
        if (shouldKeepCharacterPresentation())
        {
            MWBase::Environment::get().getWindowManager()->setHudVisibility(false);
            setChatVisible(false);
        }
        else
            exitCharacterPresentation();
    }

    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    if (shouldSuppressPreCharacterUi())
    {
        if (!mPreCharacterUiSuppressed)
        {
            mPreCharacterRestoreHudVisible
                = mCharacterPresentationActive ? mRestoreHudVisible : windowManager->isHudVisible();
            mPreCharacterUiSuppressed = true;
        }

        windowManager->setHudVisibility(false);
        setChatVisible(false);
    }
    else if (mPreCharacterUiSuppressed)
    {
        windowManager->setHudVisibility(mPreCharacterRestoreHudVisible);
        setChatVisible(true);
        mPreCharacterUiSuppressed = false;
    }
}

void mwmp::GUIController::enterCharacterPresentation()
{
    if (mCharacterPresentationActive)
        return;

    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    mRestoreHudVisible = windowManager->isHudVisible();
    mCharacterPresentationActive = true;
    windowManager->setHudVisibility(false);
    setChatVisible(false);
}

void mwmp::GUIController::exitCharacterPresentation()
{
    if (!mCharacterPresentationActive)
        return;

    MWBase::Environment::get().getWindowManager()->setHudVisibility(mRestoreHudVisible);
    setChatVisible(true);
    mCharacterPresentationActive = false;
}

bool mwmp::GUIController::shouldKeepCharacterPresentation() const
{
    if (!Main::isInitialized())
        return false;

    const LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    return localPlayer != nullptr && !localPlayer->hasLoadedCharacter();
}

bool mwmp::GUIController::shouldSuppressPreCharacterUi() const
{
    if (!Main::isInitialized())
        return false;

    const LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    return localPlayer != nullptr && localPlayer->isLoggedIn() && !localPlayer->hasLoadedCharacter();
}

void mwmp::GUIController::processCustomMessageBoxInput(int pressedButton)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Pressed: %d", pressedButton);

    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->guiMessageBox.data = MyGUI::utility::toString(pressedButton);

    PlayerPacket* playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(Main::get().getLocalPlayer());
    playerPacket->Send();
}

class MarkerWidget: public MyGUI::Widget
{
MYGUI_RTTI_DERIVED(MarkerWidget)

public:
    void setNormalColour(const MyGUI::Colour& colour)
    {
        mNormalColour = colour;
        setColour(colour);
    }

    void setHoverColour(const MyGUI::Colour& colour)
    {
        mHoverColour = colour;
    }

private:
    MyGUI::Colour mNormalColour;
    MyGUI::Colour mHoverColour;

    void onMouseLostFocus(MyGUI::Widget* _new)
    {
        setColour(mNormalColour);
    }

    void onMouseSetFocus(MyGUI::Widget* _old)
    {
        setColour(mHoverColour);
    }
};

ESM::CustomMarker mwmp::GUIController::createMarker(const PacketGuid &guid)
{
    DedicatedPlayer *player = PlayerList::getPlayer(guid);
    ESM::CustomMarker mEditingMarker;
    if (!player)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Unknown player guid: %s", packetGuidToString(guid).c_str());
        return mEditingMarker;
    }

    mEditingMarker.mNote = player->npc.mName;

    const ESM::Cell *playerCell = &player->cell;

    mEditingMarker.mWorldX = player->position.pos[0];
    mEditingMarker.mWorldY = player->position.pos[1];

    if (!playerCell->isExterior())
        mEditingMarker.mCell = playerCell->mId.empty() ? ESM::RefId::stringRefId(playerCell->mName) : playerCell->mId;
    else
        mEditingMarker.mCell = ESM::RefId::esm3ExteriorCell(playerCell->mData.mX, playerCell->mData.mY);

    return mEditingMarker;
}


void mwmp::GUIController::updatePlayersMarkers(MWGui::LocalMapBase *localMapBase)
{
    std::vector<ESM::CustomMarker> markers;
    markers.reserve(mPlayerMarkers.size());

    for (auto it = mPlayerMarkers.begin(); it != mPlayerMarkers.end(); ++it)
        markers.push_back(it->second);

    localMapBase->updatePlayerMarkers(markers);
}

void mwmp::GUIController::updateGlobalMapMarkerTooltips(MWGui::MapWindow *mapWindow)
{
    std::vector<ESM::CustomMarker> markers;
    markers.reserve(mPlayerMarkers.size());

    for (auto it = mPlayerMarkers.begin(); it != mPlayerMarkers.end(); ++it)
        markers.push_back(it->second);

    mapWindow->updateGlobalPlayerMarkers(markers);
}
