#include <components/openmw-mp/TimedLog.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/soundmanager.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwgui/backgroundimage.hpp"
#include "apps/openmw/mwgui/confirmationdialog.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwrender/characterpreview.hpp"
#include "apps/openmw/mwrender/renderingmanager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/settings/settings.hpp>

#include <MyGUI_Button.h>
#include <MyGUI_DataManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Exception.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_TextBox.h>

#include <osg/Texture2D>

#include "GUICharacterSelect.hpp"
#include "../LocalPlayer.hpp"
#include "../Main.hpp"
#include "../Networking.hpp"

using namespace mwmp;

namespace
{
    constexpr const char* characterSelectLayout = "characterselect/communitymp_character_select.layout";
    constexpr const char* characterSelectResources = "characterselect/communitymp_character_select.xml";
    constexpr const char* characterSelectPortraitTexture
        = "mygui\\characterselect\\textures\\communitymp_character_portrait.png";
    constexpr const char* lobbyBackgroundLayer = "Scene";
    constexpr const char* defaultLobbyBackgroundTexture = "mygui\\login\\textures\\communitymp-causeway.jpg";
    constexpr const char* defaultLobbyBackgroundSlides
        = "mygui\\login\\textures\\communitymp-causeway.jpg;"
          "mygui\\login\\textures\\communitymp-gathering.jpg;"
          "mygui\\login\\textures\\communitymp-server-hall.jpg;"
          "mygui\\login\\textures\\communitymp-ashlands-hero.jpg";
    constexpr const char* defaultLobbyAtmosphereOverlayTexture
        = "mygui\\login\\textures\\communitymp_login_atmosphere.png";
    constexpr const char* defaultLobbyLogoTexture = "mygui\\login\\textures\\communitymp-logo.png";
    constexpr const char* defaultLobbyMusicTrack = "music/communitymp/nightinthedesertmix.ogg";
    constexpr float defaultLobbySlideSeconds = 6.f;
    constexpr float rosterPreviewMaxZoom = 0.35f;
    constexpr float rosterPreviewMaxVerticalFocus = 18.f;
    constexpr int actionButtonTop = 1;
    constexpr int actionButtonHeight = 30;
    constexpr int actionButtonFullWidth = 236;
    constexpr int actionDeleteWidth = 74;
    constexpr int actionButtonGap = 8;
    constexpr int actionSelectCompactWidth = actionButtonFullWidth - actionDeleteWidth - actionButtonGap;

    bool customLayoutOverrideDisabled = false;
    bool resourcesLoaded = false;

    struct PreviewMetadata
    {
        std::string raceId;
        std::string gender;
        std::string headId;
        std::string hairId;
    };

    bool guiDataExists(const char* name)
    {
        return MyGUI::DataManager::getInstance().isDataExist(name);
    }

    template <class T>
    T* findOptionalWidget(const MWGui::Layout& layout, std::string_view name)
    {
        try
        {
            T* widget = nullptr;
            layout.getWidget(widget, name);
            return widget;
        }
        catch (const MyGUI::Exception&)
        {
            return nullptr;
        }
    }

    const char* selectCharacterSelectLayout()
    {
        if (!resourcesLoaded && guiDataExists(characterSelectResources))
        {
            MyGUI::ResourceManager::getInstance().load(characterSelectResources);
            resourcesLoaded = true;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Loaded CommunityMP character select resources from %s",
                characterSelectResources);
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Using CommunityMP character select layout %s", characterSelectLayout);
        return characterSelectLayout;
    }

    std::string trim(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());

        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();

