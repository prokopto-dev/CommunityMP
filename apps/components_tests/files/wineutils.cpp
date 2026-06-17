#include <components/files/wineutils.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Files::Wine
{
    namespace
    {
        struct TempDirectory
        {
            std::filesystem::path mPath;

            TempDirectory()
            {
                const auto base = std::filesystem::temp_directory_path();
                for (int i = 0; i < 100; ++i)
                {
                    mPath = base
                        / ("openmw-wineutils-"
                            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
                            + std::to_string(i));
                    std::error_code ec;
                    if (std::filesystem::create_directory(mPath, ec))
                        return;
                }

                throw std::runtime_error("Failed to create temporary Wine utils test directory");
            }

            ~TempDirectory()
            {
                std::error_code ec;
                std::filesystem::permissions(mPath / ".wine", std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::add, ec);
                std::filesystem::remove_all(mPath, ec);
            }
        };

        void writeFile(const std::filesystem::path& path)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary);
            stream << "data";
        }

        TEST(WineUtilsTest, missingDefaultWinePrefixIsIgnored)
        {
            TempDirectory temp;

            EXPECT_TRUE(getInstallPaths(temp.mPath).empty());
        }

        TEST(WineUtilsTest, inaccessibleDefaultWinePrefixIsIgnored)
        {
#ifdef _WIN32
            GTEST_SKIP() << "POSIX permission bits are required to reproduce an inaccessible ~/.wine directory";
#else
            TempDirectory temp;
            const std::filesystem::path winePath = temp.mPath / ".wine";
            writeFile(winePath / "system.reg");

            std::error_code ec;
            std::filesystem::permissions(winePath, std::filesystem::perms::none, ec);
            if (ec)
                GTEST_SKIP() << "Unable to remove permissions from temporary ~/.wine directory: " << ec.message();

            EXPECT_TRUE(getInstallPaths(temp.mPath).empty());
#endif
        }
    }
}
