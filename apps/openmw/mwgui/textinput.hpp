#ifndef MWGUI_TEXT_INPUT_H
#define MWGUI_TEXT_INPUT_H

#include "avatarpreview.hpp"
#include "windowbase.hpp"

#include <memory>

namespace MyGUI
{
    class ImageBox;
    class ITexture;
    class TextBox;
}

namespace MWRender
{
    class RaceSelectionPreview;
}

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWGui
{
    class TextInputDialog : public WindowModal
    {
    public:
        TextInputDialog(std::string_view layout = "openmw_text_input.layout", osg::Group* parent = nullptr,
            Resource::ResourceSystem* resourceSystem = nullptr);
        ~TextInputDialog() override;

        std::string getTextInput() const;
        void setTextInput(const std::string& text);

        void setNextButtonShow(bool shown);
        void setTextLabel(std::string_view label);
        void setTextNote(std::string_view note);
        void setEditPassword(bool value);
        void onOpen() override;
        void onFrame(float duration) override;
        MyGUI::Widget* getDefaultKeyFocus() override;

        bool exit() override { return false; }

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onOkClicked(MyGUI::Widget* sender);
        void onTextAccepted(MyGUI::EditBox* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::EditBox* mTextEdit;
        MyGUI::TextBox* mTextNote;
        MyGUI::ImageBox* mAvatarPreviewImage;
        AvatarPreviewController mAvatarPreviewController;
        std::unique_ptr<MWRender::RaceSelectionPreview> mAvatarPreview;
        std::unique_ptr<MyGUI::ITexture> mAvatarPreviewTexture;
    };
}
#endif
