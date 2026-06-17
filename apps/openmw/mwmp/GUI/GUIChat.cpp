#include "GUIChat.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <string_view>
#include <MyGUI_DataManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Exception.h>
#include <MyGUI_Button.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Window.h>
#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/inputmanager.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwinput/inputmanagerimp.hpp"
#include <MyGUI_InputManager.h>
#include <SDL_keyboard.h>
#include <components/openmw-mp/TimedLog.hpp>

#include "../Networking.hpp"
#include "../Main.hpp"
#include "../LocalPlayer.hpp"

#include "../GUIController.hpp"


namespace mwmp
{
    namespace
    {
        constexpr const char* defaultChatLayout = "tes3mp_chat.layout";
        constexpr const char* communityCustomChatLayout = "chat/communitymp_chat.layout";
        constexpr const char* communityCustomChatResources = "chat/communitymp_chat.xml";
        constexpr const char* legacyCustomChatLayout = "tes3mp_chat_custom.layout";
        constexpr const char* legacyCustomChatResources = "tes3mp_chat_custom.xml";
        constexpr const char* tabApplyTextColourKey = "CommunityMP_TabApplyTextColour";
        constexpr const char* tabCaptionKey = "CommunityMP_TabCaption";
        constexpr const char* tabFontKey = "CommunityMP_TabFont";
        constexpr const char* tabNormalColourKey = "CommunityMP_TabNormalColour";
        constexpr const char* tabSelectedCaptionKey = "CommunityMP_TabSelectedCaption";
        constexpr const char* tabSelectedColourKey = "CommunityMP_TabSelectedColour";
        constexpr const char* tabSelectedPrefixKey = "CommunityMP_TabSelectedPrefix";
        constexpr const char* tabSelectedSuffixKey = "CommunityMP_TabSelectedSuffix";
        constexpr const char* tabUseSelectedStateKey = "CommunityMP_TabUseSelectedState";
        constexpr std::size_t maxChatHistory = 400;
        bool customLayoutOverrideDisabled = false;
        bool customResourcesChecked = false;

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

        struct ChannelDisplay
        {
            const char* title;
            const char* mode;
            const char* input;
        };

        ChannelDisplay getChannelDisplay(GUIChat::ChatChannel channel)
        {
            switch (channel)
            {
                case GUIChat::Channel_Server:
                    return { "Server", "System stream", "CMD" };
                case GUIChat::Channel_Global:
                    return { "Global", "OOC broadcast", "OOC" };
                case GUIChat::Channel_Local:
                    return { "Local", "Local area", "IC" };
                case GUIChat::Channel_Private:
                    return { "Private", "Direct relay", "MSG" };
                case GUIChat::Channel_All:
                default:
                    return { "All Channels", "Unified feed", "OOC" };
            }
        }

        bool isHexDigit(char character)
        {
            return std::isxdigit(static_cast<unsigned char>(character)) != 0;
        }

        std::string stripColorTags(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());

            for (std::size_t index = 0; index < value.size();)
            {
                if (value[index] == '#' && index + 6 < value.size() &&
                    std::all_of(value.begin() + static_cast<std::ptrdiff_t>(index + 1),
                        value.begin() + static_cast<std::ptrdiff_t>(index + 7), isHexDigit))
                {
                    index += 7;
                    continue;
                }

                result.push_back(value[index]);
                ++index;
            }

            return result;
        }

        std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool startsWith(const std::string& value, const std::string& prefix)
        {
            return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
        }

        bool contains(const std::string& value, const std::string& needle)
        {
            return value.find(needle) != std::string::npos;
        }

        bool userString(MyGUI::Widget* widget, const std::string& key, std::string& value)
        {
            if (widget == nullptr || !widget->isUserString(key))
                return false;

            value = widget->getUserString(key);
            return true;
        }

        std::string legacyTabKey(const std::string& key)
        {
            const std::string communityPrefix = "CommunityMP_";
            if (!startsWith(key, communityPrefix))
                return key;

            return "TES3MP_" + key.substr(communityPrefix.size());
        }

        bool userStringWithLegacyKey(MyGUI::Widget* widget, const std::string& key, std::string& value)
        {
            if (userString(widget, key, value))
                return true;

            const std::string legacyKey = legacyTabKey(key);
            return legacyKey != key && userString(widget, legacyKey, value);
        }

