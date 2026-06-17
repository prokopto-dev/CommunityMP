#include "textinput.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Exception.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_UString.h>

#include <osg/Texture2D>

#include <components/esm/refid.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "../mwrender/characterpreview.hpp"

namespace
{
    template <class T>
    T* findOptionalWidget(MWGui::WindowBase& window, std::string_view name)
    {
        try
        {
            T* widget = nullptr;
            window.getWidget(widget, name);
            return widget;
        }
        catch (const MyGUI::Exception&)
        {
            return nullptr;
        }
    }
}

namespace MWGui
{

    TextInputDialog::TextInputDialog(
        std::string_view layout, osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : WindowModal(std::string(layout))
        , mAvatarPreviewImage(nullptr)
    {
        // Centre dialog
        center();

        getWidget(mTextEdit, "TextEdit");
        mTextEdit->eventEditSelectAccept += newDelegate(this, &TextInputDialog::onTextAccepted);

        getWidget(mTextNote, "TextNote");
        mTextNote->setVisible(false);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TextInputDialog::onOkClicked);

        mAvatarPreviewImage = findOptionalWidget<MyGUI::ImageBox>(*this, "AvatarPreviewImage");
        if (mAvatarPreviewImage != nullptr && parent != nullptr && resourceSystem != nullptr)
        {
            mAvatarPreview = std::make_unique<MWRender::RaceSelectionPreview>(
                parent, resourceSystem, MWRender::RaceSelectionPreview::PreviewMode::Body);
            mAvatarPreview->rebuild();
            mAvatarPreviewController.bind(mAvatarPreviewImage, mAvatarPreview.get());
            mAvatarPreviewController.setAngle(0.f);
            mAvatarPreviewTexture = std::make_unique<MyGUIPlatform::OSGTexture>(
                mAvatarPreview->getTexture(), mAvatarPreview->getTextureStateSet());
            mAvatarPreviewImage->setRenderItemTexture(mAvatarPreviewTexture.get());
            // The widget is Y-down, the RTT image is Y-up, so this UV is inverted.
            mAvatarPreviewImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));
        }

        // Make sure the edit box has focus
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);

        mControllerButtons.mA = "#{Interface:OK}";
    }

    TextInputDialog::~TextInputDialog()
    {
        if (mAvatarPreviewImage != nullptr)
            mAvatarPreviewImage->setRenderItemTexture(nullptr);
    }

    void TextInputDialog::setNextButtonShow(bool shown)
    {
        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");

        if (shown)
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sNext", {})));
        else
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
    }

    void TextInputDialog::setTextLabel(std::string_view label)
    {
        setText("LabelT", label);
    }

    void TextInputDialog::setTextNote(std::string_view note)
    {
        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");

        if (note.empty())
        {
            mTextNote->setVisible(false);
            mMainWidget->setSize(320, 97);
            okButton->setPosition(264, 60);
        }
        else
        {
            setText("TextNote", note);
            mTextNote->setVisible(true);
            mMainWidget->setSize(320, 191);
            okButton->setPosition(264, 158);
        }

        center();
    }

    void TextInputDialog::setEditPassword(bool value)
    {
        mTextEdit->setEditPassword(value);
    }

    void TextInputDialog::onOpen()
    {
        WindowModal::onOpen();
        // Make sure the edit box has focus
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
    }

    void TextInputDialog::onFrame(float duration)
    {
        mAvatarPreviewController.update(duration);
    }

    MyGUI::Widget* TextInputDialog::getDefaultKeyFocus()
    {
        return mTextEdit;
    }

    // widget controls

    void TextInputDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
        if (mTextEdit->getCaption().empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage37}");
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
        }
        else
            eventDone(this);
    }

    void TextInputDialog::onTextAccepted(MyGUI::EditBox* sender)
    {
        onOkClicked(sender);

        // To do not spam onTextAccepted() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    std::string TextInputDialog::getTextInput() const
    {
        return mTextEdit->getCaption();
    }

    void TextInputDialog::setTextInput(const std::string& text)
    {
        mTextEdit->setCaption(text);
    }

    bool TextInputDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            onOkClicked(nullptr);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }

        return false;
    }
}
