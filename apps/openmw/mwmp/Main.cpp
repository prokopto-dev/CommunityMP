#include <exception>
#include <optional>
#include <string_view>

#include <SDL_messagebox.h>

#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/ClientSettings.hpp>
#include <components/openmw-mp/Endpoint.hpp>
#include <components/openmw-mp/ServerPassword.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>

#include <components/esm3/esmwriter.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/settings/settings.hpp>

#include <extern/PicoSHA2/picosha2.h>

#include "../mwbase/environment.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/worldimp.hpp"

#include "Main.hpp"
#include "Networking.hpp"
#include "LocalSystem.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"
#include "GUIController.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"
#include "RecordHelper.hpp"

using namespace mwmp;

Main *Main::pMain = 0;
std::string Main::address = "";
std::string Main::serverPassword = TES3MP_DEFAULT_PASSW;
std::string Main::playerName = "";
std::string Main::resourceDir = "";
std::string Main::pendingAccountPassword = "";
const Files::ConfigurationManager* clientSettingsConfigManager = nullptr;

std::string Main::getResDir()
{
    return resourceDir;
}

std::string Main::takePendingAccountPassword()
{
    std::string password = std::move(pendingAccountPassword);
    pendingAccountPassword.clear();
    return password;
}

std::string loadSettings()
{
    if (clientSettingsConfigManager)
        return mwmp::ClientSettings::load(*clientSettingsConfigManager).string();

    return mwmp::ClientSettings::load().string();
}

namespace
{
    std::string trimAccountName(std::string name)
    {
        const auto first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};

        const auto last = name.find_last_not_of(" \t\r\n");
        return name.substr(first, last - first + 1);
    }

    void saveLoginCredentialSettings(
        const std::string& accountName, bool rememberCredentials, const std::string& accountPasswordHash)
    {
        try
        {
            Settings::Manager::setString("playerName", "General", accountName);
            Settings::Manager::setBool("rememberAccount", "General", rememberCredentials);
            Settings::Manager::setString(
                "accountPasswordHash", "General", rememberCredentials ? accountPasswordHash : std::string());
            if (clientSettingsConfigManager)
                mwmp::ClientSettings::save(*clientSettingsConfigManager);
            else
                mwmp::ClientSettings::save();
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to save CommunityMP login settings: %s", e.what());
        }
    }

    void setAIActive(bool active)
    {
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();

        if (mechanics->isAIActive() != active)
            mechanics->toggleAI();
    }

    std::string getAccountPasswordHash(std::string_view password)
    {
        std::string passwordHash = picosha2::hash256_hex_string(std::string(password));
        return picosha2::hash256_hex_string(
            passwordHash + picosha2::hash256_hex_string(picosha2::hash256_hex_string(passwordHash)));
    }
}

Main::Main()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s started", Branding::productName);
    mNetworking = new Networking();
    mLocalSystem = new LocalSystem();
    mLocalPlayer = new LocalPlayer();
    mGUIController = new GUIController();
    mCellController = new CellController();

    server = Branding::defaultGameHost;
    port = 25565;
}

Main::~Main()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s stopped", Branding::productName);
    delete mNetworking;
    delete mLocalSystem;
    delete mLocalPlayer;
    delete mCellController;
    delete mGUIController;
    PlayerList::cleanUp();
}

void Main::optionsDesc(boost::program_options::options_description *desc)
{
    namespace bpo = boost::program_options;
    desc->add_options()
            ("connect", bpo::value<std::string>()->default_value(""),
                        "connect to server (e.g. --connect=127.0.0.1:25565)")
            ("password", bpo::value<std::string>()->default_value(TES3MP_DEFAULT_PASSW),
                        "сonnect to a secured server. (e.g. --password=AnyPassword")
            ("name", bpo::value<std::string>()->default_value(""),
                        "CommunityMP account name to send during server login");
}

void Main::configure(const boost::program_options::variables_map& variables, const Files::ConfigurationManager& cfgMgr)
{
    Main::address = variables["connect"].as<std::string>();
    Main::serverPassword = variables["password"].as<std::string>();
    Main::playerName = variables["name"].as<std::string>();
    resourceDir = Files::pathToUnicodeString(variables["resources"].as<Files::MaybeQuotedPath>());
    clientSettingsConfigManager = &cfgMgr;
}

