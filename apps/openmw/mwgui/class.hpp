#ifndef MWGUI_CLASS_H
#define MWGUI_CLASS_H

#include <array>
#include <memory>
#include <vector>

#include <MyGUI_EditBox.h>

#include <components/esm/attr.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadclas.hpp>

#include "avatarpreview.hpp"
#include "widgets.hpp"
#include "windowbase.hpp"

namespace MWRender
{
    class RaceSelectionPreview;
}

namespace MyGUI
{
    class Button;
    class ImageBox;
    class ITexture;
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
    void setClassImage(MyGUI::ImageBox* imageBox, const ESM::RefId& classId);

    class InfoBoxDialog : public WindowModal
    {
    public:
        InfoBoxDialog();

        typedef std::vector<std::string> ButtonList;

        void setText(const std::string& str);
        std::string getText() const;
        void setButtons(ButtonList& buttons);

        void onOpen() override;

        bool exit() override { return false; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<int> EventHandle_Int;

        /** Event : Button was clicked.\n
            signature : void method(int index)\n
        */
        EventHandle_Int eventButtonSelected;

    protected:
        void onButtonClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::Widget* mTextBox;
        MyGUI::TextBox* mText;
        MyGUI::Widget* mButtonBar;
        std::vector<MyGUI::Button*> mButtons;
        size_t mControllerFocus = 0;
    };

    // Lets the player choose between 3 ways of creating a class
    class ClassChoiceDialog : public WindowModal
    {
    public:
        // Corresponds to the buttons that can be clicked
        enum ClassChoice
        {
            Class_Generate = 0,
            Class_Pick = 1,
            Class_Create = 2,
            Class_Back = 3
        };
        ClassChoiceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~ClassChoiceDialog() override;

        void onFrame(float duration) override;

        bool exit() override { return false; }

        typedef MyGUI::delegates::MultiDelegate<int> EventHandle_Int;
        EventHandle_Int eventButtonSelected;

    protected:
        void onButtonClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::ImageBox* mAvatarPreviewImage = nullptr;
        std::vector<MyGUI::Button*> mButtons;
        AvatarPreviewController mAvatarPreviewController;
        std::unique_ptr<MWRender::RaceSelectionPreview> mAvatarPreview;
        std::unique_ptr<MyGUI::ITexture> mAvatarPreviewTexture;
        size_t mControllerFocus = 0;
    };

    class GenerateClassResultDialog : public WindowModal
    {
    public:
        GenerateClassResultDialog();

        void setClassId(const ESM::RefId& classId);

        bool exit() override { return false; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Back button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventBack;

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        bool mOkButtonFocus = true;

    private:
        MyGUI::ImageBox* mClassImage;
        MyGUI::TextBox* mClassName;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mOkButton;

        ESM::RefId mCurrentClassId;
    };

    class PickClassDialog : public WindowModal
    {
    public:
        PickClassDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~PickClassDialog() override;

        const ESM::RefId& getClassId() const { return mCurrentClassId; }
        void setClassId(const ESM::RefId& classId);

        void setNextButtonShow(bool shown);
        void onOpen() override;
        void onFrame(float duration) override;

        bool exit() override { return false; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Back button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventBack;

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onSelectClass(MyGUI::ListBox* sender, size_t index);
        void onAccept(MyGUI::ListBox* sender, size_t index);

        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);

    private:
        void updateClasses();
        void updateStats();

        MyGUI::ImageBox* mClassImage;
        MyGUI::ImageBox* mAvatarPreviewImage;
        MyGUI::ListBox* mClassList;
        MyGUI::TextBox* mSpecializationName;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mOkButton;
        Widgets::MWAttributePtr mFavoriteAttribute[2];
        Widgets::MWSkillPtr mMajorSkill[5];
        Widgets::MWSkillPtr mMinorSkill[5];

        ESM::RefId mCurrentClassId;
        AvatarPreviewController mAvatarPreviewController;
        std::unique_ptr<MWRender::RaceSelectionPreview> mAvatarPreview;
        std::unique_ptr<MyGUI::ITexture> mAvatarPreviewTexture;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };

    class SelectSpecializationDialog : public WindowModal
    {
    public:
        SelectSpecializationDialog();
        ~SelectSpecializationDialog();

        bool exit() override;

