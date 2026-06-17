[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingGuard
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required source file was not found: $path"
    }

    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

function Test-RequiredFile {
    param(
        [string]$Name,
        [string]$RelativePath,
        [System.Collections.Generic.List[string]]$Missing
    )

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $Missing.Add($Name)
    }
}

$controllerHeader = Get-SourceText "apps\openmw\mwmp\GUIController.hpp"
$controller = Get-SourceText "apps\openmw\mwmp\GUIController.cpp"
$chatProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorChatMessage.hpp"
$keyboardManager = Get-SourceText "apps\openmw\mwinput\keyboardmanager.cpp"
$chatHeader = Get-SourceText "apps\openmw\mwmp\GUI\GUIChat.hpp"
$chat = Get-SourceText "apps\openmw\mwmp\GUI\GUIChat.cpp"
$windowManager = Get-SourceText "apps\openmw\mwgui\windowmanagerimp.cpp"
$main = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$engine = Get-SourceText "apps\openmw\engine.cpp"
$filesCMake = Get-SourceText "files\CMakeLists.txt"
$skins = Get-SourceText "files\data\mygui\skins.xml"
$chatFont = Get-SourceText "files\mygui\tes3mp_chat_font.xml"
$chatLayout = Get-SourceText "files\mygui\tes3mp_chat.layout"
$chatSkin = Get-SourceText "files\mygui\tes3mp_chat.skin.xml"
$communityChatLayout = Get-SourceText "files\mygui\chat\communitymp_chat.layout"
$communityChatResources = Get-SourceText "files\mygui\chat\communitymp_chat.xml"
$communityChatSkin = Get-SourceText "files\mygui\chat\communitymp_chat.skin.xml"
$communityChatExampleLayout = Get-SourceText "files\mygui\chat\examples\default\communitymp_chat.layout"
$communityChatExampleResources = Get-SourceText "files\mygui\chat\examples\default\communitymp_chat.xml"
$communityChatExampleSkin = Get-SourceText "files\mygui\chat\examples\default\communitymp_chat.skin.xml"
$chatCustomizationDoc = Get-SourceText "docs\communitymp-chat-customization.md"
$geometryVerifier = Get-SourceText "scripts\check-communitymp-chargen-layout-geometry.ps1"
$defaultCommands = Get-SourceText "files\tes3mp\server\scripts\defaultCommands.lua"
$serverConfig = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$helpMenu = Get-SourceText "files\tes3mp\server\scripts\menu\help.lua"
$serverLuaCompat = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-RequiredFile -Name "CommunityMP default chat frame texture exists" `
    -RelativePath "files\mygui\chat\textures\communitymp_chat_frame.png" `
    -Missing $missing
Test-RequiredFile -Name "CommunityMP default chat tab atlas exists" `
    -RelativePath "files\mygui\chat\textures\communitymp_chat_tab_atlas.png" `
    -Missing $missing

Test-Pattern -Name "GUIController owns a pending chat message queue" -Text $controllerHeader `
    -Pattern 'std::vector<std::string>\s+mPendingChatMessages;' `
    -Missing $missing