        return value;
    }

    std::vector<std::string> parseLobbySlideList(std::string_view value)
    {
        std::vector<std::string> slides;
        std::size_t start = 0;

        while (start < value.size())
        {
            const std::size_t end = value.find_first_of(";,", start);
            std::string slide = trim(std::string(value.substr(start,
                end == std::string_view::npos ? std::string_view::npos : end - start)));
            if (!slide.empty())
                slides.push_back(std::move(slide));
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }

        return slides;
    }

    bool shouldUseSingleLobbyBackground(std::string value)
    {
        value = trim(std::move(value));
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value == "single" || value == "static" || value == "off";
    }

    std::string getLobbyBackgroundTexture()
    {
        try
        {
            std::string texture = Settings::Manager::getString("loginBackground", "General");
            if (!texture.empty())
                return texture;
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby background setting: %s",
                e.what());
        }

        return defaultLobbyBackgroundTexture;
    }

    std::vector<std::string> getLobbyBackgroundSlides()
    {
        try
        {
            const std::string configuredSlides = Settings::Manager::getString("loginBackgroundSlides", "General");
            if (shouldUseSingleLobbyBackground(configuredSlides))
                return { getLobbyBackgroundTexture() };

            std::vector<std::string> slides = parseLobbySlideList(configuredSlides);
            if (!slides.empty())
                return slides;
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby background slides setting: %s",
                e.what());
        }

        std::vector<std::string> slides = parseLobbySlideList(defaultLobbyBackgroundSlides);
        if (slides.empty())
            slides.push_back(getLobbyBackgroundTexture());
        return slides;
    }

    bool getLobbyBackgroundEffectsEnabled()
    {
        try
        {
            return Settings::Manager::getBool("loginBackgroundEffects", "General");
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby background effects setting: %s",
                e.what());
        }

        return true;
    }

    std::string getLobbyAtmosphereOverlayTexture()
    {
        try
        {
            std::string texture = Settings::Manager::getString("loginAtmosphereOverlay", "General");
            if (!texture.empty())
                return texture;
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby atmosphere setting: %s",
                e.what());
        }

        return defaultLobbyAtmosphereOverlayTexture;
    }

    float getLobbySlideSeconds()
    {
        try
        {
            return std::clamp(Settings::Manager::getFloat("loginSlideSeconds", "General"), 3.f, 60.f);
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby slide timing setting: %s",
                e.what());
        }

        return defaultLobbySlideSeconds;
    }

    std::string getLobbyLogoTexture()
    {
        try
        {
            std::string texture = Settings::Manager::getString("loginLogo", "General");
            if (!texture.empty())
                return texture;
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby logo setting: %s", e.what());
        }

        return defaultLobbyLogoTexture;
    }

    std::string getLobbyMusicTrack()
    {
        try
        {
            return Settings::Manager::getString("loginMusic", "General");
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to read CommunityMP lobby music setting: %s", e.what());
        }

        return defaultLobbyMusicTrack;
    }

    std::vector<std::string> splitSlotLabel(const std::string& label)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;

        while (start <= label.size())
        {
            const std::size_t next = label.find(" | ", start);
            if (next == std::string::npos)
            {
                parts.push_back(trim(label.substr(start)));
                break;
            }

            parts.push_back(trim(label.substr(start, next - start)));
            start = next + 3;
        }

        return parts;
    }

    std::vector<std::string> splitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::string line;

        for (char character : text)
        {
            if (character == '\n')
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                lines.push_back(line);
                line.clear();
                continue;
            }

            line += character;
        }

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        lines.push_back(line);
        return lines;
    }

    std::vector<std::string> splitTabs(const std::string& text)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;

        while (start <= text.size())
        {
            const std::size_t next = text.find('\t', start);
            if (next == std::string::npos)
            {
                fields.push_back(text.substr(start));
                break;
            }

            fields.push_back(text.substr(start, next - start));
            start = next + 1;
        }

        return fields;
    }

    std::vector<PreviewMetadata> parsePreviewMetadata(const std::string& metadata)
    {
        std::vector<PreviewMetadata> parsed;
        if (metadata.empty())
            return parsed;

        for (const std::string& line : splitLines(metadata))
        {
            PreviewMetadata entry;
            const std::vector<std::string> fields = splitTabs(line);

            if (fields.size() > 0)
                entry.raceId = trim(fields[0]);
            if (fields.size() > 1)
                entry.gender = trim(fields[1]);
            if (fields.size() > 2)
                entry.headId = trim(fields[2]);
            if (fields.size() > 3)
                entry.hairId = trim(fields[3]);

            parsed.push_back(std::move(entry));
        }

        return parsed;
    }

    std::string stripSlotMarker(std::string label)
    {
        label = trim(std::move(label));
        if (label.rfind("* ", 0) == 0)
            label = trim(label.substr(2));
        return label;
    }

    std::string truncateText(const std::string& value, std::size_t maxLength)
    {
        if (value.size() <= maxLength)
            return value;

        if (maxLength <= 3)
            return value.substr(0, maxLength);

        return value.substr(0, maxLength - 3) + "...";
    }

    std::string makeShortLocation(std::string location)
    {
        const std::size_t coordinates = location.find(" (");
        if (coordinates != std::string::npos)
            location = trim(location.substr(0, coordinates));
        while (!location.empty() && (location.back() == ',' || std::isspace(static_cast<unsigned char>(location.back()))))
            location.pop_back();
        static const std::array<std::pair<std::string_view, std::string_view>, 3> replacements{ {
            { "Census and Excise Office", "Census Office" },
            { "Seyda Neen, Census Office", "Seyda Neen Census" },
            { "Balmora, Guild of Mages", "Balmora Mages Guild" },
        } };

        for (const auto& replacement : replacements)
        {
            const std::size_t start = location.find(replacement.first);
            if (start != std::string::npos)
                location.replace(start, replacement.first.size(), replacement.second);
        }
        return location;
    }

    std::string makeDisplayDescriptor(std::string value)
    {
        for (char& character : value)
        {
            if (character == '_')
                character = ' ';
        }

        value = trim(std::move(value));

        std::string collapsed;
        bool previousSpace = false;
        for (const unsigned char character : value)
        {
            if (std::isspace(character))
            {
                if (!collapsed.empty() && !previousSpace)
                    collapsed += ' ';
                previousSpace = true;
            }
            else
            {
                collapsed += static_cast<char>(character);
                previousSpace = false;
            }
        }

        bool capitalizeNext = true;
        for (char& character : collapsed)
        {
            const unsigned char letter = static_cast<unsigned char>(character);
            if (std::isalpha(letter))
            {
                character = static_cast<char>(capitalizeNext ? std::toupper(letter) : std::tolower(letter));
                capitalizeNext = false;
            }
            else
                capitalizeNext = std::isspace(letter) || character == '-' || character == '/';
        }

        return collapsed;
    }

    std::string makeRowCaption(const GUICharacterSelect::SlotEntry& entry)
    {
        if (entry.createNew)
            return "+ Create Character";

        std::string caption = entry.name;
        if (!entry.level.empty())
            caption += "  " + entry.level;
        return truncateText(caption, 46);
    }

    std::string makeRowSubcaption(const GUICharacterSelect::SlotEntry& entry)
    {
        if (entry.createNew)
            return "Start a new profile";

        std::string caption;
        if (!entry.raceClass.empty())
            caption += makeDisplayDescriptor(entry.raceClass);
        if (!entry.location.empty())
        {
            if (!caption.empty())
                caption += " - ";
            caption += makeShortLocation(entry.location);
        }

        if (caption.empty())
            caption = "Saved character profile";

        return truncateText(caption, 58);
    }

    std::string makeDetailsCaption(const GUICharacterSelect::SlotEntry& entry)
    {
        if (entry.createNew)
            return "New character\nFresh inventory\nFresh quest state";

        std::string details;
        if (!entry.level.empty())
            details += truncateText(entry.level, 24);
        if (!entry.raceClass.empty())
        {
            if (!details.empty())
                details += "\n";
            details += truncateText(makeDisplayDescriptor(entry.raceClass), 28);
        }
        if (!entry.location.empty())
        {
            if (!details.empty())
                details += "\n";
            details += truncateText(makeShortLocation(entry.location), 28);
        }

        if (details.empty())
            details = "Saved character slot.";

        return details;
    }

    std::string makeActionHintCaption(const GUICharacterSelect::SlotEntry& entry)
    {
        if (entry.createNew)
            return "Start a new character.\nAccount name stays separate.";

        std::string hint = "Enter as " + truncateText(entry.name, 18) + ".";
        if (!entry.location.empty())
            hint += "\nNear " + truncateText(makeShortLocation(entry.location), 24) + ".";
        return hint;
    }
}

