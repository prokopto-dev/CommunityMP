#ifndef OPENMW_MP_CLIENTSETTINGS_HPP
#define OPENMW_MP_CLIENTSETTINGS_HPP

#include <components/settings/categories.hpp>

#include <filesystem>

namespace Files
{
    struct ConfigurationManager;
}

namespace mwmp::ClientSettings
{
    Settings::CategorySettingValueMap filterForClientDefaults(const Settings::CategorySettingValueMap& source,
        const Settings::CategorySettingValueMap& defaults);

    void saveCleanSettingsFile(
        const std::filesystem::path& path, const Settings::CategorySettingValueMap& settings);
    std::filesystem::path load(const Files::ConfigurationManager& cfgMgr);
    std::filesystem::path load();
    void save(const Files::ConfigurationManager& cfgMgr);
    void save();
    void removeUserSettingsFromRuntimeStore(const Files::ConfigurationManager& cfgMgr);
    void removeUserSettingsFromRuntimeStore();
}

#endif