        ESM::Class::Specialization getSpecializationId() const { return mSpecializationId; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Cancel button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventCancel;

        /** Event : Dialog finished, specialization selected.\n
            signature : void method()\n
        */
        EventHandle_Void eventItemSelected;

    protected:
        void onSpecializationClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::TextBox *mSpecialization0, *mSpecialization1, *mSpecialization2;

        ESM::Class::Specialization mSpecializationId;
    };

    class SelectAttributeDialog : public WindowModal
    {
    public:
        SelectAttributeDialog();
        ~SelectAttributeDialog() override = default;

        bool exit() override;

        ESM::RefId getAttributeId() const { return mAttributeId; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Cancel button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventCancel;

        /** Event : Dialog finished, attribute selected.\n
            signature : void method()\n
        */
        EventHandle_Void eventItemSelected;

    protected:
        void onAttributeClicked(Widgets::MWAttributePtr sender);
        void onCancelClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;
        std::vector<Widgets::MWAttribute*> mAttributeButtons;

    private:
        ESM::RefId mAttributeId;
    };

    class SelectSkillDialog : public WindowModal
    {
    public:
        SelectSkillDialog();
        ~SelectSkillDialog();

        bool exit() override;

        ESM::RefId getSkillId() const { return mSkillId; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Cancel button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventCancel;

        /** Event : Dialog finished, skill selected.\n
            signature : void method()\n
        */
        EventHandle_Void eventItemSelected;

    protected:
        void onSkillClicked(Widgets::MWSkillPtr sender);
        void onCancelClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;
        std::vector<Widgets::MWSkill*> mSkillButtons;

    private:
        ESM::RefId mSkillId;
        std::array<size_t, 3> mNumSkillsPerSpecialization{};

        void selectNextColumn(int direction);
    };

    class DescriptionDialog : public WindowModal
    {
    public:
        DescriptionDialog();
        ~DescriptionDialog();

        std::string getTextInput() const { return mTextEdit->getCaption(); }
        void setTextInput(const std::string& text) { mTextEdit->setCaption(text); }

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onOkClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::EditBox* mTextEdit;
    };

    class CreateClassDialog : public WindowModal
    {
    public:
        CreateClassDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        virtual ~CreateClassDialog();

        bool exit() override { return false; }

        std::string getName() const;
        std::string getDescription() const;
        ESM::Class::Specialization getSpecializationId() const;
        std::vector<ESM::RefId> getFavoriteAttributes() const;
        std::vector<ESM::RefId> getMajorSkills() const;
        std::vector<ESM::RefId> getMinorSkills() const;

        void setNextButtonShow(bool shown);
        void onFrame(float duration) override;

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Back button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventBack;

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);

        void onSpecializationClicked(MyGUI::Widget* sender);
        void onSpecializationSelected();
        void onAttributeClicked(Widgets::MWAttributePtr sender);
        void onAttributeSelected();
        void onSkillClicked(Widgets::MWSkillPtr sender);
        void onSkillSelected();
        void onDescriptionClicked(MyGUI::Widget* sender);
        void onDescriptionEntered(WindowBase* parWindow);
        void onDialogCancel();

        void setSpecialization(int id);

        void update();

    private:
        MyGUI::ImageBox* mAvatarPreviewImage;
        MyGUI::EditBox* mEditName;
        MyGUI::TextBox* mSpecializationName;
        std::vector<MyGUI::Button*> mButtons;
        Widgets::MWAttributePtr mFavoriteAttribute0, mFavoriteAttribute1;
        std::array<Widgets::MWSkillPtr, 5> mMajorSkill;
        std::array<Widgets::MWSkillPtr, 5> mMinorSkill;
        std::vector<Widgets::MWSkillPtr> mSkills;
        std::string mDescription;

        std::unique_ptr<SelectSpecializationDialog> mSpecDialog;
        std::unique_ptr<SelectAttributeDialog> mAttribDialog;
        std::unique_ptr<SelectSkillDialog> mSkillDialog;
        std::unique_ptr<DescriptionDialog> mDescDialog;

        ESM::Class::Specialization mSpecializationId;
        AvatarPreviewController mAvatarPreviewController;
        std::unique_ptr<MWRender::RaceSelectionPreview> mAvatarPreview;
        std::unique_ptr<MyGUI::ITexture> mAvatarPreviewTexture;

        Widgets::MWAttributePtr mAffectedAttribute;
        Widgets::MWSkillPtr mAffectedSkill;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 2;
    };
}
#endif