bool GUICharacterSelect::hasCustomLayoutOverride()
{
    return !customLayoutOverrideDisabled && guiDataExists(characterSelectLayout) && guiDataExists(characterSelectResources);
}

void GUICharacterSelect::disableCustomLayoutOverride()
{
    customLayoutOverrideDisabled = true;
}

GUICharacterSelect::GUICharacterSelect(const std::string& message, const std::vector<std::string>& list,
    const std::string& metadata)
    : WindowModal(selectCharacterSelectLayout())
{
    createBackdrop();
    createLogo();
    startLobbyMusic();
    center();

    getWidget(mMessage, "Message");
    getWidget(mSelectButton, "SelectButton");
    getWidget(mDeleteButton, "DeleteButton");
    getWidget(mUpButton, "UpButton");
    getWidget(mDownButton, "DownButton");
    getWidget(mPortrait, "Portrait");
    getWidget(mPortraitInitial, "PortraitInitial");
    getWidget(mPortraitSubtitle, "PortraitSubtitle");
    getWidget(mCharacterTitle, "CharacterTitle");
    getWidget(mCharacterDetails, "CharacterDetails");
    getWidget(mStatusText, "StatusText");
    mSessionSummary = findOptionalWidget<MyGUI::TextBox>(*this, "SessionSummary");
    mActionHint = findOptionalWidget<MyGUI::TextBox>(*this, "ActionHint");
    mStageHint = findOptionalWidget<MyGUI::TextBox>(*this, "StageHint");

    for (std::size_t i = 0; i < visibleSlotCount; ++i)
    {
        const std::string index = MyGUI::utility::toString(i);
        getWidget(mSlotButtons[i], "Slot" + index);
        getWidget(mSlotTexts[i], "Slot" + index + "Text");
        mSlotSubtexts[i] = findOptionalWidget<MyGUI::TextBox>(*this, "Slot" + index + "Subtext");

        mSlotButtons[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &GUICharacterSelect::slotPressed);
        mSlotButtons[i]->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUICharacterSelect::keyPressed);
    }

    mSelectButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUICharacterSelect::selectPressed);
    mSelectButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUICharacterSelect::keyPressed);
    mDeleteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUICharacterSelect::deletePressed);
    mDeleteButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUICharacterSelect::keyPressed);
    mUpButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUICharacterSelect::scrollUpPressed);
    mUpButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUICharacterSelect::keyPressed);
    mDownButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUICharacterSelect::scrollDownPressed);
    mDownButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUICharacterSelect::keyPressed);

    mMessage->setCaptionWithReplacing(message);
    buildEntries(list, metadata);
    refreshRows();
    refreshPreview();
    syncBackdropVisibility(false);
}

GUICharacterSelect::~GUICharacterSelect()
{
    detachDeleteConfirmationCallbacks();
    stopLobbyMusic();
    destroyLogo();
    destroyBackdrop();
}

void GUICharacterSelect::setVisible(bool visible)
{
    WindowModal::setVisible(visible);
    syncBackdropVisibility(isVisible());
}

