#ifndef MWGUI_BIRTH_H
#define MWGUI_BIRTH_H

#include <components/esm/refid.hpp>

#include "avatarpreview.hpp"
#include "windowbase.hpp"

#include <memory>

namespace MWRender
{
    class RaceSelectionPreview;
}

namespace MyGUI
{
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
    class BirthDialog : public WindowModal
    {
    public:
        BirthDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~BirthDialog() override;

        enum Gender
        {
            GM_Male,
            GM_Female
        };

        const ESM::RefId& getBirthId() const { return mCurrentBirthId; }
        void setBirthId(const ESM::RefId& raceId);

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
        void onSelectBirth(MyGUI::ListBox* sender, size_t index);

        void onAccept(MyGUI::ListBox* sender, size_t index);
        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);

    private:
        void updateBirths();
        void updateSpells();

        MyGUI::ListBox* mBirthList;
        MyGUI::ScrollView* mSpellArea;
        MyGUI::ImageBox* mBirthImage;
        MyGUI::ImageBox* mAvatarPreviewImage;
        std::vector<MyGUI::Widget*> mSpellItems;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mOkButton;

        ESM::RefId mCurrentBirthId;
        AvatarPreviewController mAvatarPreviewController;
        std::unique_ptr<MWRender::RaceSelectionPreview> mAvatarPreview;
        std::unique_ptr<MyGUI::ITexture> mAvatarPreviewTexture;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };
}
#endif
