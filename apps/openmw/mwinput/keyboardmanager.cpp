#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/sdlutil/sdlmappings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "../mwmp/GUIController.hpp"
#include "../mwmp/Main.hpp"
#endif

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent& arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::isInitialized())
        {
            mwmp::GUIController* guiController = mwmp::Main::get().getGUIController();
            if (guiController->getChatEditState())
            {
                guiController->focusChatInput();
                for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end();
                     ++it)
                    MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
                mBindingsManager->setPlayerControlsEnabled(false);
                return;
            }
        }
#endif
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
            && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput();

#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::isInitialized())
        {
            mwmp::GUIController* guiController = mwmp::Main::get().getGUIController();
            if (guiController->getChatEditState())
            {
                guiController->focusChatInput();
                if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
                    MWBase::Environment::get().getWindowManager()->injectKeyPress(kc, 0, arg.repeat);

                mBindingsManager->setPlayerControlsEnabled(false);
                MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
                return;
            }
        }
#endif

        const bool printableKey = !(SDLK_SCANCODE_MASK & arg.keysym.sym) &&
            // Don't trust isprint for symbols outside the extended ASCII range
            ((kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff)
                || (arg.keysym.sym >= 0 && arg.keysym.sym <= 255 && std::isprint(arg.keysym.sym)));
        const bool consumedByPrintableTextInput = SDL_IsTextInputActive() && printableKey;
        bool consumed = consumedByPrintableTextInput;
        if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            if (MWBase::Environment::get().getWindowManager()->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;
            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::isInitialized())
        {
            mwmp::GUIController* guiController = mwmp::Main::get().getGUIController();
            const bool chatMayConsumePrintableKey = consumedByPrintableTextInput && !guiController->getChatEditState();

            if ((!consumed || chatMayConsumePrintableKey) && guiController->pressedKey(arg.keysym.scancode))
            {
                consumed = true;
                mBindingsManager->setPlayerControlsEnabled(false);
            }
        }
#endif

        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        if (!consumed || consumedByPrintableTextInput)
        {
            MWBase::Environment::get().getLuaManager()->inputEvent(
                { MWBase::LuaManager::InputEvent::KeyPressed, arg.keysym });
        }

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);

#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::isInitialized())
        {
            mwmp::GUIController* guiController = mwmp::Main::get().getGUIController();
            if (guiController->getChatEditState())
            {
                guiController->focusChatInput();
                MyGUI::InputManager::getInstance().injectKeyRelease(kc);
                mBindingsManager->setPlayerControlsEnabled(false);
                return;
            }
        }
#endif

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));
        mBindingsManager->keyReleased(arg);
        MWBase::Environment::get().getLuaManager()->inputEvent(
            { MWBase::LuaManager::InputEvent::KeyReleased, arg.keysym });
    }
}