void GUICharacterSelect::update(float duration)
{
    updateBackdropAnimation(duration);

    if (mPreview != nullptr)
    {
        mAvatarPreviewController.update(duration);
        mPreviewAngle = mAvatarPreviewController.getAngle();
    }
}

void GUICharacterSelect::buildEntries(const std::vector<std::string>& list, const std::string& metadata)
{
    mEntries.clear();
    const std::vector<PreviewMetadata> previewMetadata = parsePreviewMetadata(metadata);
    std::size_t defaultSelection = 0;
    bool foundDefaultSelection = false;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const std::string rawLabel = trim(list[i]);
        const bool markedSelected = rawLabel.rfind("* ", 0) == 0;
        const std::string label = stripSlotMarker(rawLabel);
        if (label.empty())
            continue;

        SlotEntry entry;
        entry.originalIndex = i;
        entry.rawLabel = label;
        entry.createNew = label.rfind("+", 0) == 0;

        if (entry.createNew)
        {
            entry.name = "Create New Character";
        }
        else
        {
            const std::vector<std::string> parts = splitSlotLabel(label);
            entry.name = parts.empty() || parts[0].empty() ? "Saved Character" : parts[0];
            if (parts.size() > 1)
                entry.level = parts[1];
            if (parts.size() > 2)
                entry.raceClass = parts[2];
            if (parts.size() > 3)
                entry.location = parts[3];

            if (i < previewMetadata.size())
            {
                const PreviewMetadata& preview = previewMetadata[i];
                entry.raceId = preview.raceId;
                entry.headId = preview.headId;
                entry.hairId = preview.hairId;
                entry.male = preview.gender != "0";
                entry.hasPreviewMetadata = !entry.raceId.empty() && !entry.headId.empty();
            }
        }

        if (markedSelected && !foundDefaultSelection)
        {
            defaultSelection = mEntries.size();
            foundDefaultSelection = true;
        }

        mEntries.push_back(std::move(entry));
    }

    mSelectedIndex = mEntries.empty() ? 0 : defaultSelection;
    mScrollOffset = 0;

    if (mSelectedIndex >= visibleSlotCount)
        mScrollOffset = mSelectedIndex - visibleSlotCount + 1;
}

void GUICharacterSelect::refreshRows()
{
    const MyGUI::Colour selectedText(1.0f, 0.84f, 0.36f);
    const MyGUI::Colour normalText(0.88f, 0.84f, 0.72f);
    const MyGUI::Colour createText(0.48f, 0.72f, 1.0f);

    for (std::size_t slot = 0; slot < visibleSlotCount; ++slot)
    {
        const std::size_t entryIndex = mScrollOffset + slot;
        const bool hasEntry = entryIndex < mEntries.size();

        mSlotButtons[slot]->setVisible(hasEntry);
        mSlotTexts[slot]->setVisible(hasEntry);
        if (mSlotSubtexts[slot] != nullptr)
            mSlotSubtexts[slot]->setVisible(hasEntry);

        if (!hasEntry)
            continue;

        const SlotEntry& entry = mEntries[entryIndex];
        const bool selected = entryIndex == mSelectedIndex;
        mSlotButtons[slot]->setStateSelected(selected);
        mSlotTexts[slot]->setCaption(MyGUI::UString(makeRowCaption(entry)));
        if (mSlotSubtexts[slot] != nullptr)
            mSlotSubtexts[slot]->setCaption(MyGUI::UString(makeRowSubcaption(entry)));

        if (selected)
        {
            mSlotTexts[slot]->setTextColour(selectedText);
            if (mSlotSubtexts[slot] != nullptr)
                mSlotSubtexts[slot]->setTextColour(MyGUI::Colour(0.82f, 0.88f, 0.82f));
        }
        else if (entry.createNew)
        {
            mSlotTexts[slot]->setTextColour(createText);
            if (mSlotSubtexts[slot] != nullptr)
                mSlotSubtexts[slot]->setTextColour(MyGUI::Colour(0.45f, 0.65f, 0.78f));
        }
        else
        {
            mSlotTexts[slot]->setTextColour(normalText);
            if (mSlotSubtexts[slot] != nullptr)
                mSlotSubtexts[slot]->setTextColour(MyGUI::Colour(0.52f, 0.63f, 0.65f));
        }
    }

    const bool canScrollUp = mScrollOffset > 0;
    const bool canScrollDown = mScrollOffset + visibleSlotCount < mEntries.size();
    mUpButton->setVisible(canScrollUp);
    mUpButton->setEnabled(canScrollUp);
    mDownButton->setVisible(canScrollDown);
    mDownButton->setEnabled(canScrollDown);
    const bool canDeleteSelection = !mEntries.empty() && !mEntries[mSelectedIndex].createNew;
    mSelectButton->setEnabled(!mEntries.empty());
    mDeleteButton->setVisible(canDeleteSelection);
    mDeleteButton->setEnabled(canDeleteSelection);
    if (canDeleteSelection)
        mSelectButton->setCoord(actionDeleteWidth + actionButtonGap, actionButtonTop, actionSelectCompactWidth,
            actionButtonHeight);
    else
        mSelectButton->setCoord(0, actionButtonTop, actionButtonFullWidth, actionButtonHeight);

    if (mEntries.empty())
    {
        mStatusText->setCaption("No character slots were sent by the server.");
        mSelectButton->setCaption("Unavailable");
        mDeleteButton->setVisible(false);
        mDeleteButton->setEnabled(false);
        return;
    }

    std::string status = "Slot " + MyGUI::utility::toString(mSelectedIndex + 1) + " of "
        + MyGUI::utility::toString(mEntries.size());
    if (!mEntries[mSelectedIndex].location.empty())
        status += " - " + truncateText(makeShortLocation(mEntries[mSelectedIndex].location), 30);

    mStatusText->setCaption(MyGUI::UString(status));
    mSelectButton->setCaption(mEntries[mSelectedIndex].createNew ? "Create Character" : "Enter World");
}

