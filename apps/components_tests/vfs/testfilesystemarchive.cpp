#include <components/vfs/filesystemarchive.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace VFS
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
                        / ("openmw-vfs-filesystemarchive-"
                            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
                            + std::to_string(i));
                    std::error_code ec;
                    if (std::filesystem::create_directory(mPath, ec))
                        return;
                }

                throw std::runtime_error("Failed to create temporary VFS test directory");
            }

            ~TempDirectory()
            {
                std::error_code ec;
                std::filesystem::remove_all(mPath, ec);
            }
        };

        void writeFile(const std::filesystem::path& path)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary);
            stream << "data";
        }

        TEST(VFSFileSystemArchive, shouldOmitUnixHiddenFilesAndDirectories)
        {
            TempDirectory temp;

            writeFile(temp.mPath / "Music" / "theme.mp3");
            writeFile(temp.mPath / "Textures" / "tx.dds");
            writeFile(temp.mPath / ".root-hidden");
            writeFile(temp.mPath / "Music" / ".DS_Store");
            writeFile(temp.mPath / "Music" / ".hidden-album" / "secret.mp3");
            writeFile(temp.mPath / ".git" / "config");

            const FileSystemArchive archive(temp.mPath);

            EXPECT_TRUE(archive.contains(Path::NormalizedView("music/theme.mp3")));
            EXPECT_TRUE(archive.contains(Path::NormalizedView("textures/tx.dds")));
            EXPECT_FALSE(archive.contains(Path::NormalizedView(".root-hidden")));
            EXPECT_FALSE(archive.contains(Path::NormalizedView("music/.ds_store")));
            EXPECT_FALSE(archive.contains(Path::NormalizedView("music/.hidden-album/secret.mp3")));
            EXPECT_FALSE(archive.contains(Path::NormalizedView(".git/config")));
        }
    }
}
