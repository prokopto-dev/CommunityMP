#ifndef OPENMW_GUIDIALOGLIST_HPP
#define OPENMW_GUIDIALOGLIST_HPP

#include <string>
#include <vector>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace MyGUI
{
    class Button;
    class EditBox;
    class ListBox;
    class Widget;
}

namespace mwmp
{
    class GUIDialogList : public MWGui::WindowModal
    {
    public:
        GUIDialogList(const std::string& message, const std::vector<std::string>& list, int messageBoxId);
        static bool isCharacterListDialog(int messageBoxId);
        void mousePressed(MyGUI::Widget *_widget);
        void onOpen() override;
        MyGUI::Widget* getDefaultKeyFocus() override;
        void onFrame(float frameDuration);
    private:
        bool mMarkedToDelete = false;
        MyGUI::EditBox* mMessage = nullptr;
        MyGUI::ListBox* mListBox = nullptr;
        MyGUI::Button* mButton = nullptr;
    };
}

#endif //OPENMW_GUIDIALOGLIST_HPP