void GUICharacterSelect::refreshPreview()
{
    mPortraitInitial->setCaption("");
    mPortraitInitial->setVisible(false);
    mPortraitSubtitle->setCaption("");
    mPortraitSubtitle->setVisible(false);

    if (mEntries.empty())
    {
        mCharacterTitle->setCaption("No Characters");
        mCharacterDetails->setCaption("The server did not send any character slots for this account.");
        if (mActionHint != nullptr)
            mActionHint->setCaption("Reconnect or choose another account to receive character data.");
        if (mStageHint != nullptr)
            mStageHint->setCaption("Waiting for roster data");
        showStaticPortrait();
        return;
    }

    const SlotEntry& entry = mEntries[mSelectedIndex];
    mCharacterTitle->setCaption(MyGUI::UString(truncateText(entry.name, 24)));
    mCharacterDetails->setCaption(MyGUI::UString(makeDetailsCaption(entry)));
    if (mActionHint != nullptr)
        mActionHint->setCaption(MyGUI::UString(makeActionHintCaption(entry)));
    if (mSessionSummary != nullptr)
    {
        if (entry.createNew)
            mSessionSummary->setCaption(
                "New profile.\nInventory, journal,\nquests and topics start fresh.");
        else
            mSessionSummary->setCaption("Inventory, journal,\nquests and topics stay\nwith this character.");
    }
    if (mStageHint != nullptr)
        mStageHint->setCaption(entry.createNew ? "New character setup" : "Selected avatar");

    const bool renderedPortrait = !entry.createNew && showRenderedPortrait(entry);

    if (!renderedPortrait)
        showStaticPortrait();
}

void GUICharacterSelect::showStaticPortrait()
{
    if (mPortrait == nullptr)
        return;

    mRenderedPreviewValid = false;
    mAvatarPreviewController.configureInspectionLimits(rosterPreviewMaxZoom, rosterPreviewMaxVerticalFocus);
    mAvatarPreviewController.resetFraming(0.f);
    mPortrait->setRenderItemTexture(nullptr);
    mAvatarPreviewController.bind(mPortrait, nullptr);
    mPortrait->setImageTexture(characterSelectPortraitTexture);
}

bool GUICharacterSelect::showRenderedPortrait(const SlotEntry& entry)
{
    if (mPortrait == nullptr || !entry.hasPreviewMetadata)
        return false;

    try
    {
        if (mPreview == nullptr)
        {
            MWRender::RenderingManager* renderingManager
                = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (renderingManager == nullptr || renderingManager->getRootNode() == nullptr)
                return false;

            mPreview = std::make_unique<MWRender::RaceSelectionPreview>(
                renderingManager->getRootNode(), MWBase::Environment::get().getResourceSystem(),
                MWRender::RaceSelectionPreview::PreviewMode::Body);
        }

        ESM::NPC record = mPreview->getPrototype();
        record.mName = entry.name;
        record.mRace = ESM::RefId::stringRefId(entry.raceId);
        record.setIsMale(entry.male);
        record.mHead = ESM::RefId::stringRefId(entry.headId);
        record.mHair = ESM::RefId::stringRefId(entry.hairId);

        const bool previewEntryChanged = !mRenderedPreviewValid || mRenderedPreviewIndex != mSelectedIndex;
        const float previewAngle = previewEntryChanged ? 0.f : mAvatarPreviewController.getAngle();
        mPreview->setPrototype(record);
        mAvatarPreviewController.configureInspectionLimits(rosterPreviewMaxZoom, rosterPreviewMaxVerticalFocus);
        mAvatarPreviewController.bind(mPortrait, mPreview.get());
        if (previewEntryChanged)
            mAvatarPreviewController.resetFraming(previewAngle);
        else
            mAvatarPreviewController.setAngle(previewAngle);
        mPreviewAngle = previewAngle;
        mRenderedPreviewIndex = mSelectedIndex;
        mRenderedPreviewValid = true;

        mPortrait->setRenderItemTexture(nullptr);
        mPreviewTexture
            = std::make_unique<MyGUIPlatform::OSGTexture>(mPreview->getTexture(), mPreview->getTextureStateSet());
        mPortrait->setRenderItemTexture(mPreviewTexture.get());
        mPortrait->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to render character select preview for %s: %s",
            entry.name.c_str(), e.what());
        mPortrait->setRenderItemTexture(nullptr);
        mPreview.reset();
        mPreviewTexture.reset();
        return false;
    }
}

