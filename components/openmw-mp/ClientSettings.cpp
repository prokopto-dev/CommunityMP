#include "ClientSettings.hpp"

#include <components/files/configurationmanager.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>

#include <array>
#include <fstream>
#include <stdexcept>

namespace
{
    constexpr char settingsFileName[] = "communitymp-client.cfg";
    constexpr char legacySettingsFileName[] = "tes3mp-client.cfg";
    constexpr std::array<const char*, 2> defaultSettingsFileNames = {
        "communitymp-client-default.cfg",
        "tes3mp-client-default.cfg",
    };

    std::filesystem::path getDefaultSettingsPath(const Files::ConfigurationManager& cfgMgr)
    {
        for (const char* fileName : defaultSettingsFileNames)
        {
            const std::filesystem::path localDefault = cfgMgr.getLocalPath() / fileName;
            if (std::filesystem::exists(localDefault))
                return localDefault;

            const std::filesystem::path globalDefault = cfgMgr.getGlobalPath() / fileName;
            if (std::filesystem::exists(globalDefault))
                return globalDefault;
        }

        throw std::runtime_error(
            "No default settings file found! Make sure \"communitymp-client-default.cfg\" was properly installed.");
    }

    std::filesystem::path getPreferredSettingsPath(const Files::ConfigurationManager& cfgMgr)
    {
        return cfgMgr.getUserConfigPath() / settingsFileName;
    }

    std::filesystem::path getReadableSettingsPath(const Files::ConfigurationManager& cfgMgr)
    {
        const std::filesystem::path preferred = getPreferredSettingsPath(cfgMgr);
        if (std::filesystem::exists(preferred))
            return preferred;

        const std::filesystem::path legacy = cfgMgr.getUserConfigPath() / legacySettingsFileName;
        if (std::filesystem::exists(legacy))
            return legacy;

        return preferred;
    }

    Settings::CategorySettingValueMap loadDefaultSettings(Settings::SettingsFileParser& parser,
        const Files::ConfigurationManager& cfgMgr)
    {
        Settings::CategorySettingValueMap defaults;
        parser.loadSettingsFile(getDefaultSettingsPath(cfgMgr), defaults, false, false);
        return defaults;
    }

    Settings::CategorySettingValueMap collectCommunityMpUserSettings(const Settings::CategorySettingValueMap& defaults)
    {
        return mwmp::ClientSettings::filterForClientDefaults(Settings::Manager::mUserSettings, defaults);
    }

    void applySettings(
        const Settings::CategorySettingValueMap& source, Settings::CategorySettingValueMap& destination)
    {
        for (const auto& [key, value] : source)
            destination[key] = value;
    }
}

Settings::CategorySettingValueMap mwmp::ClientSettings::filterForClientDefaults(
    const Settings::CategorySettingValueMap& source, const Settings::CategorySettingValueMap& defaults)
{
    Settings::CategorySettingValueMap settings;
    for (const auto& [key, value] : source)
    {
        if (defaults.contains(key))
            settings[key] = value;
    }

    return settings;
}

void mwmp::ClientSettings::saveCleanSettingsFile(
    const std::filesystem::path& path, const Settings::CategorySettingValueMap& settings)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());

    std::ofstream stream(path, std::ios::out | std::ios::trunc);
    if (!stream)
        throw std::runtime_error("Failed to open CommunityMP client settings file for writing: " + path.string());

    std::string currentCategory;
    bool wroteSetting = false;

    for (const auto& [key, value] : settings)
    {
        const auto& [category, setting] = key;
        if (category != currentCategory)
        {
            if (!currentCategory.empty())
                stream << '\n';

            currentCategory = category;
            stream << '[' << currentCategory << "]\n";
            wroteSetting = false;
        }

        stream << setting << " = " << value << '\n';
        wroteSetting = true;
    }

    if (!settings.empty() && wroteSetting)
        stream.flush();
}

std::filesystem::path mwmp::ClientSettings::load(const Files::ConfigurationManager& cfgMgr)
{
    Settings::SettingsFileParser parser;

    const Settings::CategorySettingValueMap defaults = loadDefaultSettings(parser, cfgMgr);
    applySettings(filterForClientDefaults(defaults, defaults), Settings::Manager::mDefaultSettings);

    const std::filesystem::path settingsPath = getReadableSettingsPath(cfgMgr);
    if (std::filesystem::exists(settingsPath))
    {
        Settings::CategorySettingValueMap userSettings;
        parser.loadSettingsFile(settingsPath, userSettings, false, true);
        applySettings(filterForClientDefaults(userSettings, defaults), Settings::Manager::mUserSettings);
    }

    return settingsPath;
}

std::filesystem::path mwmp::ClientSettings::load()
{
    Files::ConfigurationManager cfgMgr;
    return load(cfgMgr);
}

void mwmp::ClientSettings::save(const Files::ConfigurationManager& cfgMgr)
{
    Settings::SettingsFileParser parser;

    const Settings::CategorySettingValueMap defaults = loadDefaultSettings(parser, cfgMgr);
    saveCleanSettingsFile(getPreferredSettingsPath(cfgMgr), collectCommunityMpUserSettings(defaults));
}

void mwmp::ClientSettings::save()
{
    Files::ConfigurationManager cfgMgr;
    save(cfgMgr);
}

void mwmp::ClientSettings::removeUserSettingsFromRuntimeStore(const Files::ConfigurationManager& cfgMgr)
{
    Settings::SettingsFileParser parser;

    const Settings::CategorySettingValueMap defaults = loadDefaultSettings(parser, cfgMgr);
    for (const auto& setting : defaults)
        Settings::Manager::mUserSettings.erase(setting.first);
}

void mwmp::ClientSettings::removeUserSettingsFromRuntimeStore()
{
    Files::ConfigurationManager cfgMgr;
    removeUserSettingsFromRuntimeStore(cfgMgr);
}
