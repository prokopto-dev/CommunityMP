#include <components/openmw-mp/TimedLog.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"

#include <string>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ListBox.h>

#include "GUIDialogList.hpp"
#include "../Main.hpp"
#include "../Networking.hpp"
#include "../LocalPlayer.hpp"

using namespace mwmp;

namespace
{
    constexpr const char* defaultDialogListLayout = "tes3mp_dialog_list.layout";
    constexpr int characterListGuiId = 5;
}

bool GUIDialogList::isCharacterListDialog(int messageBoxId)
{
    return messageBoxId == characterListGuiId;
}

GUIDialogList::GUIDialogList(const std::string& message, const std::vector<std::string>& list, int /*messageBoxId*/)
    : WindowModal(defaultDialogListLayout)
{
    center(); // center window

    getWidget(mListBox, "ListBox");
    getWidget(mMessage, "Message");
    getWidget(mButton, "OkButton");

    mButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIDialogList::mousePressed);

    mMessage->setCaptionWithReplacing(message);
    for (size_t i = 0; i < list.size(); i++)
        mListBox->addItem(list[i]);
}

void GUIDialogList::mousePressed(MyGUI::Widget * /*widget*/)
{
    setVisible(false);

    size_t id = mListBox->getIndexSelected();

    Main::get().getLocalPlayer()->guiMessageBox.data = MyGUI::utility::toString(id);
    Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX)->setPlayer(Main::get().getLocalPlayer());
    Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX)->Send();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Selected id: %d", id);
    if (id == MyGUI::ITEM_NONE)
        return;

    std::string itemName = mListBox->getItemNameAt(mListBox->getIndexSelected()).asUTF8();
    LOG_APPEND(TimedLog::LOG_VERBOSE, "name of item: '%s'", itemName.c_str());
}

void GUIDialogList::onOpen()
{
    WindowModal::onOpen();
    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mListBox);
}

MyGUI::Widget* GUIDialogList::getDefaultKeyFocus()
{
    return mListBox;
}

void GUIDialogList::onFrame(float frameDuration)
{

}