        bool inheritedUserString(MyGUI::Widget* widget, MyGUI::Widget* root, const std::string& key, std::string& value)
        {
            if (userStringWithLegacyKey(widget, key, value))
                return true;

            return widget != root && userStringWithLegacyKey(root, key, value);
        }

        bool userStringBool(const std::string& value)
        {
            const std::string text = lowerAscii(value);
            return text == "1" || text == "true" || text == "yes" || text == "on";
        }

        MyGUI::Colour userStringColour(const std::string& value, const MyGUI::Colour& fallback, const char* key)
        {
            try
            {
                return MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags(value));
            }
            catch (const MyGUI::Exception& e)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid CommunityMP chat tab colour for %s: %s", key, e.what());
            }
            catch (const std::exception& e)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid CommunityMP chat tab colour for %s: %s", key, e.what());
            }

            return fallback;
        }

        bool chatDataExists(const char* name)
        {
            return MyGUI::DataManager::getInstance().isDataExist(name);
        }

        const char* selectCustomChatLayout()
        {
            if (chatDataExists(communityCustomChatLayout))
                return communityCustomChatLayout;

            if (chatDataExists(legacyCustomChatLayout))
                return legacyCustomChatLayout;

            return nullptr;
        }

        const char* customResourcesForLayout(const char* layout)
        {
            return layout == communityCustomChatLayout ? communityCustomChatResources : legacyCustomChatResources;
        }

        void loadCustomChatResources(const char* customResources)
        {
            if (customResourcesChecked || customLayoutOverrideDisabled)
                return;

            customResourcesChecked = true;

            if (!chatDataExists(customResources))
                return;

            try
            {
                MyGUI::ResourceManager::getInstance().load(customResources);
                LOG_MESSAGE_SIMPLE(
                    TimedLog::LOG_INFO, "Loaded CommunityMP chat resources from %s", customResources);
            }
            catch (const MyGUI::Exception& e)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to load CommunityMP chat resources %s: %s",
                    customResources, e.what());
            }
        }

        const char* selectChatLayout()
        {
            if (customLayoutOverrideDisabled)
                return defaultChatLayout;

            const char* layout = selectCustomChatLayout();
            if (layout == nullptr)
                return defaultChatLayout;

            loadCustomChatResources(customResourcesForLayout(layout));
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Using CommunityMP chat layout %s", layout);
            return layout;
        }
    }

    GUIChat::GUIChat(int x, int y, int w, int h)
            : WindowBase(selectChatLayout())
    {
        setCoord(x, y, w, h);

        getWidget(mCommandLine, "edit_Command");
        getWidget(mHistory, "list_History");
        getWidget(mAllTab, "tab_All");
        getWidget(mServerTab, "tab_Server");
        getWidget(mGlobalTab, "tab_Global");
        getWidget(mLocalTab, "tab_Local");
        getWidget(mPrivateTab, "tab_Private");
        mChannelTitle = findOptionalWidget<MyGUI::TextBox>(*this, "label_ChannelTitle");
        mChannelMode = findOptionalWidget<MyGUI::TextBox>(*this, "label_ChannelMode");
        mInputMode = findOptionalWidget<MyGUI::TextBox>(*this, "label_InputMode");

        // Set up the command line box
        mCommandLine->eventEditSelectAccept +=
                newDelegate(this, &GUIChat::acceptCommand);
        mCommandLine->eventKeyButtonPressed +=
                newDelegate(this, &GUIChat::keyPress);
        mAllTab->eventMouseButtonClick +=
                newDelegate(this, &GUIChat::tabPressed);
        mServerTab->eventMouseButtonClick +=
                newDelegate(this, &GUIChat::tabPressed);
        mGlobalTab->eventMouseButtonClick +=
                newDelegate(this, &GUIChat::tabPressed);
        mLocalTab->eventMouseButtonClick +=
                newDelegate(this, &GUIChat::tabPressed);
        mPrivateTab->eventMouseButtonClick +=
                newDelegate(this, &GUIChat::tabPressed);

        if (mMainWidget->castType<MyGUI::Window>(false) != nullptr)
            setTitle("Chat");

        mHistory->setOverflowToTheLeft(true);
        mHistory->setEditWordWrap(true);
        mHistory->setTextShadow(true);
        mHistory->setTextShadowColour(MyGUI::Colour::Black);

        mHistory->setNeedKeyFocus(false);

        windowState = CHAT_DISABLED;
        editState = false;
        activeChannel = Channel_All;
        curTime = 0;
        mCommandLine->setVisible(false);
        if (mInputMode != nullptr)
            mInputMode->setVisible(false);
        delay = 3; // 3 sec.
        updateTabCaptions();
        updateChannelDetails();
    }

    void GUIChat::onOpen()
    {
        // Give keyboard focus to the combo box whenever the console is
        // turned on
        setEditState(false);

        if (windowState == CHAT_DISABLED)
            windowState = CHAT_ENABLED;
    }

    void GUIChat::onClose()
    {
        setEditState(false);
    }

    bool GUIChat::exit()
    {
        //WindowBase::exit();
        return true;
    }

    bool GUIChat::getEditState()
    {
        return editState;
    }

    void GUIChat::acceptCommand(MyGUI::EditBox *_sender)
    {
        const std::string &cm = mCommandLine->getOnlyText();

        // If they enter nothing, then it should be canceled.
        // Otherwise, there's no way of closing without having text.
        if (cm.empty())
        {
            mCommandLine->setCaption("");
            setEditState(false);
            return;
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Player: %s", cm.c_str());

        // Add the command to the history, and set the current pointer to
        // the end of the list
        if (mCommandHistory.empty() || mCommandHistory.back() != cm)
            mCommandHistory.push_back(cm);
        mCurrent = mCommandHistory.end();
        mEditString.clear();

        // Reset the command line before the command execution.
        // It prevents the re-triggering of the acceptCommand() event for the same command
        // during the actual command execution
        mCommandLine->setCaption("");
        setEditState(false);
        send(cm);
    }

    void GUIChat::onResChange(int width, int height)
    {
        setCoord(10,10, width-10, height/2);
    }

    void GUIChat::setFont(const std::string &fntName)
    {
        mHistory->setFontName(fntName);
        mCommandLine->setFontName(fntName);
    }

    void GUIChat::print(const std::string &msg, const std::string &color)
    {
        if (windowState == CHAT_HIDDENMODE && !isVisible())
        {
            setVisible(true);
        }

        if(msg.size() == 0)
        {
            clean();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Chat cleaned");
        }
        else
        {
            mMessageHistory.push_back({color + msg, classifyMessage(msg)});
            if (mMessageHistory.size() > maxChatHistory)
                mMessageHistory.erase(mMessageHistory.begin());

            rebuildHistory();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", msg.c_str());
        }
    }

    void GUIChat::printOK(const std::string &msg)
    {
        print(msg + "\n", "#FF00FF");
    }

    void GUIChat::printError(const std::string &msg)
    {
        print(msg + "\n", "#FF2222");
    }

    void GUIChat::send(const std::string &str)
    {
        if (!str.empty() && str[0] != '/' && activeChannel == Channel_Server)
        {
            printError("Server tab is read-only. Use Global, Local, or a slash command.");
            return;
        }

        if (!str.empty() && str[0] != '/' && activeChannel == Channel_Private)
        {
            printError("Use /msg <pid> <text> for private chat.");
            return;
        }

        const std::string outgoing = formatOutgoingMessage(str);
        LocalPlayer *localPlayer = Main::get().getLocalPlayer();

        Networking *networking = Main::get().getNetworking();

        localPlayer->chatMessage = outgoing;

        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    void GUIChat::clean()
    {
        mMessageHistory.clear();
        mHistory->setCaption("");
    }

    void GUIChat::pressedChatMode()
    {
        windowState++;
        if (windowState == 3) windowState = 0;

        std::string chatMode = windowState == CHAT_DISABLED ? "Chat hidden" :
                               windowState == CHAT_ENABLED ? "Chat visible" :
                               "Chat appearing when needed";

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Switch chat mode to %s", chatMode.c_str());
        MWBase::Environment::get().getWindowManager()->messageBox(chatMode);

        switch (windowState)
        {
            case CHAT_DISABLED:
                setVisible(false);
                setEditState(false);
                break;
            case CHAT_ENABLED:
                setVisible(true);
                break;
            default: //CHAT_HIDDENMODE
                setVisible(true);
                curTime = 0;
        }
    }

    void GUIChat::setEditState(bool state)
    {
        editState = state;
        mCommandLine->setNeedKeyFocus(true);
        mCommandLine->setVisible(editState);
        if (mInputMode != nullptr)
            mInputMode->setVisible(editState);
        MWBase::Environment::get().getInputManager()->changeInputMode(editState);

        if (editState)
            focusInput();
        else
        {
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
            SDL_StopTextInput();
        }
    }

    void GUIChat::focusInput()
    {
        if (!editState)
            return;

        mCommandLine->setNeedKeyFocus(true);
        mCommandLine->setVisible(true);
        if (mInputMode != nullptr)
            mInputMode->setVisible(true);
        MyGUI::LayerManager::getInstance().upLayerItem(mMainWidget);
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
        SDL_StartTextInput();
    }

    bool GUIChat::hasCustomLayoutOverride()
    {
        return !customLayoutOverrideDisabled && selectCustomChatLayout() != nullptr;
    }

    void GUIChat::disableCustomLayoutOverride()
    {
        customLayoutOverrideDisabled = true;
    }

    void GUIChat::pressedSay()
    {
        if (windowState == CHAT_DISABLED)
            return;

        if (!mCommandLine->getVisible())
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Opening chat.");

        setVisible(true);

        if (windowState == CHAT_HIDDENMODE)
            curTime = 0;

        setEditState(true);
    }

    void GUIChat::keyPress(MyGUI::Widget *_sender, MyGUI::KeyCode key, MyGUI::Char _char)
    {
        if (key == MyGUI::KeyCode::Tab)
        {
            cycleActiveChannel(MyGUI::InputManager::getInstance().isShiftPressed());
            return;
        }

        if (mCommandHistory.empty()) return;

        // Traverse history with up and down arrows
        if (key == MyGUI::KeyCode::ArrowUp)
        {
            // If the user was editing a string, store it for later
            if (mCurrent == mCommandHistory.end())
                mEditString = mCommandLine->getOnlyText();

            if (mCurrent != mCommandHistory.begin())
            {
                --mCurrent;
                mCommandLine->setCaption(*mCurrent);
            }
        }
        else if (key == MyGUI::KeyCode::ArrowDown)
        {
            if (mCurrent != mCommandHistory.end())
            {
                ++mCurrent;

                if (mCurrent != mCommandHistory.end())
                    mCommandLine->setCaption(*mCurrent);
                else
                    // Restore the edit string
                    mCommandLine->setCaption(mEditString);
            }
        }

    }

    void GUIChat::tabPressed(MyGUI::Widget* sender)
    {
        if (sender == mAllTab)
            setActiveChannel(Channel_All);
        else if (sender == mServerTab)
            setActiveChannel(Channel_Server);
        else if (sender == mGlobalTab)
            setActiveChannel(Channel_Global);
        else if (sender == mLocalTab)
            setActiveChannel(Channel_Local);
        else if (sender == mPrivateTab)
            setActiveChannel(Channel_Private);

        if (editState)
            focusInput();
    }

    void GUIChat::setActiveChannel(ChatChannel channel)
    {
        activeChannel = channel;
        updateTabCaptions();
        updateChannelDetails();
        rebuildHistory();
    }

    void GUIChat::updateTabCaptions()
    {
        const MyGUI::Colour selectedColour(1.0f, 0.82f, 0.35f);
        const MyGUI::Colour normalColour(0.82f, 0.82f, 0.82f);
        auto updateTab = [this, selectedColour, normalColour](
                             MyGUI::Button* tab, bool selected, const std::string& defaultCaption) {
            std::string value;

            if (inheritedUserString(tab, mMainWidget, tabFontKey, value) && !value.empty())
                tab->setFontName(value);

            std::string caption = defaultCaption;
            if (userStringWithLegacyKey(tab, tabCaptionKey, value))
                caption = value;

            std::string selectedCaption;
            if (!userStringWithLegacyKey(tab, tabSelectedCaptionKey, selectedCaption))
            {
                std::string prefix = "[";
                std::string suffix = "]";

                if (inheritedUserString(tab, mMainWidget, tabSelectedPrefixKey, value))
                    prefix = value;
                if (inheritedUserString(tab, mMainWidget, tabSelectedSuffixKey, value))
                    suffix = value;

                selectedCaption = prefix + caption + suffix;
            }

            bool useSelectedState = false;
            if (inheritedUserString(tab, mMainWidget, tabUseSelectedStateKey, value))
                useSelectedState = userStringBool(value);

            if (useSelectedState)
                tab->setStateSelected(selected);

            bool applyTextColour = true;
            if (inheritedUserString(tab, mMainWidget, tabApplyTextColourKey, value))
                applyTextColour = userStringBool(value);

            if (applyTextColour)
            {
                MyGUI::Colour tabNormalColour = normalColour;
                MyGUI::Colour tabSelectedColour = selectedColour;

                if (inheritedUserString(tab, mMainWidget, tabNormalColourKey, value) && !value.empty())
                    tabNormalColour = userStringColour(value, normalColour, tabNormalColourKey);
                if (inheritedUserString(tab, mMainWidget, tabSelectedColourKey, value) && !value.empty())
                    tabSelectedColour = userStringColour(value, selectedColour, tabSelectedColourKey);

                tab->setTextColour(selected ? tabSelectedColour : tabNormalColour);
            }

            tab->setCaption(selected ? selectedCaption : caption);
        };

        updateTab(mAllTab, activeChannel == Channel_All, "All");
        updateTab(mServerTab, activeChannel == Channel_Server, "Server");
        updateTab(mGlobalTab, activeChannel == Channel_Global, "Global");
        updateTab(mLocalTab, activeChannel == Channel_Local, "Local");
        updateTab(mPrivateTab, activeChannel == Channel_Private, "Private");
    }

    void GUIChat::updateChannelDetails()
    {
        const ChannelDisplay display = getChannelDisplay(activeChannel);

        if (mChannelTitle != nullptr)
            mChannelTitle->setCaption(display.title);
        if (mChannelMode != nullptr)
            mChannelMode->setCaption(display.mode);
        if (mInputMode != nullptr)
            mInputMode->setCaption(display.input);
    }

    void GUIChat::rebuildHistory()
    {
        mHistory->setCaption("");

        for (const ChatLine& line : mMessageHistory)
        {
            if (shouldShowMessage(line.channel))
                mHistory->addText(line.text);
        }
    }

    void GUIChat::cycleActiveChannel(bool backwards)
    {
        int nextChannel = static_cast<int>(activeChannel) + (backwards ? -1 : 1);

        if (nextChannel < static_cast<int>(Channel_All))
            nextChannel = static_cast<int>(Channel_Private);
        else if (nextChannel > static_cast<int>(Channel_Private))
            nextChannel = static_cast<int>(Channel_All);

        setActiveChannel(static_cast<ChatChannel>(nextChannel));

        if (editState)
            focusInput();
    }

    GUIChat::ChatChannel GUIChat::classifyMessage(const std::string& msg) const
    {
        const std::string text = lowerAscii(stripColorTags(msg));

        if (text.empty())
            return Channel_Server;

        if (startsWith(text, "warning:") || startsWith(text, "you ") || startsWith(text, "welcome ") ||
            startsWith(text, "account ") || startsWith(text, "create ") || startsWith(text, "incorrect ") ||
            startsWith(text, "not a valid ") || startsWith(text, "please ") || startsWith(text, "running ") ||
            startsWith(text, "use ") || startsWith(text, "press "))
            return Channel_Server;

        if (contains(text, " to local area: ") || startsWith(text, "[ic] "))
            return Channel_Local;

        if (contains(text, " to ") && contains(text, ": "))
            return Channel_Private;

        if (contains(text, ": ") || startsWith(text, "[ooc] "))
            return Channel_Global;

        return Channel_Server;
    }

    bool GUIChat::shouldShowMessage(ChatChannel channel) const
    {
        return activeChannel == Channel_All || activeChannel == channel;
    }

    std::string GUIChat::formatOutgoingMessage(const std::string& msg) const
    {
        if (msg.empty() || msg[0] == '/')
            return msg;

        if (activeChannel == Channel_All || activeChannel == Channel_Global)
            return "/ooc " + msg;

        if (activeChannel == Channel_Local)
            return "/ic " + msg;

        return msg;
    }

    void GUIChat::update(float dt)
    {
        if (windowState == CHAT_HIDDENMODE && !editState && isVisible())
        {
            curTime += dt;
            if (curTime >= delay)
            {
                setEditState(false);
                setVisible(false);
            }
        }
    }

    void GUIChat::setDelay(float newDelay)
    {
        this->delay = newDelay;
    }
}