bool Main::init(std::vector<std::string> &content, Files::Collections &collections)
{
    assert(!pMain);
    loadSettings();

    int logLevel = Settings::Manager::getInt("logLevel", "General");
    LOG_INIT(logLevel);

    pMain = new Main();

    if (address.empty())
    {
        pMain->server = Settings::Manager::getString("destinationAddress", "General");
        pMain->port = (unsigned short) Settings::Manager::getInt("port", "General");

        serverPassword = Settings::Manager::getString("password", "General");
        if (serverPassword.empty())
            serverPassword = TES3MP_DEFAULT_PASSW;
        playerName = Settings::Manager::getString("playerName", "General");
    }
    else
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint(address);
        pMain->server = endpoint.host;
        pMain->port = endpoint.port;
    }

    playerName = trimAccountName(playerName);
    serverPassword = normalizeServerPassword(serverPassword);

    const bool rememberCredentials = Settings::Manager::getBool("rememberAccount", "General");
    const std::string rememberedAccountPasswordHash
        = rememberCredentials ? Settings::Manager::getString("accountPasswordHash", "General") : std::string();
    const bool hasRememberedAccountPasswordHash
        = rememberCredentials && !playerName.empty() && !rememberedAccountPasswordHash.empty();

    const std::string serverEndpoint = pMain->server + ":" + std::to_string(pMain->port);
    const std::string initialServerPassword = serverPassword == TES3MP_DEFAULT_PASSW ? std::string() : serverPassword;
    const std::optional<MWBase::LoginCredentials> credentials
        = MWBase::Environment::get().getWindowManager()->promptLoginCredentials(
            serverEndpoint, playerName, initialServerPassword, rememberCredentials, hasRememberedAccountPasswordHash);
    if (!credentials)
    {
        const std::string message = std::string(Branding::productName) + " login was cancelled before connecting.";
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", message.c_str());
        return false;
    }

    playerName = trimAccountName(credentials->accountName);
    serverPassword = normalizeServerPassword(credentials->serverPassword);
    const std::string accountPasswordHash = credentials->useRememberedAccountPasswordHash
        ? rememberedAccountPasswordHash
        : getAccountPasswordHash(credentials->accountPassword);
    pendingAccountPassword = credentials->useRememberedAccountPasswordHash ? std::string() : credentials->accountPassword;

    if (playerName.empty() || accountPasswordHash.empty())
    {
        const std::string message = std::string(Branding::productName)
            + " needs an account username and account password before connecting.";
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", message.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Branding::productName, message.c_str(), nullptr);
        return false;
    }

    saveLoginCredentialSettings(playerName, credentials->rememberCredentials, accountPasswordHash);

    get().mLocalSystem->playerName = playerName;
    get().mLocalSystem->serverPassword = serverPassword;
    get().mLocalSystem->accountPasswordHash = accountPasswordHash;

    pMain->mNetworking->connect(pMain->server, pMain->port, content, collections);

    if (!pMain->mNetworking->isConnected())
        pendingAccountPassword.clear();

    return pMain->mNetworking->isConnected();
}

void Main::postInit()
{
    pMain->mGUIController->setupChat();

    const MWBase::Environment &environment = MWBase::Environment::get();
    environment.getStateManager()->newGame(true);
    setAIActive(false);
    RecordHelper::createPlaceholderInteriorCell();
}

bool Main::isInitialized()
{
    return pMain != nullptr;
}

void Main::destroy()
{
    assert(pMain);

    delete pMain;
    pMain = 0;
    LOG_QUIT();
}

void Main::frame(float dt)
{
    get().getNetworking()->update();

    LocalPlayer* localPlayer = get().getLocalPlayer();
    const bool loggedIn = localPlayer != nullptr && localPlayer->isLoggedIn();

    if (!loggedIn)
        setAIActive(false);

    if (loggedIn)
    {
        PlayerList::update(dt);
        get().getCellController()->updateDedicated(dt);
    }

    get().updateWorld(dt);

    get().getGUIController()->update(dt);
}

bool Main::shouldRunWorldWhilePaused()
{
    return isInitialized() && get().getLocalPlayer() != nullptr && get().getLocalPlayer()->isLoggedIn();
}

void Main::updateWorld(float dt) const
{

    if (!mLocalPlayer->processCharGen())
    {
        setAIActive(false);
        return;
    }

    const bool loggedIn = mLocalPlayer->isLoggedIn();
    setAIActive(loggedIn);

    if (!mInitialLoadedSent)
    {
        mInitialLoadedSent = true;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_LOADED to server for login bootstrap");

        mNetworking->getPlayerPacket(ID_LOADED)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_LOADED)->Send();
        return;
    }

    if (!loggedIn)
        return;

    if (!mInitialPlayerPacketsSent)
    {
        mInitialPlayerPacketsSent = true;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending initial gameplay player state to server");

        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();
        mLocalPlayer->updateStatsDynamic(true);
        get().getGUIController()->setChatVisible(true);
    }
    else
    {
        if (loggedIn)
        {
            mLocalPlayer->update();
            mCellController->updateLocal(false);
        }
    }
}

const Main &Main::get()
{
    return *pMain;
}

Networking *Main::getNetworking() const
{
    return mNetworking;
}

LocalSystem *Main::getLocalSystem() const
{
    return mLocalSystem;
}

LocalPlayer *Main::getLocalPlayer() const
{
    return mLocalPlayer;
}

GUIController *Main::getGUIController() const
{
    return mGUIController;
}

CellController *Main::getCellController() const
{
    return mCellController;
}

bool Main::isValidPacketScript(std::string scriptId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (Utils::vectorContains(worldstate->synchronizedClientScriptIds, scriptId))
        return true;

    return false;
}

bool Main::isValidPacketGlobal(std::string globalId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (Utils::vectorContains(worldstate->synchronizedClientGlobalIds, globalId))
        return true;

    return false;
}

