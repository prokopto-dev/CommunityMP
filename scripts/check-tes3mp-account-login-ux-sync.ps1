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

function Test-AllPatterns {
    param(
        [string]$Name,
        [string]$Text,
        [string[]]$Patterns,
        [System.Collections.Generic.List[string]]$Missing
    )

    foreach ($pattern in $Patterns) {
        if (-not [regex]::IsMatch($Text, $pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
            $Missing.Add($Name)
            return
        }
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

$clientMain = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$guiController = Get-SourceText "apps\openmw\mwmp\GUIController.cpp"
$characterSelectDialog = Get-SourceText "apps\openmw\mwmp\GUI\GUICharacterSelect.cpp"
$characterSelectDialogHeader = Get-SourceText "apps\openmw\mwmp\GUI\GUICharacterSelect.hpp"
$dialogList = Get-SourceText "apps\openmw\mwmp\GUI\GUIDialogList.cpp"
$dialogListHeader = Get-SourceText "apps\openmw\mwmp\GUI\GUIDialogList.hpp"
$branding = Get-SourceText "components\openmw-mp\Branding.hpp"
$versionHeader = Get-SourceText "components\openmw-mp\Version.hpp"
$windowManagerBase = Get-SourceText "apps\openmw\mwbase\windowmanager.hpp"
$windowManager = Get-SourceText "apps\openmw\mwgui\windowmanagerimp.cpp"
$loginLayout = Get-SourceText "files\mygui\tes3mp_login.layout"
$communityLoginLayout = Get-SourceText "files\mygui\login\communitymp_login.layout"
$communityLoginResources = Get-SourceText "files\mygui\login\communitymp_login.xml"
$communityLoginSkin = Get-SourceText "files\mygui\login\communitymp_login.skin.xml"
$communityClientDefaults = Get-SourceText "files\communitymp\communitymp-client-default.cfg"
$legacyClientDefaults = Get-SourceText "files\tes3mp\tes3mp-client-default.cfg"
$builtinDataCMake = Get-SourceText "files\data\CMakeLists.txt"
$loginCustomizationDocs = Get-SourceText "docs\communitymp-login-customization.md"
$licensingNotes = Get-SourceText "docs\communitymp-licensing-notes.md"
$characterSelectLayout = Get-SourceText "files\mygui\characterselect\communitymp_character_select.layout"
$characterSelectResources = Get-SourceText "files\mygui\characterselect\communitymp_character_select.xml"
$characterSelectSkin = Get-SourceText "files\mygui\characterselect\communitymp_character_select.skin.xml"
$characterSelectDocs = Get-SourceText "docs\communitymp-character-select-customization.md"
$characterPreviewHeader = Get-SourceText "apps\openmw\mwrender\characterpreview.hpp"
$characterPreview = Get-SourceText "apps\openmw\mwrender\characterpreview.cpp"
$raceDialog = Get-SourceText "apps\openmw\mwgui\race.cpp"
$classDialog = Get-SourceText "apps\openmw\mwgui\class.cpp"
$birthDialog = Get-SourceText "apps\openmw\mwgui\birth.cpp"
$reviewDialog = Get-SourceText "apps\openmw\mwgui\review.cpp"
$characterCreation = Get-SourceText "apps\openmw\mwgui\charactercreation.cpp"
$textInputDialog = Get-SourceText "apps\openmw\mwgui\textinput.cpp"
$textInputDialogHeader = Get-SourceText "apps\openmw\mwgui\textinput.hpp"
$avatarPreviewController = Get-SourceText "apps\openmw\mwgui\avatarpreview.hpp"
$raceLayout = Get-SourceText "files\data\mygui\openmw_chargen_race.layout"
$classChoiceLayout = Get-SourceText "files\data\mygui\openmw_chargen_class_choice.layout"
$classLayout = Get-SourceText "files\data\mygui\openmw_chargen_class.layout"
$birthLayout = Get-SourceText "files\data\mygui\openmw_chargen_birth.layout"
$reviewLayout = Get-SourceText "files\data\mygui\openmw_chargen_review.layout"
$nameLayout = Get-SourceText "files\data\mygui\openmw_chargen_name.layout"
$createClassLayout = Get-SourceText "files\data\mygui\openmw_chargen_create_class.layout"
$generateClassResultLayout = Get-SourceText "files\data\mygui\openmw_chargen_generate_class_result.layout"
$generateClassQuestionLayout = Get-SourceText "files\data\mygui\openmw_chargen_generate_class_question.layout"
$selectSpecializationLayout = Get-SourceText "files\data\mygui\openmw_chargen_select_specialization.layout"
$selectAttributeLayout = Get-SourceText "files\data\mygui\openmw_chargen_select_attribute.layout"
$selectSkillLayout = Get-SourceText "files\data\mygui\openmw_chargen_select_skill.layout"
$classDescriptionLayout = Get-SourceText "files\data\mygui\openmw_chargen_class_description.layout"
$infoBoxLayout = Get-SourceText "files\data\mygui\openmw_infobox.layout"
$guiFunctionsHeader = Get-SourceText "apps\openmw-mp\Script\Functions\GUI.hpp"
$guiFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\GUI.cpp"
$packetGuiBoxes = Get-SourceText "components\openmw-mp\Packets\Player\PacketGUIBoxes.cpp"
$renderingManagerHeader = Get-SourceText "apps\openmw\mwrender\renderingmanager.hpp"
$browserMainWindow = Get-SourceText "apps\browser\MainWindow.cpp"
$clientSettings = Get-SourceText "components\openmw-mp\ClientSettings.cpp"
$baseSystem = Get-SourceText "components\openmw-mp\Base\BaseSystem.hpp"
$systemHandshakePacket = Get-SourceText "components\openmw-mp\Packets\System\PacketSystemHandshake.cpp"
$serverPassword = Get-SourceText "components\openmw-mp\ServerPassword.cpp"
$serverNetworking = Get-SourceText "apps\openmw-mp\Networking.cpp"
$miscFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Miscellaneous.cpp"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$guiHelper = Get-SourceText "files\tes3mp\server\scripts\guiHelper.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"
$installVerifier = Get-SourceText "scripts\verify-tes3mp-install.ps1"
$deployScript = Get-SourceText "scripts\deploy-tes3mp.ps1"
$packageScript = Get-SourceText "scripts\package-communitymp.ps1"
$runtimeSmoke = Get-SourceText "scripts\smoke-tes3mp-runtime.ps1"
$geometryVerifier = Get-SourceText "scripts\check-communitymp-chargen-layout-geometry.ps1"
$componentTests = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "CommunityMP product branding is separated from executable and compatibility names" -Text $branding `
    -Pattern 'namespace\s+mwmp::Branding.*productName\s*=\s*"CommunityMP".*productVersion\s*=\s*TES3MP_VERSION.*launcherExecutableName\s*=\s*"communitymp".*executableName\s*=\s*"communitymp-client".*serverExecutableName\s*=\s*"communitymp-server".*hubExecutableName\s*=\s*"communitymp-hub".*masterExecutableName\s*=\s*"masterserver".*compatibilityName\s*=\s*"TES3MP".*defaultGameHost\s*=\s*"play\.communitymp\.com".*defaultMasterHost\s*=\s*"master\.communitymp\.com".*websiteUrl\s*=\s*"https://communitymp\.com"' `
    -Missing $missing

Test-Pattern -Name "CommunityMP server script version gate follows the app version" -Text ($versionHeader + "`n" + $serverCore) `
    -Pattern '#define\s+TES3MP_VERSION\s+"0\.1\.0".*expectedVersionPrefix\s*=\s*"0\.1\.0".*tes3mp\.GetServerVersion\(\)' `
    -Missing $missing

Test-Pattern -Name "Window manager exposes account, server, and remember-me login credentials" -Text $windowManagerBase `
    -Pattern 'struct\s+LoginCredentials\s*\{.*std::string\s+accountName;.*std::string\s+accountPassword;.*std::string\s+serverPassword;.*bool\s+rememberCredentials\s*=\s*false;.*bool\s+useRememberedAccountPasswordHash\s*=\s*false;.*promptLoginCredentials\(std::string_view\s+serverEndpoint,\s+const\s+std::string&\s+initialAccountName\s*=\s*\{\},\s+const\s+std::string&\s+initialServerPassword\s*=\s*\{\},\s+bool\s+rememberCredentials\s*=\s*false,\s+bool\s+hasRememberedAccountPasswordHash\s*=\s*false\)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP login panel uses dedicated skinned layout with account and server access fields" -Text ($communityLoginResources + "`n" + $communityLoginSkin + "`n" + $communityLoginLayout + "`n" + $loginLayout) `
    -Pattern 'List\s+file="login/communitymp_login\.skin\.xml".*ResourceSkin"\s+name="CommunityMP_LoginButton".*ResourceSkin"\s+name="CommunityMP_LoginToggle".*texture="mygui\\login\\textures\\communitymp_login_button_atlas\.png".*ImageTexture"\s+value="mygui\\login\\textures\\communitymp_login_panel\.png".*CommunityMP.*Sign in to your server account before the world session starts\..*Server account.*Account username.*Account password.*CommunityMP_LoginToggle"\s+position=.*name="ButtonRemember".*\[ \]\s+Remember me.*Server join password.*Leave blank unless this server requires a join password\..*CommunityMP_LoginButton"\s+position=.*name="ButtonConnect".*Sign In.*Multiplayer Sign In.*ButtonRemember' `
    -Missing $missing

Test-AllPatterns -Name "OpenMW login dialog prefers CommunityMP layout, layers media behind the panel, and masks passwords" -Text $windowManager `
    -Patterns @(
        'communityLoginLayout\s*=\s*"login/communitymp_login\.layout"',
        'communityLoginResources\s*=\s*"login/communitymp_login\.xml"',
        'defaultLoginBackgroundTexture\s*=\s*"mygui\\\\login\\\\textures\\\\communitymp-causeway\.jpg"',
        'defaultLoginBackgroundSlides.*communitymp-causeway\.jpg.*communitymp-gathering\.jpg.*communitymp-server-hall\.jpg.*communitymp-ashlands-hero\.jpg',
        'defaultLoginAtmosphereOverlayTexture\s*=\s*"mygui\\\\login\\\\textures\\\\communitymp_login_atmosphere\.png"',
        'defaultLoginLogoTexture\s*=\s*"mygui\\\\login\\\\textures\\\\communitymp-logo\.png"',
        'loginBackgroundLayer\s*=\s*"Scene"',
        'defaultLoginMusicTrack\s*=\s*"music/communitymp/nightinthedesertmix\.ogg"',
        'selectLoginLayout\(\).*ResourceManager::getInstance\(\)\.load\(communityLoginResources\)',
        'getLoginBackgroundTexture\(\).*Settings::Manager::getString\("loginBackground",\s*"General"\)',
        'getLoginBackgroundSlides\(\).*Settings::Manager::getString\("loginBackgroundSlides",\s*"General"\)',
        'getLoginBackgroundEffectsEnabled\(\).*Settings::Manager::getBool\("loginBackgroundEffects",\s*"General"\)',
        'getLoginAtmosphereOverlayTexture\(\).*Settings::Manager::getString\("loginAtmosphereOverlay",\s*"General"\)',
        'getLoginSlideSeconds\(\).*Settings::Manager::getFloat\("loginSlideSeconds",\s*"General"\)',
        'getLoginLogoTexture\(\).*Settings::Manager::getString\("loginLogo",\s*"General"\)',
        'getLoginMusicTrack\(\).*Settings::Manager::getString\("loginMusic",\s*"General"\)',
        'WindowModal\(selectLoginLayout\(\)\)',
        'createBackground\(\)',
        'createLogo\(\)',
        'startLoginMusic\(\)',
        'stopLoginMusic\(\)',
        'createWidgetReal<BackgroundImage>\(.*loginBackgroundLayer',
        'mBackground->setBackgroundImage\(mBackgroundSlides\.front\(\),\s*false,\s*true\)',
        'mBackgroundNext->setBackgroundImage\(mBackgroundSlides\[1\],\s*false,\s*true\)',
        'mAtmosphereOverlay->setImageTexture\(atmosphereTexture\)',
        'updateBackgroundAnimation\(float\s+duration\).*setAnimatedBackgroundCoord\(mBackground,\s*mCurrentBackgroundSlide',
        'createWidgetReal<MyGUI::ImageBox>\(.*"Windows"\)',
        'mLogo->setImageTexture\(texture\)',
        'soundManager->stopMusic\(\).*soundManager->streamMusic\(VFS::Path::Normalized\(music\),\s*MWSound::MusicType::Normal,\s*0\.f\)',
        'getSoundManager\(\)->stopMusic\(\)',
        'mAccountNameEdit->setCaption\(MyGUI::UString\(std::string\(initialAccountName\)\)\);',
        'mServerPasswordEdit->setCaption\(MyGUI::UString\(std::string\(initialServerPassword\)\)\);',
        'mAccountPasswordEdit->setEditPassword\(true\);',
        'mServerPasswordEdit->setEditPassword\(true\);',
        'const\s+bool\s+restoreHudVisibility\s*=\s*isHudVisible\(\);',
        'setHudVisibility\(false\);.*mPromptLoginDialog\s*=\s*std::make_unique<Tes3mpLoginDialog>',
        'mPromptLoginDialog->onFrame\(dt\);',
        'removeDialog\(std::move\(mPromptLoginDialog\)\);.*setHudVisibility\(restoreHudVisibility\);'
    ) `
    -Missing $missing

Test-AllPatterns -Name "OpenMW login dialog supports remember-me without treating saved hashes as raw passwords" -Text $windowManager `
    -Patterns @(
        'rememberedPasswordMask\s*=\s*"\*\*\*\*\*\*\*\*"',
        'mUsingRememberedAccountPasswordHash',
        'mRememberCredentialsButton',
        'getCredentials\(\)\s+const.*mUsingRememberedAccountPasswordHash\s*\?\s*std::string\(\)\s*:\s*std::string\(mAccountPasswordEdit->getCaption\(\)\)',
        'mRememberCredentials,\s*mUsingRememberedAccountPasswordHash',
        'onCredentialChanged.*mUsingRememberedAccountPasswordHash.*mAccountPasswordEdit->setCaption\(""\)',
        'onRememberCredentialsClicked.*mRememberCredentials\s*=\s*!mRememberCredentials.*updateRememberCredentialsButton',
        '!credentials\.useRememberedAccountPasswordHash\s*&&\s*credentials\.accountPassword\.empty\(\)',
        'Enter the account password\.'
    ) `
    -Missing $missing

Test-Pattern -Name "Login prompt runs as a modal pre-connect loop and returns credentials only when accepted" -Text $windowManager `
    -Pattern 'std::optional<MWBase::LoginCredentials>\s+WindowManager::promptLoginCredentials\(std::string_view\s+serverEndpoint,\s+const\s+std::string&\s+initialAccountName,\s+const\s+std::string&\s+initialServerPassword,\s+bool\s+rememberCredentials,\s+bool\s+hasRememberedAccountPasswordHash\).*mPromptLoginDialog\s*=\s*std::make_unique<Tes3mpLoginDialog>\(serverEndpoint,\s*initialAccountName,\s*initialServerPassword,\s*rememberCredentials,\s*hasRememberedAccountPasswordHash\);.*while\s*\(!mPromptLoginDone\s*&&\s*!MWBase::Environment::get\(\)\.getStateManager\(\)->hasQuitRequest\(\)\).*removeDialog\(std::move\(mPromptLoginDialog\)\);.*return\s+mPromptLoginResult;.*void\s+WindowManager::onPromptLoginDone\(WindowBase\*.*\).*if\s*\(mPromptLoginDialog\s*&&\s*mPromptLoginDialog->isAccepted\(\)\)\s+mPromptLoginResult\s*=\s*mPromptLoginDialog->getCredentials\(\);.*mPromptLoginDone\s*=\s*true;' `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP client seeds the prompt from settings and persists remember-me hash only when requested" -Text $clientMain `
    -Patterns @(
        '\("name",\s*bpo::value<std::string>\(\)->default_value\(""\),\s*"CommunityMP account name to send during server login"\)',
        'Main::playerName\s*=\s*variables\["name"\]\.as<std::string>\(\);',
        'playerName\s*=\s*Settings::Manager::getString\("playerName",\s*"General"\);',
        'Settings::Manager::getBool\("rememberAccount",\s*"General"\)',
        'Settings::Manager::getString\("accountPasswordHash",\s*"General"\)',
        'promptLoginCredentials\(\s*serverEndpoint,\s*playerName,\s*initialServerPassword,\s*rememberCredentials,\s*hasRememberedAccountPasswordHash\);',
        'saveLoginCredentialSettings\(playerName,\s*credentials->rememberCredentials,\s*accountPasswordHash\);',
        'Settings::Manager::setString\(\s*"accountPasswordHash",\s*"General",\s*rememberCredentials\s*\?\s*accountPasswordHash\s*:\s*std::string\(\)\);'
    ) `
    -Missing $missing

Test-Pattern -Name "TES3MP client hashes or reuses remembered account hash and keeps the join password separate" -Text $clientMain `
    -Pattern 'std::string\s+getAccountPasswordHash\(std::string_view\s+password\).*picosha2::hash256_hex_string.*serverPassword\s*=\s*normalizeServerPassword\(credentials->serverPassword\);.*const\s+std::string\s+accountPasswordHash\s*=\s*credentials->useRememberedAccountPasswordHash\s*\?\s*rememberedAccountPasswordHash\s*:\s*getAccountPasswordHash\(credentials->accountPassword\);.*pendingAccountPassword\s*=\s*credentials->useRememberedAccountPasswordHash\s*\?\s*std::string\(\)\s*:\s*credentials->accountPassword;.*if\s*\(playerName\.empty\(\)\s*\|\|\s*accountPasswordHash\.empty\(\)\).*mLocalSystem->playerName\s*=\s*playerName;.*mLocalSystem->serverPassword\s*=\s*serverPassword;.*mLocalSystem->accountPasswordHash\s*=\s*accountPasswordHash;.*mNetworking->connect\(pMain->server,\s*pMain->port,\s*content,\s*collections\);.*if\s*\(!pMain->mNetworking->isConnected\(\)\)\s+pendingAccountPassword\.clear\(\);' `
    -Missing $missing

Test-Pattern -Name "CommunityMP client defaults expose login media and remember-me settings" -Text ($communityClientDefaults + "`n" + $legacyClientDefaults) `
    -Pattern 'rememberAccount\s*=\s*false.*accountPasswordHash\s*=.*loginBackground\s*=\s*mygui\\login\\textures\\communitymp-causeway\.jpg.*loginBackgroundSlides\s*=.*communitymp-causeway\.jpg.*communitymp-gathering\.jpg.*communitymp-server-hall\.jpg.*communitymp-ashlands-hero\.jpg.*loginBackgroundEffects\s*=\s*true.*loginAtmosphereOverlay\s*=\s*mygui\\login\\textures\\communitymp_login_atmosphere\.png.*loginSlideSeconds\s*=\s*6.*loginLogo\s*=\s*mygui\\login\\textures\\communitymp-logo\.png.*loginMusic\s*=\s*music/communitymp/nightinthedesertmix\.ogg.*rememberAccount\s*=\s*false.*accountPasswordHash\s*=.*loginBackground\s*=\s*mygui\\login\\textures\\communitymp-causeway\.jpg.*loginBackgroundSlides\s*=.*communitymp-causeway\.jpg.*communitymp-gathering\.jpg.*communitymp-server-hall\.jpg.*communitymp-ashlands-hero\.jpg.*loginBackgroundEffects\s*=\s*true.*loginAtmosphereOverlay\s*=\s*mygui\\login\\textures\\communitymp_login_atmosphere\.png.*loginSlideSeconds\s*=\s*6.*loginLogo\s*=\s*mygui\\login\\textures\\communitymp-logo\.png.*loginMusic\s*=\s*music/communitymp/nightinthedesertmix\.ogg' `
    -Missing $missing

Test-Pattern -Name "Client settings only persist configured TES3MP defaults, keeping account password out of the config file" -Text $clientSettings `
    -Pattern 'constexpr\s+char\s+settingsFileName\[\]\s*=\s*"communitymp-client\.cfg";.*constexpr\s+char\s+legacySettingsFileName\[\]\s*=\s*"tes3mp-client\.cfg";.*"communitymp-client-default\.cfg".*"tes3mp-client-default\.cfg".*filterForClientDefaults\(Settings::Manager::mUserSettings,\s*defaults\).*for\s*\(const\s+auto&\s+\[key,\s*value\]\s*:\s*source\).*if\s*\(defaults\.contains\(key\)\)\s+settings\[key\]\s*=\s*value;.*saveCleanSettingsFile\(getPreferredSettingsPath\(cfgMgr\),\s*collectCommunityMpUserSettings\(defaults\)\);' `
    -Missing $missing

Test-Pattern -Name "Browser join dialog asks for account name only plus optional server join password" -Text $browserMainWindow `
    -Pattern 'struct\s+JoinCredentials\s*\{.*QString\s+accountName;.*QString\s+serverPassword;.*showJoinDialog\(QWidget\*\s+parent,\s+const\s+QString&\s+address,\s+bool\s+requiresServerPassword\).*character names are loaded separately.*accountNameEdit->setPlaceholderText\(QObject::tr\("Account username"\)\);.*serverPasswordEdit->setEchoMode\(QLineEdit::Password\);.*if\s*\(requiresServerPassword\)\s+form->addRow\(QObject::tr\("Join password:"\),\s*serverPasswordEdit\);.*This password unlocks the server connection only\. It is separate from your account password\..*if\s*\(accountNameEdit->text\(\)\.trimmed\(\)\.isEmpty\(\)\).*Missing account username.*if\s*\(requiresServerPassword\s*&&\s*serverPasswordEdit->text\(\)\.isEmpty\(\)\).*Missing server password' `
    -Missing $missing

Test-Pattern -Name "Browser launch passes account name as --name and only passes --password for passworded servers" -Text $browserMainWindow `
    -Pattern 'arguments\.append\(QLatin1String\("--client"\)\);.*arguments\.append\(QLatin1String\("--connect="\)\s*\+\s*sm->myData\[sourceId\]\.addr\.toLatin1\(\)\);.*showJoinDialog\(this,\s*sm->myData\[sourceId\]\.addr,\s*sm->myData\[sourceId\]\.GetPassword\(\)\s*==\s*1\);.*saveLastPlayerName\(credentials->accountName\);.*arguments\.append\(QStringLiteral\("--name="\)\s*\+\s*credentials->accountName\);.*if\s*\(sm->myData\[sourceId\]\.GetPassword\(\)\s*==\s*1\)\s+arguments\.append\(QLatin1String\("--password="\)\s*\+\s*credentials->serverPassword\.toLatin1\(\)\);.*startProcess\(QLatin1String\("communitymp"\),\s*arguments,\s*true\)' `
    -Missing $missing

Test-Pattern -Name "System handshake carries account name, server join password, and account password hash as separate fields" -Text ($baseSystem + "`n" + $systemHandshakePacket) `
    -Pattern 'std::string\s+playerName;.*std::string\s+serverPassword;.*std::string\s+accountPasswordHash;.*PacketSystemHandshake::PacketSystemHandshake.*packetID\s*=\s*ID_SYSTEM_HANDSHAKE;.*orderChannel\s*=\s*CHANNEL_SYSTEM;.*RW\(system->playerName,\s*send,\s*true,\s*maxNameLength\).*RW\(system->serverPassword,\s*send,\s*true,\s*maxPasswordLength\).*RW\(system->accountPasswordHash,\s*send,\s*true,\s*maxAccountPasswordHashLength\).*packetValid\s*=\s*false' `
    -Missing $missing

Test-Pattern -Name "Server password validation treats the join password as connection-only and accepts extra password only on unpassworded servers" -Text ($serverPassword + "`n" + $serverNetworking) `
    -Pattern 'normalizeServerPassword\(std::string_view\s+password\).*if\s*\(password\.empty\(\)\)\s+return\s+TES3MP_DEFAULT_PASSW;.*bool\s+mwmp::isServerPassworded\(std::string_view\s+password\).*return\s+password\s*!=\s*TES3MP_DEFAULT_PASSW;.*validateServerPassword\(std::string_view\s+serverPassword,\s*std::string_view\s+clientPassword\).*if\s*\(clientPassword\s*==\s*serverPassword\)\s+return\s+ServerPasswordValidation::Accepted;.*if\s*\(isServerPassworded\(serverPassword\)\)\s+return\s+ServerPasswordValidation::Rejected;.*return\s+ServerPasswordValidation::AcceptedWithExtraClientPassword;.*validateServerPassword\(serverPassword,\s*baseSystem\.serverPassword\).*Wrong server password.*player->setLoginPasswordHash\(baseSystem\.accountPasswordHash\)' `
    -Missing $missing

Test-Pattern -Name "Server Lua consumes and clears handshake password hashes before falling back to legacy password dialogs" -Text ($miscFunctions + "`n" + $eventHandler) `
    -Pattern 'GetHandshakePasswordHash\(unsigned\s+short\s+pid\).*return\s+player->getLoginPasswordHash\(\)\.c_str\(\);.*ClearHandshakePasswordHash\(unsigned\s+short\s+pid\).*player->clearLoginPasswordHash\(\);.*local\s+handshakePasswordHash\s*=\s*tes3mp\.GetHandshakePasswordHash\(pid\).*tes3mp\.ClearHandshakePasswordHash\(pid\).*pendingHandshakePasswordHash\s*=\s*handshakePasswordHash.*if\s+handshakePasswordHash\s*==\s*nil\s+or\s+handshakePasswordHash\s*==\s*""\s+then.*guiHelper\.ShowLogin\(pid\).*guiHelper\.ShowRegister\(pid\).*ProcessAccountPassword\(pid,\s*pendingLoginAction,\s*pendingHandshakePasswordHash\)' `
    -Missing $missing

Test-Pattern -Name "Legacy fallback login/register dialogs remain available for retry and old script compatibility" -Text ($guiHelper + "`n" + $eventHandler) `
    -Pattern '(?=.*guiHelper\.names\s*=\s*\{"LOGIN",\s*"REGISTER",\s*"PLAYERSLIST",\s*"CELLSLIST",\s*"CHARACTERLIST"\})(?=.*guiHelper\.ShowLogin\s*=\s*function\(pid\).*tes3mp\.PasswordDialog\(pid,\s*guiHelper\.ID\.LOGIN,\s*"Account password:")(?=.*guiHelper\.ShowRegister\s*=\s*function\(pid\).*tes3mp\.PasswordDialog\(pid,\s*guiHelper\.ID\.REGISTER,\s*"Create account password:")(?=.*guiHelper\.ShowCharacterList\s*=\s*function\(pid\))(?=.*GetCharacterSlotListLabel)(?=.*GetCharacterSlotPreviewMetadata)(?=.*tes3mp\.ListBoxWithMetadata\(pid,\s*guiHelper\.ID\.CHARACTERLIST)(?=.*tes3mp\.ListBox\(pid,\s*guiHelper\.ID\.CHARACTERLIST)(?=.*Character slots keep separate inventory,\s*journal,\s*topics and quest state)(?=.*if\s+idGui\s*==\s*guiHelper\.ID\.LOGIN\s+then.*guiHelper\.ShowLogin\(pid\))(?=.*local\s+replacedSessionCount\s*=\s*logicHandler\.DisconnectAuthenticatedAccountSessions\(Players\[pid\]\.accountName,\s*pid\).*if\s+replacedSessionCount\s*>\s*0\s+then\s+Players\[pid\]:LoadFromDrive\(\)\s+end\s+Players\[pid\]\.accountAuthenticated\s*=\s*true)(?=.*Players\[pid\]:StopLoginTimer\(\).*guiHelper\.ShowCharacterList\(pid\))(?=.*elseif\s+idGui\s*==\s*guiHelper\.ID\.REGISTER\s+then.*guiHelper\.ShowRegister\(pid\).*Players\[pid\]:Register\(data\))(?=.*local\s+ProcessCharacterSelection\s*=\s*function\(pid,\s*data\).*Players\[pid\]:StartNewCharacter\(\).*Players\[pid\]:SelectCharacterSlot\(selectedIndex\).*Players\[pid\]:FinishLogin\(\))(?=.*eventHandler\.OnGUIAction\s*=\s*function\(pid,\s*idGui,\s*data\).*if\s+idGui\s*==\s*guiHelper\.ID\.CHARACTERLIST\s+then\s+ProcessCharacterSelection\(pid,\s*data\)\s+else\s+ProcessAccountPassword\(pid,\s*idGui,\s*data\))' `
    -Missing $missing

Test-Pattern -Name "CommunityMP character roster supports guarded account-slot deletion without stale confirmation callbacks" -Text ($characterSelectLayout + "`n" + $characterSelectDialogHeader + "`n" + $characterSelectDialog + "`n" + $eventHandler + "`n" + $playerBase) `
    -Pattern '(?=.*name="DeleteButton")(?=.*mDeleteButton)(?=.*confirmDeleteSelection\(\))(?=.*detachDeleteConfirmationCallbacks\(\))(?=.*eventOkClicked\s*-=\s*MyGUI::newDelegate\(this,\s*&GUICharacterSelect::deleteSelectionConfirmed\))(?=.*eventCancelClicked\s*-=\s*MyGUI::newDelegate\(this,\s*&GUICharacterSelect::deleteSelectionCanceled\))(?=.*submitResponse\("delete:"\s*\+\s*MyGUI::utility::toString\(mEntries\[mSelectedIndex\]\.originalIndex\)\))(?=.*local\s+deleteRow\s*=\s*string\.match\(data,\s*"\^delete:\(%d\+\)\$"\))(?=.*local\s+selectedIndex\s*=\s*tonumber\(deleteRow\)\s*\+\s*1)(?=.*Players\[pid\]:DeleteCharacterSlot\(selectedIndex\))(?=.*Players\[pid\]:QuicksaveToDrive\(\))(?=.*guiHelper\.ShowCharacterList\(pid\))(?=.*function\s+BasePlayer:DeleteCharacterSlot\(characterIndex\).*entries\[characterIndex\]\s*=\s*nil.*compactCharacterEntries\(entries\).*self\.data\.characters\.selectedIndex\s*=\s*nextSelectedIndex.*self:RestoreSharedAccountData\(sharedData\).*self\.loggedIn\s*=\s*false.*self\.activeCharacterIndex\s*=\s*nil.*self\.creatingNewCharacter\s*=\s*false.*self:SelectCharacterSlot\(nextSelectedIndex\).*self\.loggedIn\s*=\s*wasLoggedIn)' `
    -Missing $missing

Test-Pattern -Name "Server scripts distinguish account authentication from loaded character login" -Text ($playerBase + "`n" + $eventHandler) `
    -Pattern 'function\s+BasePlayer:IsLoggedIn\(\)\s+return\s+self:HasLoadedCharacter\(\).*function\s+BasePlayer:HasAuthenticatedAccount\(\)\s+return\s+self\.accountAuthenticated\s*==\s*true.*function\s+BasePlayer:HasLoadedCharacter\(\)\s+return\s+self\.loggedIn.*local\s+HasAuthenticatedAccount\s*=\s*function\(player\).*player\.HasAuthenticatedAccount\s*~=\s*nil.*return\s+player:HasAuthenticatedAccount\(\).*return\s+player\.accountAuthenticated\s*==\s*true.*not\s+HasAuthenticatedAccount\(Players\[pid\]\)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP character selection uses a dedicated skinned client dialog with metadata-backed preview fallback" -Text ($characterSelectDialogHeader + "`n" + $characterSelectDialog + "`n" + $dialogListHeader + "`n" + $dialogList + "`n" + $guiController + "`n" + $guiFunctionsHeader + "`n" + $guiFunctions + "`n" + $packetGuiBoxes + "`n" + $renderingManagerHeader + "`n" + $characterPreviewHeader + "`n" + $characterPreview) `
    -Pattern '(?=.*class\s+GUICharacterSelect)(?=.*GUICharacterSelect\(const\s+std::string&\s+message,\s+const\s+std::vector<std::string>&\s+list,\s+const\s+std::string&\s+metadata\))(?=.*SlotEntry.*originalIndex.*raceId.*headId.*hairId.*hasPreviewMetadata)(?=.*RaceSelectionPreview)(?=.*PreviewMode\s*\{.*Head.*Body)(?=.*renderHeadOnly\(\)\s+override\s*\{\s*return\s+mPreviewMode\s*==\s*PreviewMode::Head;\s*\})(?=.*previewMode\s*==\s*PreviewMode::Body\s*\?\s*768\s*:\s*512)(?=.*OSGTexture)(?=.*visibleSlotCount\s*=\s*6)(?=.*characterSelectLayout\s*=\s*"characterselect/communitymp_character_select\.layout")(?=.*characterSelectResources\s*=\s*"characterselect/communitymp_character_select\.xml")(?=.*parsePreviewMetadata)(?=.*WindowModal\(selectCharacterSelectLayout\(\)\))(?=.*createBackdrop\(\))(?=.*createLogo\(\))(?=.*startLobbyMusic\(\))(?=.*updateBackdropAnimation\(float\s+duration\))(?=.*mAvatarPreviewController\.update\(duration\).*mPreviewAngle\s*=\s*mAvatarPreviewController\.getAngle\(\))(?=.*buildEntries\(list,\s*metadata\))(?=.*showRenderedPortrait)(?=.*ESM::RefId::stringRefId\(entry\.raceId\))(?=.*RaceSelectionPreview::PreviewMode::Body)(?=.*mAvatarPreviewController\.bind\(mPortrait,\s*mPreview\.get\(\)\))(?=.*setRenderItemTexture\(mPreviewTexture\.get\(\)\))(?=.*getRootNode\(\))(?=.*GUIDialogList::isCharacterListDialog\(guiMessageBox\.id\))(?=.*enterCharacterPresentation\(\))(?=.*mCharacterSelect->update\(dt\))(?=.*setHudVisibility\(false\))(?=.*std::make_unique<GUICharacterSelect>\(guiMessageBox\.label,\s*list,\s*guiMessageBox\.note\))(?=.*catch\s*\(const\s+MyGUI::Exception&\s+e\).*GUICharacterSelect::disableCustomLayoutOverride\(\))(?=.*std::make_unique<GUIDialogList>\(guiMessageBox\.label,\s*list,\s*guiMessageBox\.id\))(?=.*ListBoxWithMetadata)(?=.*player->guiMessageBox\.note\.clear\(\))(?=.*player->guiMessageBox\.note\s*=\s*metadata)(?=.*BasePlayer::GUIMessageBox::ListBox)(?=.*RW\(player->guiMessageBox\.note,\s*send\))' `
    -Missing $missing

Test-Pattern -Name "Bundled character select resources keep required widgets and texture-prefixed assets" -Text ($characterSelectResources + "`n" + $characterSelectSkin + "`n" + $characterSelectLayout + "`n" + $characterSelectDocs) `
    -Pattern 'List\s+file="characterselect/communitymp_character_select\.skin\.xml".*ResourceSkin"\s+name="CommunityMP_CharacterSlot".*ResourceSkin"\s+name="CommunityMP_CharacterButton".*ResourceSkin"\s+name="CommunityMP_CharacterActionButton".*texture="mygui\\characterselect\\textures\\communitymp_character_button_atlas\.png".*ImageTexture"\s+value="mygui\\characterselect\\textures\\communitymp_character_select_frame\.png".*type="EditBox".*name="Message".*type="Button".*name="Slot0".*name="Slot0Text".*name="Slot5".*name="Slot5Text".*name="UpButton".*name="DownButton".*ImageTexture"\s+value="mygui\\characterselect\\textures\\communitymp_character_portrait\.png".*ImageTexture"\s+value="mygui\\characterselect\\textures\\communitymp_character_portrait_frame\.png".*name="PortraitInitial".*name="PortraitSubtitle".*name="CharacterTitle".*name="CharacterDetails".*name="ActionButtons".*skin="CommunityMP_CharacterActionButton".*name="DeleteButton".*name="SelectButton".*Custom layouts must keep these widget names and compatible types.*Texture paths should include the `mygui\\characterselect\\textures\\\.\.\.` prefix.*power-of-two source sizes' `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP character roster uses MMO character hub fields and full-body preview mode" -Text ($characterSelectLayout + "`n" + $characterSelectDialogHeader + "`n" + $characterSelectDialog + "`n" + $avatarPreviewController + "`n" + $characterPreviewHeader + "`n" + $characterPreview) `
    -Patterns @(
        'position="0 0 1180 650"',
        'name="RosterEyebrow".*ACCOUNT ROSTER',
        'name="Slot0Subtext".*name="Slot5Subtext"',
        'name="StageEyebrow".*PREVIEW',
        'name="Portrait".*NeedMouse"\s+value="true"',
        'name="SessionSummary".*name="ActionHint"',
        'name="ActionButtons".*skin="CommunityMP_CharacterActionButton".*name="DeleteButton".*name="SelectButton"',
        'mSlotSubtexts',
        'findOptionalWidget<MyGUI::TextBox>',
        'MWGui::AvatarPreviewController',
        'RaceSelectionPreview::PreviewMode::Body',
        'rosterPreviewMaxZoom\s*=\s*0\.35f',
        'configureInspectionLimits\(rosterPreviewMaxZoom,\s*rosterPreviewMaxVerticalFocus\)',
        'resetFraming\(float\s+angle\s*=\s*0\.f\)',
        'mRenderedPreviewValid',
        'previewEntryChanged.*mAvatarPreviewController\.resetFraming\(previewAngle\)',
        'mAvatarPreviewController\.bind\(mPortrait,\s*nullptr\)',
        'mAvatarPreviewController\.bind\(mPortrait,\s*mPreview\.get\(\)\)',
        'mAvatarPreviewController\.update\(duration\).*mPreviewAngle\s*=\s*mAvatarPreviewController\.getAngle\(\)',
        'mSelectButton->setCoord\(actionDeleteWidth\s*\+\s*actionButtonGap,\s*actionButtonTop,\s*actionSelectCompactWidth,\s*actionButtonHeight\)',
        'mSelectButton->setCoord\(0,\s*actionButtonTop,\s*actionButtonFullWidth,\s*actionButtonHeight\)',
        'mSelectButton->setCaption\(mEntries\[mSelectedIndex\]\.createNew\s*\?\s*"Create Character"\s*:\s*"Enter World"\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "OpenMW race chargen uses MMO-scale full-body creator layout" -Text ($raceLayout + "`n" + $raceDialog + "`n" + $characterCreation + "`n" + $avatarPreviewController + "`n" + $characterPreviewHeader + "`n" + $characterPreview) `
    -Patterns @(
        'position="0 0 1320 760".*name="_Main".*mygui\\characterselect\\textures\\communitymp_character_select_frame\.png',
        'name="CreatorStageRail".*name="CreatorStageAppearance".*TextColour"\s+value="1 0\.86 0\.42"',
        'position="72 44 360 24"\s+name="RaceT".*position="486 44 380 24"\s+name="AppearanceT".*Caption"\s+value="APPEARANCE SCAN".*position="900 44 348 24"\s+name="SkillsT"',
        'position="486 86 380 548"\s+name="AppearancePreviewPanel".*position="12 10 356 508".*name="PreviewImage".*position="48 526 284 14"\s+name="HeadRotate"',
        'position="72 86 360 250"\s+name="RacePanel".*name="RaceList"',
        'name="BodyPanel".*name="BodyLabel".*Caption"\s+value="Body".*name="PrevGenderButton".*name="GenderChoiceT".*name="NextGenderButton"',
        'name="FacePanel".*name="FaceLabel".*Caption"\s+value="Face".*name="PrevFaceButton".*name="FaceChoiceT".*name="NextFaceButton"',
        'name="HairPanel".*name="HairLabel".*Caption"\s+value="Hair".*name="PrevHairButton".*name="HairChoiceT".*name="NextHairButton"',
        'position="900 86 348 244"\s+name="SkillBonusPanel".*name="SkillList".*position="900 390 348 244"\s+name="SpecialsPanel".*name="SpellPowerList"',
        'RaceSelectionPreview::PreviewMode::Body',
        'mRaceDialog->onFrame\(duration\)',
        'mAvatarPreviewController\.bind\(mPreviewImage,\s*mPreview\.get\(\)\).*mAvatarPreviewController\.setAngle\(mCurrentAngle\)',
        'RaceDialog::onFrame\(float\s+duration\).*mAvatarPreviewController\.update\(duration\).*mCurrentAngle\s*=\s*mAvatarPreviewController\.getAngle\(\)',
        'RaceDialog::updatePreview\(\).*const\s+float\s+previewAngle\s*=\s*mAvatarPreviewController\.getAngle\(\).*mPreview->setPrototype\(record\).*mAvatarPreviewController\.setAngle\(previewAngle\).*mCurrentAngle\s*=\s*previewAngle',
        'RaceDialog::onHeadRotate\(MyGUI::ScrollBar\*\s+scroll,\s+size_t\s+position\).*mAvatarPreviewController\.setAngle\(angle\)',
        'if\s*\(mBoundImage\s*!=\s*mImage\).*eventMouseButtonPressed',
        'previewMode\s*==\s*PreviewMode::Body\s*\?\s*768\s*:\s*512',
        'mPreviewMode\s*==\s*PreviewMode::Body.*mRTTNode->setViewMatrix'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP chargen name stage uses an avatar-backed MMO identity screen" -Text ($nameLayout + "`n" + $textInputDialogHeader + "`n" + $textInputDialog + "`n" + $characterCreation + "`n" + $builtinDataCMake) `
    -Patterns @(
        'mygui/openmw_chargen_name\.layout',
        'TextInputDialog\(std::string_view\s+layout\s*=\s*"openmw_text_input\.layout",\s*osg::Group\*\s+parent\s*=\s*nullptr,\s*Resource::ResourceSystem\*\s+resourceSystem\s*=\s*nullptr\)',
        'std::make_unique<TextInputDialog>\("openmw_chargen_name\.layout",\s*mParent,\s*mResourceSystem\)',
        'mNameDialog->onFrame\(duration\)',
        'position="0 0 1320 760".*name="_Main".*mygui\\characterselect\\textures\\communitymp_character_select_frame\.png.*name="NameStageT"',
        'name="NameStageT".*Caption"\s+value="CHARACTER NAME".*name="IdentityPanel".*name="TextEdit"',
        'name="NameAvatarT".*Caption"\s+value="PREVIEW".*name="NameAvatarPanel".*name="AvatarPreviewImage"',
        'name="NameNextT".*Caption"\s+value="PROFILE".*name="NameFlowPanel"',
        'name="CreatorStageRail".*name="CreatorStageIdentity".*TextColour"\s+value="1 0\.86 0\.42"',
        'Shown in chat and saves\..*Account remains separate\..*1\. Appearance.*2\. Class.*3\. Birthsign.*4\. Review.*Next: Appearance',
        'mAvatarPreview\s*=\s*std::make_unique<MWRender::RaceSelectionPreview>.*RaceSelectionPreview::PreviewMode::Body',
        'mAvatarPreviewController\.bind\(mAvatarPreviewImage,\s*mAvatarPreview\.get\(\)\)',
        'mAvatarPreviewController\.update\(duration\)',
        'mAvatarPreviewImage->setRenderItemTexture\(mAvatarPreviewTexture\.get\(\)\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP chargen class birthsign and review stages share the MMO creator shell" -Text ($classLayout + "`n" + $birthLayout + "`n" + $reviewLayout) `
    -Patterns @(
        'position="0 0 1320 760".*name="_Main".*mygui\\characterselect\\textures\\communitymp_character_select_frame\.png.*name="ClassStageT"',
        'name="ClassStageT".*Caption"\s+value="CLASS"',
        'name="CreatorStageRail".*name="CreatorStageClass".*TextColour"\s+value="1 0\.86 0\.42"',
        'name="ClassList".*name="ClassAvatarPanel".*name="AvatarPreviewImage".*name="ClassImagePanel".*name="ClassImage"',
        'name="SpecializationName".*name="FavoriteAttribute0".*name="FavoriteAttribute1"',
        'name="MajorSkill0".*name="MajorSkill4".*name="MinorSkill0".*name="MinorSkill4"',
        'position="0 0 1320 760".*name="_Main".*mygui\\characterselect\\textures\\communitymp_character_select_frame\.png.*name="BirthStageT"',
        'name="BirthStageT".*name="CreatorStageRail".*name="CreatorStageBirthsign".*TextColour"\s+value="1 0\.86 0\.42"',
        'name="BirthStageT".*Caption"\s+value="BIRTHSIGN".*name="BirthPowerT".*Caption"\s+value="SIGN EFFECTS"',
        'name="BirthStageT".*name="BirthsignPanel".*name="BirthsignList".*name="BirthAvatarPanel".*name="AvatarPreviewImage"',
        'name="BirthsignArtPanel".*name="BirthsignImage".*name="BirthHintPanel".*name="BirthHintText".*name="BirthPowerPanel".*name="SpellArea"',
        'name="ReviewIdentityT".*name="CreatorStageRail".*name="CreatorStageReview".*TextColour"\s+value="1 0\.86 0\.42"',
        'name="ReviewIdentityT".*name="ReviewIdentityPanel".*name="NameButton".*name="RaceButton".*name="ClassButton".*name="SignButton"',
        'name="ReviewAvatarT".*name="ReviewAvatarPanel".*name="AvatarPreviewImage"',
        'name="Health".*name="Magicka".*name="Fatigue"',
        'name="ReviewVitalsPanel".*position="34 432 330 150"\s+name="Attributes"',
        'name="Skills".*name="SkillView"'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP chargen advances directly and recovers from layout failures" -Text $characterCreation `
    -Patterns @(
        'bool\s+isCommunityMpCharacterCreation\(\).*mwmp::Main::isInitialized\(\).*localPlayer\s*!=\s*nullptr\s*&&\s*!localPlayer->hasLoadedCharacter\(\)',
        'if\s*\(isCommunityMpCharacterCreation\(\)\).*popGuiMode\(\).*if\s*\(id\s*==\s*GM_Class\).*CommunityMP chargen recovered by returning to appearance selection.*pushGuiMode\(GM_Race\).*else.*CommunityMP chargen recovered by returning to class selection.*pushGuiMode\(GM_Class\)',
        'if\s*\(isCommunityMpCharacterCreation\(\)\).*popGuiMode\(\).*mCreationStage\s*<\s*currentStage.*mCreationStage\s*=\s*currentStage.*pushGuiMode\(static_cast<GuiMode>\(nextMode\)\)',
        'handleDialogDone\(CSE_ClassChosen,\s*GM_Birth\)',
        'handleDialogDone\(CSE_BirthSignChosen,\s*GM_Review\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP review attributes expand with the MMO review panel" -Text $reviewDialog `
    -Patterns @(
        'MyGUI::Widget\*\s+attributes\s*=\s*getWidget\("Attributes"\)',
        'MyGUI::IntCoord\s+coord\{\s*8,\s*4,\s*std::max\(0,\s*attributes->getWidth\(\)\s*-\s*16\),\s*18\s*\}'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP chargen keeps an animated full-body avatar preview through class birthsign and review" -Text ($classDialog + "`n" + $birthDialog + "`n" + $reviewDialog + "`n" + $characterCreation + "`n" + $characterPreviewHeader + "`n" + $characterPreview + "`n" + $avatarPreviewController) `
    -Patterns @(
        'PickClassDialog::PickClassDialog\(osg::Group\*\s+parent,\s+Resource::ResourceSystem\*\s+resourceSystem\)',
        'BirthDialog::BirthDialog\(osg::Group\*\s+parent,\s+Resource::ResourceSystem\*\s+resourceSystem\)',
        'ReviewDialog::ReviewDialog\(osg::Group\*\s+parent,\s+Resource::ResourceSystem\*\s+resourceSystem\)',
        'std::make_unique<PickClassDialog>\(mParent,\s*mResourceSystem\)',
        'std::make_unique<BirthDialog>\(mParent,\s*mResourceSystem\)',
        'std::make_unique<ReviewDialog>\(mParent,\s*mResourceSystem\)',
        'mPickClassDialog->onFrame\(duration\)',
        'mBirthSignDialog->onFrame\(duration\)',
        'mAvatarPreview\s*=\s*std::make_unique<MWRender::RaceSelectionPreview>.*RaceSelectionPreview::PreviewMode::Body',
        'mAvatarPreviewController\.bind\(mAvatarPreviewImage,\s*mAvatarPreview\.get\(\)\)',
        'mAvatarPreviewController\.setAngle\(0\.f\)',
        'mAvatarPreviewController\.update\(duration\)',
        'mAvatarPreviewImage->setRenderItemTexture\(mAvatarPreviewTexture\.get\(\)\)',
        'mAvatarPreviewImage->getSubWidgetMain\(\)->_setUVSet\(MyGUI::FloatRect\(0\.f,\s*1\.f,\s*1\.f,\s*0\.f\)\)',
        'setAngle\(mAngle\s*\+\s*\(autoRotationSpeed\s*\+\s*mAngularVelocity\)\s*\*\s*frameDuration\)',
        'renderHeadOnly\(\)\s+override\s*\{\s*return\s+mPreviewMode\s*==\s*PreviewMode::Head;\s*\}'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP chargen avatar previews support direct drag rotation and inspection zoom" -Text ($avatarPreviewController + "`n" + $textInputDialogHeader + "`n" + $textInputDialog + "`n" + $raceDialog + "`n" + $characterCreation + "`n" + $classDialog + "`n" + $birthDialog + "`n" + $reviewDialog + "`n" + $characterPreviewHeader + "`n" + $characterPreview) `
    -Patterns @(
        'class\s+AvatarPreviewController',
        'eventMouseButtonPressed\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseButtonPressed\)',
        'eventMouseButtonReleased\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseButtonReleased\)',
        'eventMouseDrag\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseDrag\)',
        'eventMouseWheel\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseWheel\)',
        'eventMouseButtonDoubleClick\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseDoubleClick\)',
        'configureInspectionLimits\(float\s+maxZoom,\s*float\s+maxVerticalFocus\).*mMaxZoom\s*=\s*std::clamp\(maxZoom,\s*minZoom,\s*defaultMaxZoom\)',
        'mAngle\s*=\s*std::fmod\(angle,\s*twoPi\).*mAngle\s*\+=\s*twoPi',
        'frameDuration\s*=\s*std::min\(std::max\(0\.f,\s*duration\),\s*maxFrameDuration\)',
        'setAngle\(mAngle\s*\+\s*\(autoRotationSpeed\s*\+\s*mAngularVelocity\)\s*\*\s*frameDuration\)',
        'deltaAngle\s*=\s*static_cast<float>\(left\s*-\s*mLastPointerX\)\s*\*\s*dragRadiansPerPixel.*mAngularVelocity\s*=\s*std::clamp\(deltaAngle\s*\*\s*dragMomentumFramesPerSecond,\s*-maxAngularVelocity,\s*maxAngularVelocity\)',
        'mAngularVelocity\s*\*=\s*std::pow\(momentumDecayPerSecond,\s*frameDuration\).*std::abs\(mAngularVelocity\)\s*<\s*stopAngularVelocity',
        'MyGUI::MouseButton::Right.*setVerticalFocus\(mVerticalFocus\s*\+\s*static_cast<float>\(mLastPointerY\s*-\s*top\)\s*\*\s*focusUnitsPerPixel\)',
        'setZoom\(mZoom\s*\+\s*\(rel\s*>\s*0\s*\?\s*wheelZoomStep\s*:\s*-wheelZoomStep\)\)',
        'mZoom\s*=\s*std::clamp\(zoom,\s*minZoom,\s*mMaxZoom\)',
        'mVerticalFocus\s*=\s*std::clamp\(focus,\s*-mMaxVerticalFocus,\s*mMaxVerticalFocus\)',
        'mAngularVelocity\s*=\s*0\.f.*setAngle\(0\.f\).*setZoom\(0\.f\).*setVerticalFocus\(0\.f\)',
        'mPreview->setZoom\(mZoom\)',
        'mPreview->setVerticalFocus\(mVerticalFocus\)',
        'mInspectionZoom\s*=\s*std::clamp\(zoom,\s*0\.f,\s*1\.f\)',
        'mInspectionFocusOffset\s*=\s*std::clamp\(focusOffset,\s*bodyPreviewMinFocusOffset,\s*bodyPreviewMaxFocusOffset\)',
        'bodyPreviewFovYDegrees.*bodyPreviewVerticalPadding.*getBodyPreviewFitDistance',
        'osg::ComputeBoundsVisitor.*std::max\(fullDistance,\s*getBodyPreviewFitDistance\(height\)\).*fullTargetZ\s*=\s*\(bounds\.zMin\(\)\s*\+\s*bounds\.zMax\(\)\)\s*\*\s*0\.5f',
        'fullDistance.*bodyPreviewCloseDistance.*smoothZoom',
        'mRotating\s*=\s*true.*mRotating\s*=\s*false',
        'mFocusing\s*=\s*true.*mFocusing\s*=\s*false',
        'mAvatarPreviewController\.bind\(mAvatarPreviewImage,\s*mAvatarPreview\.get\(\)\)',
        'mAvatarPreviewController\.bind\(mPreviewImage,\s*mPreview\.get\(\)\)',
        'mRaceDialog->onFrame\(duration\)',
        'float\s+getAngle\(\)\s+const\s*\{\s*return\s+mAngle;\s*\}',
        'mAvatarPreviewController\.setAngle\(0\.f\)',
        'mAvatarPreviewController\.update\(duration\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP custom class creator uses the modern avatar-backed MMO shell" -Text ($classDialog + "`n" + $characterCreation + "`n" + $createClassLayout + "`n" + $selectSpecializationLayout + "`n" + $selectAttributeLayout + "`n" + $selectSkillLayout + "`n" + $classDescriptionLayout) `
    -Patterns @(
        'CreateClassDialog::CreateClassDialog\(osg::Group\*\s+parent,\s+Resource::ResourceSystem\*\s+resourceSystem\)',
        'std::make_unique<CreateClassDialog>\(mParent,\s*mResourceSystem\)',
        'mCreateClassDialog->onFrame\(duration\)',
        'name="ClassIdentityT".*name="EditName".*name="DescriptionButton"',
        'name="ClassAvatarT".*name="AvatarPreviewImage"',
        'name="CreatorStageRail".*name="CreatorStageClass".*TextColour"\s+value="1 0\.86 0\.42"',
        'name="MajorSkill0".*name="MajorSkill4".*name="MinorSkill0".*name="MinorSkill4"',
        'mAvatarPreview\s*=\s*std::make_unique<MWRender::RaceSelectionPreview>.*RaceSelectionPreview::PreviewMode::Body',
        'mAvatarPreviewController\.bind\(mAvatarPreviewImage,\s*mAvatarPreview\.get\(\)\)',
        'mAvatarPreviewController\.update\(duration\)',
        'mAvatarPreviewImage->setRenderItemTexture\(mAvatarPreviewTexture\.get\(\)\)',
        'position="0 0 900 430".*name="CreatorStageRail".*name="Specialization0".*name="Specialization2"',
        'position="0 0 900 560".*name="CreatorStageRail".*name="AttributePanel".*name="Attributes"',
        'position="0 0 900 560".*name="CombatSkillPanel".*name="CombatSkills".*name="MagicSkillPanel".*name="MagicSkills".*name="StealthSkillPanel".*name="StealthSkills"',
        'position="0 0 900 560".*name="DescriptionTitle".*name="CreatorStageRail".*name="DescriptionPanel".*name="TextEdit".*name="OKButton"'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP class-route prompts use dedicated modern creator dialogs" -Text ($classChoiceLayout + "`n" + $infoBoxLayout + "`n" + $generateClassResultLayout + "`n" + $classDialog + "`n" + $characterCreation + "`n" + $builtinDataCMake) `
    -Patterns @(
        'mygui/openmw_chargen_class_choice\.layout',
        'ClassChoiceDialog::ClassChoiceDialog\(osg::Group\*\s+parent,\s+Resource::ResourceSystem\*\s+resourceSystem\)\s*: WindowModal\("openmw_chargen_class_choice\.layout"\)',
        'std::make_unique<ClassChoiceDialog>\(mParent,\s*mResourceSystem\)',
        'mClassChoiceDialog->onFrame\(duration\)',
        'position="0 0 1320 760".*name="_Main".*mygui\\characterselect\\textures\\communitymp_character_select_frame\.png.*name="ClassRouteTitle".*name="ClassRouteAvatarT".*name="ClassRouteAvatarPanel".*name="AvatarPreviewImage".*name="CreatorStageRail"',
        'name="ClassRouteBriefT".*Caption"\s+value="DETAILS".*name="ClassRouteBriefPanel".*name="ClassRouteBriefText".*name="ClassRouteSessionText".*name="ClassRouteSkillsText".*name="ClassRouteNextText"',
        'name="GenerateRouteCard".*name="GenerateButton".*name="PickRouteCard".*name="PickButton".*name="CreateRouteCard".*name="CreateButton".*name="BackButton"',
        'mAvatarPreview\s*=\s*std::make_unique<MWRender::RaceSelectionPreview>.*RaceSelectionPreview::PreviewMode::Body',
        'mAvatarPreviewController\.bind\(mAvatarPreviewImage,\s*mAvatarPreview\.get\(\)\)',
        'mAvatarPreviewController\.update\(duration\)',
        'mAvatarPreviewImage->setRenderItemTexture\(mAvatarPreviewTexture\.get\(\)\)',
        'bindButton\("GenerateButton",\s*"Answer Questions"',
        'bindButton\("PickButton",\s*"Pick Preset Class"',
        'bindButton\("CreateButton",\s*"Create Custom Class"',
        'bindButton\("BackButton",\s*windowManager->getGameSettingString\("sBack"',
        'eventButtonSelected\(static_cast<int>\(i\)\)',
        'onButtonClicked\(mButtons\[Class_Back\]\)',
        'position="0 0 900 560".*name="GeneratedClassT".*name="CreatorStageRail"',
        'name="GeneratedClassImagePanel".*name="ClassImage".*name="GeneratedClassSummaryPanel".*name="ReflectT".*name="ClassName"',
        'GenerateClassResultDialog::GenerateClassResultDialog\(\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP generated class questionnaire uses a modern creator modal" -Text ($generateClassQuestionLayout + "`n" + $classDialog + "`n" + $characterCreation + "`n" + $builtinDataCMake) `
    -Patterns @(
        'mygui/openmw_chargen_generate_class_question\.layout',
        'InfoBoxDialog::InfoBoxDialog\(\)\s*: WindowModal\("openmw_chargen_generate_class_question\.layout"\)',
        'position="0 0 900 560".*name="ClassQuestionTitle".*name="CreatorStageRail"',
        'name="TextBox".*name="Text".*position="32 292 836 214"\s+name="ButtonBar"',
        'MyGUI::IntCoord\(0,\s*0,\s*mButtonBar->getWidth\(\),\s*58\)',
        'createWidget<MyGUI::Button>\(\s*"SandTextButton"',
        'button->setSize\(mButtonBar->getWidth\(\),\s*58\)',
        'coord\.top\s*\+=\s*button->getHeight\(\)\s*\+\s*10',
        'mGenerateClassQuestionDialog\s*=\s*std::make_unique<InfoBoxDialog>\(\)',
        'mGenerateClassQuestionDialog->setText\(step\.mText\)',
        'mGenerateClassQuestionDialog->setButtons\(buttons\)'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP suppresses HUD and chat through logged-in character generation" -Text ($guiController + "`n" + $clientMain) `
    -Patterns @(
        'bool\s+mwmp::GUIController::shouldSuppressPreCharacterUi\(\)\s+const',
        'localPlayer\s*!=\s*nullptr\s*&&\s*localPlayer->isLoggedIn\(\)\s*&&\s*!localPlayer->hasLoadedCharacter\(\)',
        'mPreCharacterUiSuppressed',
        'mPreCharacterRestoreHudVisible\s*=\s*mCharacterPresentationActive\s*\?\s*mRestoreHudVisible\s*:\s*windowManager->isHudVisible\(\)',
        'windowManager->setHudVisibility\(false\);\s*setChatVisible\(false\);',
        'windowManager->setHudVisibility\(mPreCharacterRestoreHudVisible\);\s*setChatVisible\(true\);',
        'if\s*\(!mLocalPlayer->processCharGen\(\)\).*setAIActive\(false\);.*return;'
    ) `
    -Missing $missing

Test-AllPatterns -Name "CommunityMP creator layouts have deterministic geometry verification coverage" -Text $geometryVerifier `
    -Patterns @(
        'Assert-RootSize',
        'Assert-WidgetAspect',
        'Assert-HeadingGutters',
        'Assert-MwBoxSiblingOverlap',
        'Assert-StaticTextFit',
        'Assert-CreatorStageRail',
        'Assert-AvatarPreviewCameraMath',
        'Assert-GeneratedClassQuestionCapacity',
        'root widget must be named _Main',
        'root widget must be Window or Widget',
        'root widget must live on a modal/window layer',
        'ActiveStage',
        'CreatorStageRail',
        'Height = 760',
        'rootRect\.W\s+-eq\s+1320',
        'bodyPreviewFullDistance',
        'bodyPreviewFovYDegrees',
        'getBodyPreviewFitDistance',
        'bodyPreviewMinFocusOffset',
        'maxAngularVelocity',
        'momentumDecayPerSecond',
        'smoothZoom',
        'openmw_chargen_name\.layout',
        'openmw_chargen_race\.layout',
        'openmw_chargen_class_choice\.layout',
        'openmw_chargen_create_class\.layout',
        'openmw_chargen_generate_class_question\.layout',
        'openmw_chargen_review\.layout',
        'communitymp_character_select\.layout',
        'Layouts checked',
        'Camera checks',
        'Questionnaire capacity checks',
        'Issues:\s*\$'
    ) `
    -Missing $missing

Test-RequiredFile -Name "Bundled character select frame asset is present" -RelativePath "files\mygui\characterselect\textures\communitymp_character_select_frame.png" -Missing $missing
Test-RequiredFile -Name "Bundled character select portrait asset is present" -RelativePath "files\mygui\characterselect\textures\communitymp_character_portrait.png" -Missing $missing
Test-RequiredFile -Name "Bundled character select portrait frame asset is present" -RelativePath "files\mygui\characterselect\textures\communitymp_character_portrait_frame.png" -Missing $missing
Test-RequiredFile -Name "Bundled character select button atlas is present" -RelativePath "files\mygui\characterselect\textures\communitymp_character_button_atlas.png" -Missing $missing

Test-Pattern -Name "Install and deploy validation require CommunityMP login resources in packaged runtime resources" -Text ($installVerifier + "`n" + $deployScript) `
    -Pattern 'Assert-LoginResources.*resources\\vfs\\mygui\\login\\communitymp_login\.layout.*resources\\vfs\\mygui\\login\\communitymp_login\.skin\.xml.*resources\\vfs\\mygui\\login\\communitymp_login\.xml.*resources\\vfs\\mygui\\login\\textures\\communitymp-ashlands-hero\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-causeway\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-gathering\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-logo\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp-server-hall\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_atmosphere\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_panel\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_button_atlas\.png.*resources\\vfs\\music\\communitymp\\nightinthedesertmix\.ogg.*resources\\vfs\\music\\communitymp\\nightinthedesertmix\.CREDITS\.txt.*"CommunityMP",\s*"Server account",\s*"Account username",\s*"Account password",\s*"Remember me",\s*"Server join password".*Assert-LoginResources.*resources\\vfs\\mygui\\login\\communitymp_login\.layout.*resources\\vfs\\mygui\\login\\communitymp_login\.skin\.xml.*resources\\vfs\\mygui\\login\\communitymp_login\.xml.*resources\\vfs\\mygui\\login\\textures\\communitymp-ashlands-hero\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-causeway\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-gathering\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp-logo\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp-server-hall\.jpg.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_atmosphere\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_panel\.png.*resources\\vfs\\mygui\\login\\textures\\communitymp_login_button_atlas\.png.*resources\\vfs\\music\\communitymp\\nightinthedesertmix\.ogg.*resources\\vfs\\music\\communitymp\\nightinthedesertmix\.CREDITS\.txt.*"CommunityMP",\s*"Server account",\s*"Account username",\s*"Account password",\s*"Remember me",\s*"Server join password"' `
    -Missing $missing

Test-RequiredFile -Name "Bundled CommunityMP login ashlands hero asset is present" -RelativePath "files\mygui\login\textures\communitymp-ashlands-hero.jpg" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login causeway background asset is present" -RelativePath "files\mygui\login\textures\communitymp-causeway.jpg" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login gathering asset is present" -RelativePath "files\mygui\login\textures\communitymp-gathering.jpg" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login logo asset is present" -RelativePath "files\mygui\login\textures\communitymp-logo.png" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login server hall asset is present" -RelativePath "files\mygui\login\textures\communitymp-server-hall.jpg" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login skin is present" -RelativePath "files\mygui\login\communitymp_login.skin.xml" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login atmosphere texture is present" -RelativePath "files\mygui\login\textures\communitymp_login_atmosphere.png" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login panel texture is present" -RelativePath "files\mygui\login\textures\communitymp_login_panel.png" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login button atlas is present" -RelativePath "files\mygui\login\textures\communitymp_login_button_atlas.png" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login music asset is present" -RelativePath "files\data\music\communitymp\nightinthedesertmix.ogg" -Missing $missing
Test-RequiredFile -Name "Bundled CommunityMP login music attribution is present" -RelativePath "files\data\music\communitymp\nightinthedesertmix.CREDITS.txt" -Missing $missing

Test-Pattern -Name "Bundled login music is installed with attribution and documented customization" -Text ($builtinDataCMake + "`n" + $loginCustomizationDocs + "`n" + $licensingNotes) `
    -Pattern 'music/communitymp/nightinthedesertmix\.CREDITS\.txt.*music/communitymp/nightinthedesertmix\.ogg.*loginLogo\s*=\s*mygui\\login\\textures\\communitymp-logo\.png.*loginMusic\s*=\s*music/communitymp/nightinthedesertmix\.ogg.*leave the value empty to keep the login prompt silent.*character-selection lobby.*CC-BY 3\.0.*nightinthedesertmix\.CREDITS\.txt' `
    -Missing $missing

Test-Pattern -Name "CommunityMP packaging names release artifacts and requires full distribution modules by default" -Text ($packageScript + "`n" + $deployScript + "`n" + $installVerifier) `
    -Pattern '(?=.*PackagePrefix\s*=\s*"CommunityMP-testing")(?=.*\$packageName\s*=\s*"\$PackagePrefix-\$shortCommit-Windows-x64")(?=.*Assert-NoDebugArtifacts)(?=.*"\*\.pdb")(?=.*scripts\\deploy-tes3mp\.ps1)(?=.*Alias\("IncludeBrowser"\))(?=.*\$includeHub\s*=\s*-not\s+\$Minimal\s+-or\s+\$IncludeHub)(?=.*\$includeMaster\s*=\s*-not\s+\$Minimal\s+-or\s+\$IncludeMaster)(?=.*\$includeTools\s*=\s*-not\s+\$Minimal)(?=.*"communitymp")(?=.*"communitymp-client\.exe")(?=.*"communitymp-server\.exe")(?=.*"communitymp-hub")(?=.*"masterserver")(?=.*"openmw-cs")(?=.*"openmw-launcher")(?=.*"openmw-wizard")(?=.*"openmw-navmeshtool")(?=.*"openmw-bulletobjecttool")(?=.*"openmw-iniimporter")(?=.*"openmw-essimporter")(?=.*\$requireFullDistribution\s*=\s*-not\s+\$Minimal)(?=.*Assert-InstalledFile\s+"communitymp-client\.exe")(?=.*Assert-InstalledFile\s+"communitymp-server\.exe")(?=.*Assert-NotInstalledFile\s+"tes3mp\.exe")(?=.*Assert-NotInstalledFile\s+"tes3mp-server\.exe")(?=.*Assert-NotInstalledFile\s+"communitymp-browser\.exe")(?=.*Assert-InstalledFile\s+"masterserver\.exe")(?=.*Assert-InstalledFile\s+"communitymp-hub\.exe")(?=.*Assert-InstalledFile\s+\$requiredFile)' `
    -Missing $missing

Test-Pattern -Name "Component coverage pins fallback dialog text and handshake-password login without fallback dialogs" -Text $componentTests `
    -Pattern 'GuiHelperKeepsAccountDialogIdsAndText.*GetCharacterSlotPreviewMetadata.*b_n_dark elf_m_head_01.*ListBoxWithMetadata.*guiHelper\.ShowLogin\(2\).*guiHelper\.ShowRegister\(2\).*guiHelper\.ShowCharacterList\(2\).*Character slots keep separate inventory,\s*journal,\s*topics and quest state.*\+ Create new character.*EventHandlerHandshakeLoginLoadsExistingCharacterWithoutFallbackDialog.*GetHandshakePasswordHash.*ClearHandshakePasswordHash.*eventHandler\.OnGUIAction\(31,\s*guiHelper\.ID\.CHARACTERLIST,\s*"0"\).*DisconnectAuthenticatedAccountSessions:ServerAccount:31.*loadFromDriveCount\s*==\s*2.*ShowCharacterList:31:1:SavedCharacter.*ShowLogin:31.*==\s*nil.*ShowRegister:31.*==\s*nil' `
    -Missing $missing

Test-Pattern -Name "Component coverage pins bad handshake-password fallback to retry dialog" -Text $componentTests `
    -Pattern '(?=.*EventHandlerHandshakeWrongPasswordFallsBackToRetryDialog)(?=.*GetHandshakePasswordHash)(?=.*return\s+"wrong")(?=.*ClearHandshakePasswordHash:32)(?=.*passwordHash\s*=\s*"hash:correctsalt")(?=.*Incorrect password!)(?=.*ShowLogin:32)(?=.*\|FinishLogin\|",\s*1,\s*true\)\s*==\s*nil)(?=.*handler:OnPlayerFinishLogin",\s*1,\s*true\)\s*==\s*nil)(?=.*handler:OnPlayerAuthentified",\s*1,\s*true\)\s*==\s*nil)' `
    -Missing $missing

Test-Pattern -Name "Runtime smoke pins bad handshake-password retry before successful relog" -Text $runtimeSmoke `
    -Pattern '(?=.*badClientPasswordHash\s*=\s*"bad-client-password-hash")(?=.*badRelogPid\s*=\s*43)(?=.*requestPid\s*==\s*badRelogPid.*return\s+badClientPasswordHash)(?=.*ClearHandshakePasswordHash.*counts\.handshakeClear\s*=\s*counts\.handshakeClear\s*\+\s*1)(?=.*ShowLogin.*unexpected login prompt pid)(?=.*ShowCharacterList.*unexpected character list pid)(?=.*eventHandler\.OnPlayerConnect\(badRelogPid,\s*accountName\))(?=.*bad existing-account handshake logged in the player)(?=.*FinishLogin handler count after bad relog)(?=.*eventHandler\.OnPlayerConnect\(relogPid,\s*accountName\))(?=.*existing-account handshake logged in before character selection)(?=.*eventHandler\.OnGUIAction\(relogPid,\s*guiHelper\.ID\.CHARACTERLIST,\s*"0"\))(?=.*existing-account character selection did not finish login)(?=.*character list count)(?=.*auth handler count after bad relog)(?=.*registered\|bad-password-retry\|relog)' `
    -Missing $missing

Write-Host "TES3MP account/login UX sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 53"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