void GUICharacterSelect::createBackdrop()
{
    mBackgroundSlides = getLobbyBackgroundSlides();
    mBackgroundEffects = getLobbyBackgroundEffectsEnabled();
    mSlideDuration = getLobbySlideSeconds();

    if (mBackgroundSlides.empty())
        return;

    try
    {
        mBackground = MyGUI::Gui::getInstance().createWidgetReal<MWGui::BackgroundImage>(
            "ImageBox", 0, 0, 1, 1, MyGUI::Align::Default, lobbyBackgroundLayer);
        mBackground->setProperty("NeedMouse", "false");
        mBackground->setBackgroundImage(mBackgroundSlides.front(), false, true);

        if (mBackgroundEffects && mBackgroundSlides.size() > 1)
        {
            mBackgroundNext = MyGUI::Gui::getInstance().createWidgetReal<MWGui::BackgroundImage>(
                "ImageBox", 0, 0, 1, 1, MyGUI::Align::Default, lobbyBackgroundLayer);
            mBackgroundNext->setProperty("NeedMouse", "false");
            mBackgroundNext->setBackgroundImage(mBackgroundSlides[1], false, true);
            mBackgroundNext->setAlpha(0.f);
        }

        const std::string atmosphereTexture = getLobbyAtmosphereOverlayTexture();
        if (mBackgroundEffects && !atmosphereTexture.empty())
        {
            mAtmosphereOverlay = MyGUI::Gui::getInstance().createWidgetReal<MyGUI::ImageBox>(
                "ImageBox", 0, 0, 1, 1, MyGUI::Align::Stretch, lobbyBackgroundLayer);
            mAtmosphereOverlay->setProperty("NeedMouse", "false");
            mAtmosphereOverlay->setImageTexture(atmosphereTexture);
            mAtmosphereOverlay->setAlpha(0.32f);
        }

        updateBackdropAnimation(0.f);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to create CommunityMP character lobby background: %s",
            e.what());
        destroyBackdrop();
    }
}

void GUICharacterSelect::destroyBackdrop()
{
    if (mBackground != nullptr)
    {
        MyGUI::Gui::getInstance().destroyWidget(mBackground);
        mBackground = nullptr;
    }

    if (mBackgroundNext != nullptr)
    {
        MyGUI::Gui::getInstance().destroyWidget(mBackgroundNext);
        mBackgroundNext = nullptr;
    }

    if (mAtmosphereOverlay != nullptr)
    {
        MyGUI::Gui::getInstance().destroyWidget(mAtmosphereOverlay);
        mAtmosphereOverlay = nullptr;
    }
}

void GUICharacterSelect::createLogo()
{
    const std::string texture = getLobbyLogoTexture();
    if (texture.empty())
        return;

    try
    {
        mLogo = MyGUI::Gui::getInstance().createWidgetReal<MyGUI::ImageBox>(
            "ImageBox", 0.25f, 0.035f, 0.50f, 0.105f, MyGUI::Align::Default, "Windows");
        mLogo->setProperty("NeedMouse", "false");
        mLogo->setImageTexture(texture);
        mLogo->setAlpha(0.88f);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(
            TimedLog::LOG_WARN, "Failed to create CommunityMP character lobby logo %s: %s", texture.c_str(), e.what());
        destroyLogo();
    }
}

void GUICharacterSelect::destroyLogo()
{
    if (mLogo != nullptr)
    {
        MyGUI::Gui::getInstance().destroyWidget(mLogo);
        mLogo = nullptr;
    }
}

void GUICharacterSelect::startLobbyMusic()
{
    const std::string music = getLobbyMusicTrack();
    if (music.empty())
        return;

    try
    {
        auto soundManager = MWBase::Environment::get().getSoundManager();
        soundManager->stopMusic();
        soundManager->streamMusic(VFS::Path::Normalized(music), MWSound::MusicType::MWScript, 0.f);
        mLobbyMusicStarted = true;
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(
            TimedLog::LOG_WARN, "Failed to start CommunityMP character lobby music %s: %s", music.c_str(), e.what());
    }
}

void GUICharacterSelect::stopLobbyMusic()
{
    if (!mLobbyMusicStarted)
        return;

    try
    {
        MWBase::Environment::get().getSoundManager()->stopMusic();
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to stop CommunityMP character lobby music: %s", e.what());
    }

    mLobbyMusicStarted = false;
}

void GUICharacterSelect::syncBackdropVisibility(bool visible)
{
    if (mBackground != nullptr)
        mBackground->setVisible(visible);
    if (mBackgroundNext != nullptr)
        mBackgroundNext->setVisible(visible);
    if (mAtmosphereOverlay != nullptr)
        mAtmosphereOverlay->setVisible(visible);
    if (mLogo != nullptr)
        mLogo->setVisible(visible);
}

