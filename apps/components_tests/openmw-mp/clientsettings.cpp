#include <components/openmw-mp/ClientSettings.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    using Settings::CategorySetting;
    using Settings::CategorySettingValueMap;

    struct ScopedTempFile
    {
        explicit ScopedTempFile(std::filesystem::path path)
            : mPath(std::move(path))
        {
        }

        ~ScopedTempFile()
        {
            std::error_code error;
            std::filesystem::remove(mPath, error);
        }

        std::filesystem::path mPath;
    };

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        std::stringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }
}

TEST(ClientSettingsTest, filtersPollutedOpenMwSettingsFromTes3mpClientSettings)
{
    const CategorySettingValueMap defaults{
        { CategorySetting("General", "destinationAddress"), "localhost" },
        { CategorySetting("General", "playerName"), "" },
        { CategorySetting("Chat", "keySay"), "Y" },
    };

    const CategorySettingValueMap source{
        { CategorySetting("Camera", "third person camera distance"), "30" },
        { CategorySetting("General", "playerName"), "alex" },
        { CategorySetting("General", "texture mipmap"), "linear" },
        { CategorySetting("Chat", "keySay"), "T" },
    };

    const CategorySettingValueMap filtered = mwmp::ClientSettings::filterForClientDefaults(source, defaults);

    EXPECT_EQ(filtered,
        CategorySettingValueMap({
            { CategorySetting("General", "playerName"), "alex" },
            { CategorySetting("Chat", "keySay"), "T" },
        }));
}

TEST(ClientSettingsTest, cleanSaveRemovesPreviouslyPollutedSettings)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("tes3mp-client-settings-test-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    const ScopedTempFile cleanup(path);

    {
        std::ofstream stream(path);
        stream << "[Camera]\n";
        stream << "third person camera distance = 30\n";
        stream << "\n";
        stream << "[General]\n";
        stream << "playerName = old\n";
        stream << "texture mipmap = linear\n";
    }

    mwmp::ClientSettings::saveCleanSettingsFile(path,
        CategorySettingValueMap({
            { CategorySetting("General", "playerName"), "alex" },
            { CategorySetting("Chat", "keySay"), "T" },
        }));

    const std::string saved = readFile(path);

    EXPECT_NE(saved.find("[General]"), std::string::npos);
    EXPECT_NE(saved.find("playerName = alex"), std::string::npos);
    EXPECT_NE(saved.find("[Chat]"), std::string::npos);
    EXPECT_NE(saved.find("keySay = T"), std::string::npos);
    EXPECT_EQ(saved.find("[Camera]"), std::string::npos);
    EXPECT_EQ(saved.find("third person camera distance"), std::string::npos);
    EXPECT_EQ(saved.find("texture mipmap"), std::string::npos);
}