Test-Pattern -Name "GUIController cleanup clears pending chat messages between sessions" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::cleanUp\(\)\s*\{.*mPendingChatMessages\.clear\(\);.*if\s*\(mChat\s*!=\s*nullptr\)\s*delete\s+mChat;.*mChat\s*=\s*nullptr;' `
    -Missing $missing
Test-Pattern -Name "GUIController setup creates chat before draining pending messages" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::setupChat\(\)\s*\{.*mChat\s*=\s*new\s+GUIChat\(chatX,\s*chatY,\s*chatW,\s*chatH\);.*for\s*\(const\s+std::string&\s+message\s*:\s*mPendingChatMessages\)\s*mChat->print\(message\);' `
    -Missing $missing
Test-Pattern -Name "GUIController falls back to the default chat layout when a custom layout fails" -Text $controller `
    -Pattern 'const\s+bool\s+customChatLayoutAvailable\s*=\s*GUIChat::hasCustomLayoutOverride\(\);.*catch\s*\(const\s+MyGUI::Exception&\s+e\).*if\s*\(!customChatLayoutAvailable\)\s*throw;.*GUIChat::disableCustomLayoutOverride\(\);.*mChat\s*=\s*new\s+GUIChat\(chatX,\s*chatY,\s*chatW,\s*chatH\);' `
    -Missing $missing
Test-Pattern -Name "GUIController setup clears pending messages after draining" -Text $controller `
    -Pattern 'for\s*\(const\s+std::string&\s+message\s*:\s*mPendingChatMessages\)\s*mChat->print\(message\);\s*mPendingChatMessages\.clear\(\);' `
    -Missing $missing
Test-Pattern -Name "GUIController prints immediately when chat exists" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::printChatMessage\(std::string\s*&msg\)\s*\{.*if\s*\(mChat\s*!=\s*nullptr\)\s*\{.*mChat->print\(msg\);\s*return;' `
    -Missing $missing
Test-Pattern -Name "GUIController queues early chat before chat exists" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::printChatMessage\(std::string\s*&msg\)\s*\{.*mPendingChatMessages\.push_back\(msg\);' `
    -Missing $missing
Test-Pattern -Name "GUIController setChatVisible is null-safe before chat setup" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::setChatVisible\(bool\s+chatVisible\)\s*\{.*if\s*\(mChat\s*!=\s*nullptr\).*mChat->setVisible\(chatVisible\);' `
    -Missing $missing
Test-Pattern -Name "GUIController setChatVisible enables normal chat mode when making chat visible" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::setChatVisible\(bool\s+chatVisible\)\s*\{.*if\s*\(mChat\s*!=\s*nullptr\).*if\s*\(chatVisible\s*&&\s*mChat->windowState\s*==\s*GUIChat::CHAT_DISABLED\)\s*mChat->windowState\s*=\s*GUIChat::CHAT_ENABLED;.*mChat->setVisible\(chatVisible\);' `
    -Missing $missing
Test-Pattern -Name "GUIController pressedKey ignores input before chat setup" -Text $controller `
    -Pattern 'bool\s+mwmp::GUIController::pressedKey\(int\s+key\)\s*\{.*if\s*\(mChat\s*==\s*nullptr\s*\|\|\s*windowManager->isConsoleMode\(\)\s*\|\|\s*windowManager->getMode\(\)\s*!=\s*MWGui::GM_None\)\s*return\s+false;' `
    -Missing $missing
Test-Pattern -Name "Keyboard manager routes TES3MP chat hotkeys before printable gameplay keys stay consumed" -Text $keyboardManager `
    -Pattern '#ifdef\s+BUILD_TES3MP_CLIENT.*#include\s+"\.\./mwmp/GUIController\.hpp".*#include\s+"\.\./mwmp/Main\.hpp".*const\s+bool\s+consumedByPrintableTextInput\s*=\s*SDL_IsTextInputActive\(\)\s*&&\s*printableKey;.*if\s*\(mwmp::Main::isInitialized\(\)\).*mwmp::GUIController\*\s+guiController\s*=\s*mwmp::Main::get\(\)\.getGUIController\(\);.*const\s+bool\s+chatMayConsumePrintableKey\s*=\s*consumedByPrintableTextInput\s*&&\s*!guiController->getChatEditState\(\);.*if\s*\(\(!consumed\s*\|\|\s*chatMayConsumePrintableKey\)\s*&&\s*guiController->pressedKey\(arg\.keysym\.scancode\)\)\s*\{\s*consumed\s*=\s*true;\s*mBindingsManager->setPlayerControlsEnabled\(false\);' `
    -Missing $missing
Test-Pattern -Name "Keyboard manager routes focused TES3MP chat text input into the edit box" -Text $keyboardManager `
    -Pattern 'void\s+KeyboardManager::textInput\(const\s+SDL_TextInputEvent&\s+arg\)\s*\{.*if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{.*mwmp::GUIController\*\s+guiController\s*=\s*mwmp::Main::get\(\)\.getGUIController\(\);.*if\s*\(guiController->getChatEditState\(\)\)\s*\{.*guiController->focusChatInput\(\);.*MyGUI::InputManager::getInstance\(\)\.injectKeyPress\(MyGUI::KeyCode::None,\s*\*it\);.*mBindingsManager->setPlayerControlsEnabled\(false\);.*return;' `
    -Missing $missing
Test-Pattern -Name "Keyboard manager lets focused TES3MP chat capture key presses before gameplay bindings" -Text $keyboardManager `
    -Pattern 'if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{.*mwmp::GUIController\*\s+guiController\s*=\s*mwmp::Main::get\(\)\.getGUIController\(\);.*if\s*\(guiController->getChatEditState\(\)\)\s*\{.*guiController->focusChatInput\(\);.*if\s*\(kc\s*!=\s*MyGUI::KeyCode::None\s*&&\s*!mBindingsManager->isDetectingBindingState\(\)\).*MWBase::Environment::get\(\)\.getWindowManager\(\)->injectKeyPress\(kc,\s*0,\s*arg\.repeat\);.*mBindingsManager->setPlayerControlsEnabled\(false\);.*MWBase::Environment::get\(\)\.getInputManager\(\)->setJoystickLastUsed\(false\);.*return;' `
    -Missing $missing
Test-Pattern -Name "Keyboard manager keeps focused TES3MP chat capturing key releases" -Text $keyboardManager `
    -Pattern 'void\s+KeyboardManager::keyReleased\(const\s+SDL_KeyboardEvent&\s+arg\)\s*\{.*if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{.*mwmp::GUIController\*\s+guiController\s*=\s*mwmp::Main::get\(\)\.getGUIController\(\);.*if\s*\(guiController->getChatEditState\(\)\)\s*\{.*guiController->focusChatInput\(\);.*MyGUI::InputManager::getInstance\(\)\.injectKeyRelease\(kc\);.*mBindingsManager->setPlayerControlsEnabled\(false\);.*return;' `
    -Missing $missing
Test-Pattern -Name "GUIChat initializes edit state before chat hotkeys read it" -Text $chat `
    -Pattern 'windowState\s*=\s*CHAT_DISABLED;\s*editState\s*=\s*false;.*curTime\s*=\s*0;\s*mCommandLine->setVisible\(false\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat title setting is safe for frameless custom root widgets" -Text $chat `
    -Pattern 'mMainWidget->castType<MyGUI::Window>\(false\)\s*!=\s*nullptr\)\s*setTitle\("Chat"\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat focuses chat input with text input and GUI mouse mode" -Text $chat `
    -Pattern 'void\s+GUIChat::setEditState\(bool\s+state\)\s*\{.*editState\s*=\s*state;.*mCommandLine->setNeedKeyFocus\(true\);.*mCommandLine->setVisible\(editState\);.*changeInputMode\(editState\);.*if\s*\(editState\)\s*focusInput\(\);.*else\s*\{.*setKeyFocusWidget\(nullptr\);.*SDL_StopTextInput\(\);.*void\s+GUIChat::focusInput\(\)\s*\{.*if\s*\(!editState\)\s*return;.*mCommandLine->setNeedKeyFocus\(true\);.*mCommandLine->setVisible\(true\);.*MyGUI::LayerManager::getInstance\(\)\.upLayerItem\(mMainWidget\);.*setKeyFocusWidget\(mCommandLine\);.*SDL_StartTextInput\(\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat pressedSay makes the chat window visible before focusing input" -Text $chat `
    -Pattern 'void\s+GUIChat::pressedSay\(\)\s*\{.*if\s*\(windowState\s*==\s*CHAT_DISABLED\)\s*return;.*setVisible\(true\);\s*if\s*\(windowState\s*==\s*CHAT_HIDDENMODE\)\s*curTime\s*=\s*0;\s*setEditState\(true\);' `
    -Missing $missing
Test-Pattern -Name "MyGUI resource list loads the TES3MP chat edit skin" -Text $skins `
    -Pattern '<List\s+file="tes3mp_chat\.skin\.xml"\s*/>' `
    -Missing $missing
Test-Pattern -Name "MyGUI resource list loads the TES3MP chat font before the edit skin" -Text $skins `
    -Pattern '<List\s+file="tes3mp_chat_font\.xml"\s*/>\s*<List\s+file="tes3mp_chat\.skin\.xml"\s*/>' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat edit skin is copied for every resource-list consumer" -Text $filesCMake `
    -Pattern 'copy_resource_file\("mygui/tes3mp_chat\.skin\.xml"\s+"\$\{OPENMW_RESOURCES_ROOT\}"\s+"resources/vfs/mygui/tes3mp_chat\.skin\.xml"\)' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat Russo font resources are copied for every resource-list consumer" -Text $filesCMake `
    -Pattern 'copy_resource_file\("mygui/RussoOne-Regular\.ttf"\s+"\$\{OPENMW_RESOURCES_ROOT\}"\s+"resources/vfs/mygui/RussoOne-Regular\.ttf"\).*copy_resource_file\("mygui/tes3mp_chat_font\.xml"\s+"\$\{OPENMW_RESOURCES_ROOT\}"\s+"resources/vfs/mygui/tes3mp_chat_font\.xml"\)' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat Russo font is registered with MyGUI" -Text $chatFont `
    -Pattern '<Resource\s+type="ResourceTrueTypeFont"\s+name="Russo">.*<Property\s+key="Source"\s+value="RussoOne-Regular\.ttf"\s*/>.*<Property\s+key="Size"\s+value="11"\s*/>' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat layout uses the classic Russo font" -Text $chatLayout `
    -Pattern '<Property\s+key="FontName"\s+value="Russo"\s*/>' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat edit skin uses the classic Russo font" -Text $chatSkin `
    -Pattern '<Property\s+key="FontName"\s+value="Russo"\s*/>' `
    -Missing $missing
Test-Pattern -Name "GUIController changeChatMode is null-safe before chat setup" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::changeChatMode\(\)\s*\{\s*if\s*\(mChat\s*==\s*nullptr\)\s*return;\s*mChat->pressedChatMode\(\);' `
    -Missing $missing
Test-Pattern -Name "GUIController getChatEditState is false before chat setup" -Text $controller `
    -Pattern 'bool\s+mwmp::GUIController::getChatEditState\(\)\s*\{\s*if\s*\(mChat\s*==\s*nullptr\)\s*return\s+false;\s*return\s+mChat->editState;' `
    -Missing $missing
Test-Pattern -Name "GUIController can refocus the active chat edit box" -Text $controller `
    -Pattern 'void\s+mwmp::GUIController::focusChatInput\(\)\s*\{\s*if\s*\(mChat\s*!=\s*nullptr\)\s*mChat->focusInput\(\);' `
    -Missing $missing
Test-Pattern -Name "Window manager keeps active TES3MP chat input in GUI mode" -Text $windowManager `
    -Pattern '#ifdef\s+BUILD_TES3MP_CLIENT.*#include\s+"\.\./mwmp/GUIController\.hpp".*#include\s+"\.\./mwmp/Main\.hpp".*bool\s+WindowManager::isGuiMode\(\)\s+const\s*\{.*if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{.*mwmp::GUIController\*\s+guiController\s*=\s*mwmp::Main::get\(\)\.getGUIController\(\).*if\s*\(guiController->getChatEditState\(\)\)\s*return\s+true;.*return\s+!mGuiModes\.empty\(\)' `
    -Missing $missing
Test-Pattern -Name "Chat message processor routes server chat through GUIController" -Text $chatProcessor `
    -Pattern 'if\s*\(player\s*!=\s*0\)\s*Main::get\(\)\.getGUIController\(\)->printChatMessage\(player->chatMessage\);' `
    -Missing $missing
Test-Pattern -Name "Main constructs chat only after TES3MP post-init" -Text $main `
    -Pattern 'void\s+Main::postInit\(\)\s*\{\s*pMain->mGUIController->setupChat\(\);.*environment\.getStateManager\(\)->newGame\(true\);' `
    -Missing $missing
Test-Pattern -Name "Engine runs TES3MP post-init immediately after successful initialization" -Text $engine `
    -Pattern 'if\s*\(!mwmp::Main::init\(mContentFiles,\s*mFileCollections\)\)\s*return;\s*if\s*\(mwmp::Main::isInitialized\(\)\)\s*\{\s*mwmp::Main::postInit\(\);\s*skipDefaultGameStart\s*=\s*true;' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat layout exposes channel tabs" -Text $chatLayout `
    -Pattern 'name="tab_All".*name="tab_Server".*name="tab_Global".*name="tab_Local".*name="tab_Private"' `
    -Missing $missing
Test-Pattern -Name "GUIChat selects optional custom MyGUI resources and layout" -Text $chat `
    -Pattern 'communityCustomChatLayout\s*=\s*"chat/communitymp_chat\.layout".*communityCustomChatResources\s*=\s*"chat/communitymp_chat\.xml".*legacyCustomChatLayout\s*=\s*"tes3mp_chat_custom\.layout".*legacyCustomChatResources\s*=\s*"tes3mp_chat_custom\.xml".*selectCustomChatLayout\(\).*chatDataExists\(communityCustomChatLayout\).*chatDataExists\(legacyCustomChatLayout\).*loadCustomChatResources\(customResourcesForLayout\(layout\)\).*return\s+layout;' `
    -Missing $missing
Test-Pattern -Name "CommunityMP chat subfolders are copied into packaged MyGUI resources" -Text $filesCMake `
    -Pattern 'file\(GLOB_RECURSE\s+COMMUNITYMP_MYGUI_FILES\s+CONFIGURE_DEPENDS\s+LIST_DIRECTORIES\s+false.*RELATIVE\s+"\$\{OpenMW_SOURCE_DIR\}/files/mygui".*"\$\{OpenMW_SOURCE_DIR\}/files/mygui/\*"\).*copy_all_resource_files\("\$\{OpenMW_SOURCE_DIR\}/files/mygui"\s+"\$\{OPENMW_RESOURCES_ROOT\}"\s+"resources/vfs/mygui"\s+"\$\{COMMUNITYMP_MYGUI_FILES\}"\)' `
    -Missing $missing
Test-Pattern -Name "GUIChat lets custom layouts style channel tabs" -Text $chat `
    -Pattern 'CommunityMP_TabFont.*CommunityMP_TabNormalColour.*CommunityMP_TabSelectedColour.*CommunityMP_TabSelectedPrefix.*CommunityMP_TabSelectedSuffix.*CommunityMP_TabUseSelectedState.*legacyTabKey\(const\s+std::string&\s+key\).*return\s+"TES3MP_"\s*\+\s*key\.substr\(communityPrefix\.size\(\)\);.*inheritedUserString\(tab,\s*mMainWidget,\s*tabFontKey,\s*value\).*tab->setFontName\(value\).*tab->setStateSelected\(selected\).*tab->setTextColour\(selected\s*\?\s*tabSelectedColour\s*:\s*tabNormalColour\)' `
    -Missing $missing
Test-Pattern -Name "CommunityMP default chat example includes required widgets" -Text $communityChatExampleLayout `
    -Pattern 'CommunityMP_TabFont.*CommunityMP_TabNormalColour.*CommunityMP_TabSelectedColour.*name="tab_All".*name="tab_Server".*name="tab_Global".*name="tab_Local".*name="tab_Private".*name="label_ChannelTitle".*name="label_ChannelMode".*name="list_History".*name="label_InputMode".*name="edit_Command"' `
    -Missing $missing
Test-Pattern -Name "CommunityMP active default chat override is shipped" -Text ($communityChatLayout + "`n" + $communityChatResources + "`n" + $communityChatSkin) `
    -Pattern 'ImageTexture"\s+value="mygui\\chat\\textures\\communitymp_chat_frame\.png".*name="tab_All".*name="tab_Server".*name="tab_Global".*name="tab_Local".*name="tab_Private".*name="label_ChannelTitle".*name="label_ChannelMode".*name="list_History".*name="label_InputMode".*name="edit_Command".*<List\s+file="chat/communitymp_chat\.skin\.xml"\s*/>.*ResourceSkin"\s+name="CommunityMP_ChatTab"\s+size="64 18"\s+texture="mygui\\chat\\textures\\communitymp_chat_tab_atlas\.png"' `
    -Missing $missing
Test-Pattern -Name "CommunityMP default chat example aligns channel tabs to the framed top rail" -Text $communityChatExampleLayout `
    -Pattern 'position="30 23 42 18"[^>]*name="tab_All".*position="74 23 58 18"[^>]*name="tab_Server".*position="134 23 60 18"[^>]*name="tab_Global".*position="196 23 52 18"[^>]*name="tab_Local".*position="250 23 64 18"[^>]*name="tab_Private"' `
    -Missing $missing
Test-Pattern -Name "CommunityMP chat shell exposes MMO channel state fields" -Text ($chatHeader + "`n" + $chat + "`n" + $communityChatLayout) `
    -Pattern '(?=.*MyGUI::TextBox\*\s+mChannelTitle;.*MyGUI::TextBox\*\s+mChannelMode;.*MyGUI::TextBox\*\s+mInputMode;)(?=.*struct\s+ChannelDisplay)(?=.*getChannelDisplay\(GUIChat::ChatChannel\s+channel\))(?=.*"All Channels".*"Unified feed".*"OOC")(?=.*"Server".*"System stream".*"CMD")(?=.*"Global".*"OOC broadcast".*"OOC")(?=.*"Local".*"Local area".*"IC")(?=.*"Private".*"Direct relay".*"MSG")(?=.*findOptionalWidget<MyGUI::TextBox>\(\*this,\s*"label_ChannelTitle"\).*findOptionalWidget<MyGUI::TextBox>\(\*this,\s*"label_ChannelMode"\).*findOptionalWidget<MyGUI::TextBox>\(\*this,\s*"label_InputMode"\))(?=.*updateChannelDetails\(\))(?=.*mInputMode->setVisible\(editState\))(?=.*void\s+GUIChat::updateChannelDetails\(\).*mChannelTitle->setCaption\(display\.title\).*mChannelMode->setCaption\(display\.mode\).*mInputMode->setCaption\(display\.input\))(?=.*name="label_ChannelTitle".*name="label_ChannelMode".*name="label_InputMode")' `
    -Missing $missing
Test-Pattern -Name "CommunityMP chat shell has deterministic geometry verification coverage" -Text $geometryVerifier `
    -Pattern 'files\\mygui\\chat\\communitymp_chat\.layout";\s+Width\s*=\s*490;\s+Height\s*=\s*331.*files\\mygui\\chat\\examples\\default\\communitymp_chat\.layout";\s+Width\s*=\s*490;\s+Height\s*=\s*331' `
    -Missing $missing
Test-Pattern -Name "CommunityMP default chat example includes resource list and skins" -Text ($communityChatExampleLayout + "`n" + $communityChatExampleResources + "`n" + $communityChatExampleSkin) `
    -Pattern 'ImageTexture"\s+value="mygui\\chat\\textures\\communitymp_chat_frame\.png".*<List\s+file="chat/communitymp_chat\.skin\.xml"\s*/>.*ResourceSkin"\s+name="CommunityMP_ChatTab"\s+size="64 18"\s+texture="mygui\\chat\\textures\\communitymp_chat_tab_atlas\.png".*ResourceSkin"\s+name="CommunityMP_ChatInputText".*ResourceSkin"\s+name="CommunityMP_ChatInput"' `
    -Missing $missing
Test-Pattern -Name "CommunityMP chat customization docs keep chat textures under the chat folder" -Text $chatCustomizationDoc `
    -Pattern 'resources\\vfs\\mygui\\chat\\textures.*<build>\\RelWithDebInfo\\resources\\vfs\\mygui\\chat\\textures.*ImageTexture"\s+value="mygui\\chat\\textures\\my_chat_frame\.png".*bright pink or magenta.*resources\\vfs\\mygui\\chat\\textures.*mygui\\chat\\textures' `
    -Missing $missing
Test-Pattern -Name "GUIChat exposes custom layout availability and session disable hooks" -Text $chatHeader `
    -Pattern 'static\s+bool\s+hasCustomLayoutOverride\(\);.*static\s+void\s+disableCustomLayoutOverride\(\);' `
    -Missing $missing
Test-Pattern -Name "TES3MP chat history stays below channel tabs" -Text $chatLayout `
    -Pattern '<Widget\s+type="EditBox"\s+skin="MW_TextBoxEdit"\s+position="5 34 380 299"\s+align="Stretch"\s+name="list_History">' `
    -Missing $missing
Test-Pattern -Name "GUIChat owns channel tab widgets and active channel state" -Text $chatHeader `
    -Pattern 'MyGUI::Button\*\s+mAllTab;.*MyGUI::Button\*\s+mServerTab;.*MyGUI::Button\*\s+mGlobalTab;.*MyGUI::Button\*\s+mLocalTab;.*MyGUI::Button\*\s+mPrivateTab;.*enum\s+ChatChannel.*Channel_All.*Channel_Server.*Channel_Global.*Channel_Local.*Channel_Private.*ChatChannel\s+activeChannel;' `
    -Missing $missing
Test-Pattern -Name "GUIChat stores bounded channel-filterable message history" -Text $chat `
    -Pattern 'constexpr\s+std::size_t\s+maxChatHistory\s*=\s*400;.*mMessageHistory\.push_back\(\{color\s*\+\s*msg,\s*classifyMessage\(msg\)\}\);.*if\s*\(mMessageHistory\.size\(\)\s*>\s*maxChatHistory\)\s*mMessageHistory\.erase\(mMessageHistory\.begin\(\)\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat wires all channel tabs to a shared tab handler" -Text $chat `
    -Pattern 'mAllTab->eventMouseButtonClick\s*\+=\s*newDelegate\(this,\s*&GUIChat::tabPressed\);.*mServerTab->eventMouseButtonClick\s*\+=\s*newDelegate\(this,\s*&GUIChat::tabPressed\);.*mGlobalTab->eventMouseButtonClick\s*\+=\s*newDelegate\(this,\s*&GUIChat::tabPressed\);.*mLocalTab->eventMouseButtonClick\s*\+=\s*newDelegate\(this,\s*&GUIChat::tabPressed\);.*mPrivateTab->eventMouseButtonClick\s*\+=\s*newDelegate\(this,\s*&GUIChat::tabPressed\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat rebuilds visible history from the selected channel" -Text $chat `
    -Pattern 'void\s+GUIChat::rebuildHistory\(\)\s*\{.*mHistory->setCaption\(""\);.*for\s*\(const\s+ChatLine&\s+line\s*:\s*mMessageHistory\).*if\s*\(shouldShowMessage\(line\.channel\)\)\s*mHistory->addText\(line\.text\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat classifies server global local and private messages" -Text $chat `
    -Pattern 'GUIChat::ChatChannel\s+GUIChat::classifyMessage\(const\s+std::string&\s+msg\)\s+const\s*\{.*warning:.*to local area:.*\[ic\].*Channel_Local.*Channel_Private.*\[ooc\].*Channel_Global' `
    -Missing $missing
Test-Pattern -Name "GUIChat sends selected all global and local tabs through explicit commands" -Text $chat `
    -Pattern 'activeChannel\s*==\s*Channel_Private.*printError\("Use /msg <pid> <text> for private chat\."\).*std::string\s+GUIChat::formatOutgoingMessage\(const\s+std::string&\s+msg\)\s+const\s*\{.*activeChannel\s*==\s*Channel_All\s*\|\|\s*activeChannel\s*==\s*Channel_Global.*"/ooc "\s*\+\s*msg.*activeChannel\s*==\s*Channel_Local.*"/ic "\s*\+\s*msg' `
    -Missing $missing
Test-Pattern -Name "GUIChat cycles chat channels with Tab while input is focused" -Text $chat `
    -Pattern 'if\s*\(key\s*==\s*MyGUI::KeyCode::Tab\)\s*\{.*cycleActiveChannel\(MyGUI::InputManager::getInstance\(\)\.isShiftPressed\(\)\);.*void\s+GUIChat::cycleActiveChannel\(bool\s+backwards\)\s*\{.*setActiveChannel\(static_cast<ChatChannel>\(nextChannel\)\);.*if\s*\(editState\)\s*focusInput\(\);' `
    -Missing $missing
Test-Pattern -Name "GUIChat keeps server and private tabs read-only for plain text" -Text $chat `
    -Pattern 'activeChannel\s*==\s*Channel_Server.*printError\("Server tab is read-only\. Use Global, Local, or a slash command\."\).*activeChannel\s*==\s*Channel_Private.*printError\("Use /msg <pid> <text> for private chat\."\)' `
    -Missing $missing
Test-Pattern -Name "Default commands expose global OOC chat aliases" -Text $defaultCommands `
    -Pattern 'defaultCommands\.ooc\s*=\s*function\(pid,\s*cmd\).*"\[OOC\] ".*customCommandHooks\.registerCommand\("ooc",\s*defaultCommands\.ooc\).*customCommandHooks\.registerCommand\("global",\s*defaultCommands\.ooc\).*customCommandHooks\.registerCommand\("g",\s*defaultCommands\.ooc\)' `
    -Missing $missing
Test-Pattern -Name "Default commands expose local IC chat scoped to cell visitors with sender fallback" -Text $defaultCommands `
    -Pattern 'local\s+sendCellChatMessage\s*=\s*function\(pid,\s*cellDescription,\s*message\).*for\s+index,\s+visitorPid\s+in\s+pairs\(LoadedCells\[cellDescription\]\.visitors\)\s+do.*tes3mp\.SendMessage\(visitorPid,\s*message,\s*false\).*if\s+sentToSender\s*==\s*false\s+then\s*tes3mp\.SendMessage\(pid,\s*message,\s*false\).*defaultCommands\.ic\s*=\s*function\(pid,\s*cmd\).*"\[IC\] ".*sendCellChatMessage\(pid,\s*cellDescription,\s*message\).*customCommandHooks\.registerCommand\("ic",\s*defaultCommands\.ic\)' `
    -Missing $missing
Test-Pattern -Name "Server join chat instructions advertise tabbed OOC and IC chat" -Text $serverConfig `
    -Pattern 'config\.chatWindowInstructions\s*=.*"Tab".*"Text in All or Global is OOC; text in Local is IC\..*"/help"' `
    -Missing $missing
Test-Pattern -Name "Help menu documents OOC and IC chat commands" -Text $helpMenu `
    -Pattern '"/ooc <text>\\n".*"\(/global, /g\)\\n".*"/ic <text>\\n"' `
    -Missing $missing
Test-Pattern -Name "Component coverage pins OOC and IC default command behavior" -Text $serverLuaCompat `
    -Pattern 'DefaultCommandsExposeOocAndIcChatChannels.*registered\.ooc.*registered\.global.*registered\.g.*registered\.ic.*\[OOC\].*\[IC\]' `
    -Missing $missing

Write-Host "TES3MP chat buffering check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 59"
Write-Host "Missing guards: $($missing.Count)"

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing or changed chat buffering patterns:"
    foreach ($item in $missing) {
        Write-Host "  $item"
    }

    if ($FailOnMissingGuard) {
        exit 1
    }
}