void GUICharacterSelect::updateBackdropAnimation(float duration)
{
    if (mBackground == nullptr || mBackgroundSlides.empty())
        return;

    const MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
    if (viewSize.width <= 0 || viewSize.height <= 0)
        return;

    if (!mBackgroundEffects)
    {
        mBackground->setCoord(MyGUI::IntCoord(0, 0, viewSize.width, viewSize.height));
        return;
    }

    mBackgroundTime += std::max(0.f, duration);
    if (mBackgroundSlides.size() > 1)
    {
        while (mBackgroundTime >= mSlideDuration)
        {
            mBackgroundTime -= mSlideDuration;
            mCurrentBackgroundSlide = (mCurrentBackgroundSlide + 1) % mBackgroundSlides.size();
            mBackground->setBackgroundImage(mBackgroundSlides[mCurrentBackgroundSlide], false, true);
            if (mBackgroundNext != nullptr)
            {
                const std::size_t nextSlide = (mCurrentBackgroundSlide + 1) % mBackgroundSlides.size();
                mBackgroundNext->setBackgroundImage(mBackgroundSlides[nextSlide], false, true);
            }
        }
    }

    const float phase = mSlideDuration > 0.f ? std::clamp(mBackgroundTime / mSlideDuration, 0.f, 1.f) : 0.f;
    const float crossFadeSeconds = std::min(2.75f, mSlideDuration * 0.42f);
    const float fade = mBackgroundSlides.size() > 1 && mBackgroundNext != nullptr
        ? std::clamp((mBackgroundTime - (mSlideDuration - crossFadeSeconds)) / crossFadeSeconds, 0.f, 1.f)
        : 0.f;

    setAnimatedBackdropCoord(mBackground, mCurrentBackgroundSlide, phase, viewSize, 0.f);
    mBackground->setAlpha(1.f - fade * 0.9f);

    if (mBackgroundNext != nullptr)
    {
        const std::size_t nextSlide = (mCurrentBackgroundSlide + 1) % mBackgroundSlides.size();
        setAnimatedBackdropCoord(mBackgroundNext, nextSlide, fade * 0.28f, viewSize, 0.012f);
        mBackgroundNext->setAlpha(fade);
    }

    if (mAtmosphereOverlay != nullptr)
    {
        mAtmosphereOverlay->setCoord(0, 0, viewSize.width, viewSize.height);
        const float shimmer = 0.5f + 0.5f * std::sin(mBackgroundTime * 0.65f);
        mAtmosphereOverlay->setAlpha(0.22f + shimmer * 0.10f);
    }
}

void GUICharacterSelect::setAnimatedBackdropCoord(
    MyGUI::Widget* widget, std::size_t slideIndex, float phase, const MyGUI::IntSize& viewSize, float extraZoom)
{
    if (widget == nullptr)
        return;

    phase = std::clamp(phase, 0.f, 1.f);
    const float eased = phase * phase * (3.f - 2.f * phase);
    const float zoom = 1.12f + eased * 0.08f + extraZoom;
    const int width = static_cast<int>(std::ceil(viewSize.width * zoom));
    const int height = static_cast<int>(std::ceil(viewSize.height * zoom));
    const int overflowX = std::max(0, width - viewSize.width);
    const int overflowY = std::max(0, height - viewSize.height);

    constexpr float xStart[] = { 0.06f, 0.88f, 0.34f, 0.78f };
    constexpr float xEnd[] = { 0.80f, 0.14f, 0.72f, 0.20f };
    constexpr float yStart[] = { 0.18f, 0.08f, 0.70f, 0.34f };
    constexpr float yEnd[] = { 0.54f, 0.58f, 0.20f, 0.66f };
    const std::size_t motion = slideIndex % std::size(xStart);
    const float x = xStart[motion] + (xEnd[motion] - xStart[motion]) * eased;
    const float y = yStart[motion] + (yEnd[motion] - yStart[motion]) * eased;

    widget->setCoord(-static_cast<int>(overflowX * x), -static_cast<int>(overflowY * y), width, height);
}

void GUICharacterSelect::selectEntry(std::size_t index)
{
    if (mEntries.empty())
        return;

    mSelectedIndex = std::min(index, mEntries.size() - 1);

    if (mSelectedIndex < mScrollOffset)
        mScrollOffset = mSelectedIndex;
    else if (mSelectedIndex >= mScrollOffset + visibleSlotCount)
        mScrollOffset = mSelectedIndex - visibleSlotCount + 1;

    refreshRows();
    refreshPreview();
}

void GUICharacterSelect::selectVisibleSlot(std::size_t visibleSlot)
{
    selectEntry(mScrollOffset + visibleSlot);
}

void GUICharacterSelect::scrollBy(int delta)
{
    if (mEntries.empty())
        return;

    const int requested = static_cast<int>(mSelectedIndex) + delta;
    const int clamped = std::max(0, std::min(requested, static_cast<int>(mEntries.size() - 1)));
    selectEntry(static_cast<std::size_t>(clamped));
}

void GUICharacterSelect::submitSelection()
{
    if (mSubmitted || mEntries.empty() || mSelectedIndex >= mEntries.size())
        return;

    submitResponse(MyGUI::utility::toString(mEntries[mSelectedIndex].originalIndex));

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Selected character slot id: %d",
        static_cast<int>(mEntries[mSelectedIndex].originalIndex));
    LOG_APPEND(TimedLog::LOG_VERBOSE, "name of item: '%s'", mEntries[mSelectedIndex].rawLabel.c_str());
}

