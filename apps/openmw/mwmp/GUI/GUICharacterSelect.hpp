#ifndef OPENMW_GUICHARACTERSELECT_HPP
#define OPENMW_GUICHARACTERSELECT_HPP

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "apps/openmw/mwgui/avatarpreview.hpp"
#include "apps/openmw/mwgui/windowbase.hpp"

namespace MyGUIPlatform
{
    class OSGTexture;
}

namespace MWGui
{
    class BackgroundImage;
}

namespace MyGUI
{
    class Button;
    class EditBox;
    class ImageBox;
    class TextBox;
    class Widget;
}

namespace MWRender
{
    class RaceSelectionPreview;
}

namespace mwmp
{
    class GUICharacterSelect : public MWGui::WindowModal
    {
    public:
        GUICharacterSelect(const std::string& message, const std::vector<std::string>& list,
            const std::string& metadata);
        ~GUICharacterSelect() override;

        static bool hasCustomLayoutOverride();
        static void disableCustomLayoutOverride();

        void setVisible(bool visible) override;
        void update(float duration);
        bool isSubmitted() const { return mSubmitted; }
        void onOpen() override;
        MyGUI::Widget* getDefaultKeyFocus() override;

        struct SlotEntry
        {
            std::size_t originalIndex = 0;
            bool createNew = false;
            std::string rawLabel;
            std::string name;
            std::string level;
            std::string raceClass;
            std::string location;
            std::string raceId;
            std::string headId;
            std::string hairId;
            bool male = true;
            bool hasPreviewMetadata = false;
        };

    private:
        static constexpr std::size_t visibleSlotCount = 6;

        void buildEntries(const std::vector<std::string>& list, const std::string& metadata);
        void refreshRows();
        void refreshPreview();
        void showStaticPortrait();
        bool showRenderedPortrait(const SlotEntry& entry);
        void createBackdrop();
        void destroyBackdrop();
        void createLogo();
        void destroyLogo();
        void startLobbyMusic();
        void stopLobbyMusic();
        void syncBackdropVisibility(bool visible);
        void updateBackdropAnimation(float duration);
        void setAnimatedBackdropCoord(
            MyGUI::Widget* widget, std::size_t slideIndex, float phase, const MyGUI::IntSize& viewSize, float extraZoom);
        void selectEntry(std::size_t index);
        void selectVisibleSlot(std::size_t visibleSlot);
        void scrollBy(int delta);
        void submitSelection();
        void confirmDeleteSelection();
        void detachDeleteConfirmationCallbacks();
        void deleteSelectionConfirmed();
        void deleteSelectionCanceled();
        void submitResponse(const std::string& data);
        void slotPressed(MyGUI::Widget* widget);
        void selectPressed(MyGUI::Widget* widget);
        void deletePressed(MyGUI::Widget* widget);
        void scrollUpPressed(MyGUI::Widget* widget);
        void scrollDownPressed(MyGUI::Widget* widget);
        void keyPressed(MyGUI::Widget* widget, MyGUI::KeyCode key, MyGUI::Char character);

        std::vector<SlotEntry> mEntries;
        std::size_t mSelectedIndex = 0;
        std::size_t mScrollOffset = 0;

        MyGUI::EditBox* mMessage = nullptr;
        MyGUI::Button* mSelectButton = nullptr;
        MyGUI::Button* mDeleteButton = nullptr;
        MyGUI::Button* mUpButton = nullptr;
        MyGUI::Button* mDownButton = nullptr;
        MyGUI::ImageBox* mPortrait = nullptr;
        MyGUI::TextBox* mPortraitInitial = nullptr;
        MyGUI::TextBox* mPortraitSubtitle = nullptr;
        MyGUI::TextBox* mCharacterTitle = nullptr;
        MyGUI::TextBox* mCharacterDetails = nullptr;
        MyGUI::TextBox* mStatusText = nullptr;
        MyGUI::TextBox* mSessionSummary = nullptr;
        MyGUI::TextBox* mActionHint = nullptr;
        MyGUI::TextBox* mStageHint = nullptr;
        MWGui::BackgroundImage* mBackground = nullptr;
        MWGui::BackgroundImage* mBackgroundNext = nullptr;
        MyGUI::ImageBox* mAtmosphereOverlay = nullptr;
        MyGUI::ImageBox* mLogo = nullptr;
        std::array<MyGUI::Button*, visibleSlotCount> mSlotButtons{};
        std::array<MyGUI::TextBox*, visibleSlotCount> mSlotTexts{};
        std::array<MyGUI::TextBox*, visibleSlotCount> mSlotSubtexts{};
        std::vector<std::string> mBackgroundSlides;
        std::unique_ptr<MWRender::RaceSelectionPreview> mPreview;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mPreviewTexture;
        MWGui::AvatarPreviewController mAvatarPreviewController;
        std::size_t mCurrentBackgroundSlide = 0;
        float mBackgroundTime = 0.f;
        float mSlideDuration = 6.f;
        float mPreviewAngle = 0.f;
        std::size_t mRenderedPreviewIndex = 0;
        bool mBackgroundEffects = true;
        bool mLobbyMusicStarted = false;
        bool mSubmitted = false;
        bool mDeleteConfirmationOpen = false;
        bool mRenderedPreviewValid = false;
    };
}

#endif // OPENMW_GUICHARACTERSELECT_HPP
