#ifndef OPENMW_GUICHAT_HPP
#define OPENMW_GUICHAT_HPP

#include <list>
#include <string>
#include <vector>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace MyGUI
{
    class TextBox;
}

namespace mwmp
{
    class GUIController;
    class GUIChat : public MWGui::WindowBase
    {
        friend class GUIController;
    public:
        enum
        {
            CHAT_DISABLED = 0,
            CHAT_ENABLED,
            CHAT_HIDDENMODE
        } CHAT_WIN_STATE;

        MyGUI::EditBox* mCommandLine;
        MyGUI::EditBox* mHistory;
        MyGUI::Button* mAllTab;
        MyGUI::Button* mServerTab;
        MyGUI::Button* mGlobalTab;
        MyGUI::Button* mLocalTab;
        MyGUI::Button* mPrivateTab;
        MyGUI::TextBox* mChannelTitle;
        MyGUI::TextBox* mChannelMode;
        MyGUI::TextBox* mInputMode;

        typedef std::list<std::string> StringList;
        enum ChatChannel
        {
            Channel_All = 0,
            Channel_Server,
            Channel_Global,
            Channel_Local,
            Channel_Private
        };

        struct ChatLine
        {
            std::string text;
            ChatChannel channel;
        };

        // History of previous entered commands
        StringList mCommandHistory;
        StringList::iterator mCurrent;
        std::string mEditString;
        std::vector<ChatLine> mMessageHistory;

        GUIChat(int x, int y, int w, int h);

        void pressedChatMode(); //switch chat mode
        void pressedSay(); // switch chat focus (if chat mode != CHAT_DISABLED)
        void setDelay(float newDelay);

        void update(float dt);

        virtual void onOpen();
        virtual void onClose();

        virtual bool exit();

        bool getEditState();

        void setFont(const std::string &fntName);

        void onResChange(int width, int height);

        // Print a message to the console, in specified color.
        void print(const std::string &msg, const std::string& color = "#FFFFFF");

        // Clean chat
        void clean();

        // These are pre-colored versions that you should use.

        /// Output from successful console command
        void printOK(const std::string &msg);

        /// Error message
        void printError(const std::string &msg);

        void send(const std::string &str);

    protected:

    private:

        void keyPress(MyGUI::Widget* _sender,
                      MyGUI::KeyCode key,
                      MyGUI::Char _char);

        void acceptCommand(MyGUI::EditBox* _sender);

        void setEditState(bool state);
        void focusInput();
        static bool hasCustomLayoutOverride();
        static void disableCustomLayoutOverride();
        void tabPressed(MyGUI::Widget* sender);
        void setActiveChannel(ChatChannel channel);
        void updateTabCaptions();
        void updateChannelDetails();
        void rebuildHistory();
        void cycleActiveChannel(bool backwards);
        ChatChannel classifyMessage(const std::string& msg) const;
        bool shouldShowMessage(ChatChannel channel) const;
        std::string formatOutgoingMessage(const std::string& msg) const;

        int windowState;
        bool editState;
        ChatChannel activeChannel;
        float delay;
        float curTime;
    };
}
#endif //OPENMW_GUICHAT_HPP