void GUICharacterSelect::confirmDeleteSelection()
{
    if (mSubmitted || mEntries.empty() || mSelectedIndex >= mEntries.size() || mEntries[mSelectedIndex].createNew)
        return;

    detachDeleteConfirmationCallbacks();
    MWGui::ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
    dialog->askForConfirmation("Delete character \"" + truncateText(mEntries[mSelectedIndex].name, 36)
        + "\"?\nThis removes it from this account.");
    dialog->eventOkClicked.clear();
    dialog->eventOkClicked += MyGUI::newDelegate(this, &GUICharacterSelect::deleteSelectionConfirmed);
    dialog->eventCancelClicked.clear();
    dialog->eventCancelClicked += MyGUI::newDelegate(this, &GUICharacterSelect::deleteSelectionCanceled);
    mDeleteConfirmationOpen = true;
}

void GUICharacterSelect::detachDeleteConfirmationCallbacks()
{
    MWGui::ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
    if (dialog == nullptr)
        return;

    dialog->eventOkClicked -= MyGUI::newDelegate(this, &GUICharacterSelect::deleteSelectionConfirmed);
    dialog->eventCancelClicked -= MyGUI::newDelegate(this, &GUICharacterSelect::deleteSelectionCanceled);
}

void GUICharacterSelect::deleteSelectionConfirmed()
{
    mDeleteConfirmationOpen = false;
    detachDeleteConfirmationCallbacks();

    if (mSubmitted || mEntries.empty() || mSelectedIndex >= mEntries.size() || mEntries[mSelectedIndex].createNew)
        return;

    submitResponse("delete:" + MyGUI::utility::toString(mEntries[mSelectedIndex].originalIndex));

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Requested deletion of character slot id: %d",
        static_cast<int>(mEntries[mSelectedIndex].originalIndex));
    LOG_APPEND(TimedLog::LOG_VERBOSE, "name of item: '%s'", mEntries[mSelectedIndex].rawLabel.c_str());
}

void GUICharacterSelect::deleteSelectionCanceled()
{
    mDeleteConfirmationOpen = false;
    detachDeleteConfirmationCallbacks();
    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mDeleteButton);
}

void GUICharacterSelect::submitResponse(const std::string& data)
{
    mSubmitted = true;
    mSelectButton->setEnabled(false);
    mDeleteButton->setEnabled(false);
    mUpButton->setEnabled(false);
    mDownButton->setEnabled(false);
    for (MyGUI::Button* button : mSlotButtons)
        button->setEnabled(false);

    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->guiMessageBox.data = data;

    PlayerPacket* playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(localPlayer);
    playerPacket->Send();
}

void GUICharacterSelect::slotPressed(MyGUI::Widget* widget)
{
    for (std::size_t slot = 0; slot < visibleSlotCount; ++slot)
    {
        if (widget == mSlotButtons[slot])
        {
            selectVisibleSlot(slot);
            return;
        }
    }
}

void GUICharacterSelect::selectPressed(MyGUI::Widget* /*widget*/)
{
    submitSelection();
}

void GUICharacterSelect::deletePressed(MyGUI::Widget* /*widget*/)
{
    confirmDeleteSelection();
}

void GUICharacterSelect::scrollUpPressed(MyGUI::Widget* /*widget*/)
{
    scrollBy(-1);
}

void GUICharacterSelect::scrollDownPressed(MyGUI::Widget* /*widget*/)
{
    scrollBy(1);
}

void GUICharacterSelect::keyPressed(MyGUI::Widget* widget, MyGUI::KeyCode key, MyGUI::Char /*character*/)
{
    if (mDeleteConfirmationOpen)
        return;

    if (key == MyGUI::KeyCode::ArrowUp)
        scrollBy(-1);
    else if (key == MyGUI::KeyCode::ArrowDown)
        scrollBy(1);
    else if (key == MyGUI::KeyCode::PageUp)
        scrollBy(-static_cast<int>(visibleSlotCount));
    else if (key == MyGUI::KeyCode::PageDown)
        scrollBy(static_cast<int>(visibleSlotCount));
    else if (key == MyGUI::KeyCode::Home)
        selectEntry(0);
    else if (key == MyGUI::KeyCode::End && !mEntries.empty())
        selectEntry(mEntries.size() - 1);
    else if (key == MyGUI::KeyCode::Delete)
        confirmDeleteSelection();
    else if (key == MyGUI::KeyCode::Return || key == MyGUI::KeyCode::NumpadEnter || key == MyGUI::KeyCode::Space)
    {
        if (widget == mDeleteButton)
            confirmDeleteSelection();
        else
            submitSelection();
    }
}

void GUICharacterSelect::onOpen()
{
    WindowModal::onOpen();
    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(getDefaultKeyFocus());
}

MyGUI::Widget* GUICharacterSelect::getDefaultKeyFocus()
{
    if (!mEntries.empty() && mSelectedIndex >= mScrollOffset
        && mSelectedIndex < mScrollOffset + mSlotButtons.size())
        return mSlotButtons[mSelectedIndex - mScrollOffset];

    return mSelectButton;
}
