#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../openmw/OpenMWApplication.hpp"
#include "../openmw-mp/ServerApplication.hpp"
#include "../openmw-mp/SimulationRuntime.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cwctype>
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace
{
#ifdef _WIN32
    using NativeChar = wchar_t;
    using NativeString = std::wstring;
#define COMMUNITYMP_TEXT(value) L##value
#else
    using NativeChar = char;
    using NativeString = std::string;
#define COMMUNITYMP_TEXT(value) value
#endif

    enum class Mode
    {
        Client,
        Server,
    };

    struct ParsedArguments
    {
        Mode mMode = Mode::Client;
        bool mHelp = false;
        bool mPrintTarget = false;
        bool mExternalClient = false;
        bool mExternalServer = false;
        std::vector<NativeString> mForwarded;
    };

    class ScopedCurrentPath
    {
    public:
        explicit ScopedCurrentPath(const std::filesystem::path& path)
            : mPrevious(std::filesystem::current_path())
        {
            std::filesystem::current_path(path);
        }

        ~ScopedCurrentPath()
        {
            std::error_code error;
            std::filesystem::current_path(mPrevious, error);
        }

        ScopedCurrentPath(const ScopedCurrentPath&) = delete;
        ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

    private:
        std::filesystem::path mPrevious;
    };

    NativeString toLower(NativeString value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](NativeChar c) {
#ifdef _WIN32
            return static_cast<NativeChar>(std::towlower(c));
#else
            return static_cast<NativeChar>(std::tolower(static_cast<unsigned char>(c)));
#endif
        });
        return value;
    }

    bool startsWith(const NativeString& value, const NativeString& prefix)
    {
        return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
    }

    bool isClientMode(const NativeString& value)
    {
        const NativeString lower = toLower(value);
        return lower == COMMUNITYMP_TEXT("client") || lower == COMMUNITYMP_TEXT("game");
    }

    bool isServerMode(const NativeString& value)
    {
        const NativeString lower = toLower(value);
        return lower == COMMUNITYMP_TEXT("server") || lower == COMMUNITYMP_TEXT("dedicated")
            || lower == COMMUNITYMP_TEXT("dedicated-server");
    }

    NativeString getModeName(Mode mode)
    {
        return mode == Mode::Server ? COMMUNITYMP_TEXT("server") : COMMUNITYMP_TEXT("client");
    }

    std::filesystem::path getExecutablePath(int argc, NativeChar** argv)
    {
#ifdef _WIN32
        std::wstring buffer(MAX_PATH, L'\0');
        while (true)
        {
            const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (size == 0)
                throw std::runtime_error("failed to read executable path");

            if (size < buffer.size() - 1)
            {
                buffer.resize(size);
                return std::filesystem::path(buffer);
            }

            buffer.resize(buffer.size() * 2);
        }
#elif defined(__linux__)
        std::string buffer(4096, '\0');
        const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (size > 0)
        {
            buffer.resize(static_cast<std::size_t>(size));
            return std::filesystem::path(buffer);
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0)
            return std::filesystem::path(buffer.c_str());
#endif

        if (argc > 0 && argv[0] != nullptr)
            return std::filesystem::absolute(std::filesystem::path(argv[0]));

        return std::filesystem::current_path() / COMMUNITYMP_TEXT("communitymp");
    }

    std::filesystem::path canonicalDirectory(std::filesystem::path path)
    {
        try
        {
            return std::filesystem::weakly_canonical(std::move(path));
        }
        catch (const std::exception&)
        {
            return std::filesystem::absolute(std::move(path));
        }
    }

    NativeString pathToNativeString(const std::filesystem::path& path)
    {
#ifdef _WIN32
        return path.wstring();
#else
        return path.string();
#endif
    }

    std::filesystem::path getTargetPath(const std::filesystem::path& executableDirectory, Mode mode)
    {
#ifdef _WIN32
        return executableDirectory / (mode == Mode::Server ? L"communitymp-server.exe" : L"communitymp-client.exe");
#else
        return executableDirectory / (mode == Mode::Server ? "communitymp-server" : "communitymp-client");
#endif
    }

    void printLine(const NativeString& line)
    {
#ifdef _WIN32
        std::wcout << line << L'\n';
#else
        std::cout << line << '\n';
#endif
    }

    void printError(const NativeString& line)
    {
#ifdef _WIN32
        std::wcerr << line << L'\n';
#else
        std::cerr << line << '\n';
#endif
    }

    void printUsage(const std::filesystem::path& executablePath)
    {
        const NativeString executable = pathToNativeString(executablePath.filename());
        printLine(executable + COMMUNITYMP_TEXT(" [--client|--server] [mode arguments...]"));
        printLine(COMMUNITYMP_TEXT(""));
        printLine(COMMUNITYMP_TEXT("Modes:"));
        printLine(COMMUNITYMP_TEXT("  --client, client            Launch the CommunityMP client (default)."));
        printLine(COMMUNITYMP_TEXT("  --server, server            Launch the dedicated server."));
        printLine(COMMUNITYMP_TEXT("  --dedicated-server          Alias for --server."));
        printLine(COMMUNITYMP_TEXT("  --mode client|server        Explicit mode selector."));
        printLine(COMMUNITYMP_TEXT(""));
        printLine(COMMUNITYMP_TEXT("Launcher options:"));
        printLine(COMMUNITYMP_TEXT("  --external-client           Run client mode using communitymp-client child process."));
        printLine(COMMUNITYMP_TEXT("  --external-server          Run server mode using communitymp-server child process."));
        printLine(COMMUNITYMP_TEXT("  --print-target              Print the resolved child executable and exit."));
        printLine(COMMUNITYMP_TEXT("  --help                      Show this help."));
        printLine(COMMUNITYMP_TEXT(""));
        printLine(COMMUNITYMP_TEXT("Client and server modes run in-process by default."));
        printLine(
            COMMUNITYMP_TEXT("A client and a dedicated server can run together from the same install, but duplicate"));
        printLine(COMMUNITYMP_TEXT("clients or duplicate dedicated servers from that install are refused."));
    }

    ParsedArguments parseArguments(int argc, NativeChar** argv)
    {
        ParsedArguments parsed;

        for (int i = 1; i < argc; ++i)
        {
            const NativeString argument = argv[i] == nullptr ? NativeString() : NativeString(argv[i]);
            const NativeString lower = toLower(argument);

            if (argument == COMMUNITYMP_TEXT("--"))
            {
                for (++i; i < argc; ++i)
                    parsed.mForwarded.emplace_back(argv[i]);
                break;
            }

            if (lower == COMMUNITYMP_TEXT("--help") || lower == COMMUNITYMP_TEXT("-h"))
            {
                parsed.mHelp = true;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--print-target"))
            {
                parsed.mPrintTarget = true;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--external-server"))
            {
                parsed.mExternalServer = true;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--external-client"))
            {
                parsed.mExternalClient = true;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--client") || lower == COMMUNITYMP_TEXT("client"))
            {
                parsed.mMode = Mode::Client;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--server") || lower == COMMUNITYMP_TEXT("server")
                || lower == COMMUNITYMP_TEXT("--dedicated-server") || lower == COMMUNITYMP_TEXT("dedicated-server"))
            {
                parsed.mMode = Mode::Server;
                continue;
            }

            if (lower == COMMUNITYMP_TEXT("--mode"))
            {
                if (++i >= argc)
                    throw std::runtime_error("--mode requires client or server");

                const NativeString mode = argv[i] == nullptr ? NativeString() : NativeString(argv[i]);
                if (isClientMode(mode))
                    parsed.mMode = Mode::Client;
                else if (isServerMode(mode))
                    parsed.mMode = Mode::Server;
                else
                    throw std::runtime_error("--mode requires client or server");
                continue;
            }

            constexpr NativeChar modePrefix[] = COMMUNITYMP_TEXT("--mode=");
            if (startsWith(lower, modePrefix))
            {
                const NativeString mode = argument.substr(NativeString(modePrefix).size());
                if (isClientMode(mode))
                    parsed.mMode = Mode::Client;
                else if (isServerMode(mode))
                    parsed.mMode = Mode::Server;
                else
                    throw std::runtime_error("--mode requires client or server");
                continue;
            }

            parsed.mForwarded.push_back(argument);
        }

        return parsed;
    }

    std::uint64_t hashNativeString(const NativeString& value)
    {
        constexpr std::uint64_t offset = 1469598103934665603ull;
        constexpr std::uint64_t prime = 1099511628211ull;

        std::uint64_t hash = offset;
        for (const NativeChar c : value)
        {
            std::uint64_t bits = static_cast<std::uint64_t>(c);
            for (std::size_t i = 0; i < sizeof(NativeChar); ++i)
            {
                hash ^= bits & 0xffu;
                hash *= prime;
                bits >>= 8;
            }
        }

        return hash;
    }

    NativeString toHex(std::uint64_t value)
    {
        constexpr NativeChar digits[] = COMMUNITYMP_TEXT("0123456789abcdef");
        NativeString result(16, COMMUNITYMP_TEXT('0'));
        for (int i = 15; i >= 0; --i)
        {
            result[static_cast<std::size_t>(i)] = digits[value & 0xfu];
            value >>= 4;
        }
        return result;
    }

    NativeString getInstallIdentity(const std::filesystem::path& executableDirectory)
    {
        NativeString identity = pathToNativeString(canonicalDirectory(executableDirectory));
#ifdef _WIN32
        std::replace(identity.begin(), identity.end(), L'/', L'\\');
        identity = toLower(std::move(identity));
#endif
        return toHex(hashNativeString(identity));
    }

    class ModeLock
    {
    public:
        ModeLock() = default;
        ModeLock(const ModeLock&) = delete;
        ModeLock& operator=(const ModeLock&) = delete;

        ~ModeLock()
        {
#ifdef _WIN32
            if (mHandle != nullptr)
                CloseHandle(mHandle);
#else
            if (mFd != -1)
                close(mFd);
#endif
        }

        bool acquire(const std::filesystem::path& executableDirectory, Mode mode)
        {
            const NativeString name = COMMUNITYMP_TEXT("CommunityMP-") + getInstallIdentity(executableDirectory)
                + COMMUNITYMP_TEXT("-") + getModeName(mode);

#ifdef _WIN32
            const NativeString mutexName = COMMUNITYMP_TEXT("Local\\") + name;
            mHandle = CreateMutexW(nullptr, TRUE, mutexName.c_str());
            if (mHandle == nullptr)
                throw std::runtime_error("failed to create CommunityMP mode mutex");

            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle(mHandle);
                mHandle = nullptr;
                return false;
            }

            return true;
#else
            const std::filesystem::path lockPath = std::filesystem::temp_directory_path() / (name + ".lock");
            mFd = open(lockPath.c_str(), O_RDWR | O_CREAT, 0600);
            if (mFd == -1)
                throw std::runtime_error(std::string("failed to open CommunityMP mode lock: ") + std::strerror(errno));

            if (flock(mFd, LOCK_EX | LOCK_NB) == -1)
            {
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                    return false;
                throw std::runtime_error(std::string("failed to lock CommunityMP mode: ") + std::strerror(errno));
            }

            const std::string pid = std::to_string(getpid()) + "\n";
            ftruncate(mFd, 0);
            write(mFd, pid.data(), pid.size());
            return true;
#endif
        }

    private:
#ifdef _WIN32
        HANDLE mHandle = nullptr;
#else
        int mFd = -1;
#endif
    };

#ifdef _WIN32
    std::string nativeToUtf8(const NativeString& value)
    {
        if (value.empty())
            return {};

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0,
            nullptr, nullptr);
        if (size <= 0)
            throw std::runtime_error("failed to convert command line argument to UTF-8");

        std::string result(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size,
                nullptr, nullptr)
            != size)
            throw std::runtime_error("failed to convert command line argument to UTF-8");

        return result;
    }
#else
    std::string nativeToUtf8(const NativeString& value)
    {
        return value;
    }
#endif

    class Utf8Argv
    {
    public:
        Utf8Argv(const std::filesystem::path& executablePath, const std::vector<NativeString>& arguments)
        {
            mStorage.reserve(arguments.size() + 1);
            mStorage.push_back(nativeToUtf8(pathToNativeString(executablePath)));
            for (const NativeString& argument : arguments)
                mStorage.push_back(nativeToUtf8(argument));

            mArgv.reserve(mStorage.size() + 1);
            for (std::string& argument : mStorage)
                mArgv.push_back(argument.data());
            mArgv.push_back(nullptr);
        }

        int argc() const
        {
            return static_cast<int>(mStorage.size());
        }

        char** argv()
        {
            return mArgv.data();
        }

    private:
        std::vector<std::string> mStorage;
        std::vector<char*> mArgv;
    };

    class UnifiedOpenMwSimulationRuntime final : public mwmp::SimulationRuntime
    {
    public:
        UnifiedOpenMwSimulationRuntime()
            : mwmp::SimulationRuntime(mwmp::SimulationRuntimeKind::OpenMwHeadless)
        {
        }
    };

    std::unique_ptr<mwmp::SimulationRuntime> createUnifiedOpenMwSimulationRuntime()
    {
        return std::make_unique<UnifiedOpenMwSimulationRuntime>();
    }

    class ScopedSimulationRuntimeFactory
    {
    public:
        explicit ScopedSimulationRuntimeFactory(mwmp::SimulationRuntimeFactory factory)
        {
            mwmp::setSimulationRuntimeFactory(factory);
        }

        ~ScopedSimulationRuntimeFactory()
        {
            mwmp::setSimulationRuntimeFactory(nullptr);
        }

        ScopedSimulationRuntimeFactory(const ScopedSimulationRuntimeFactory&) = delete;
        ScopedSimulationRuntimeFactory& operator=(const ScopedSimulationRuntimeFactory&) = delete;
    };

    int runServerInProcess(const std::filesystem::path& executablePath, const std::filesystem::path& workingDirectory,
        const std::vector<NativeString>& arguments)
    {
        ScopedSimulationRuntimeFactory simulationRuntimeFactory(&createUnifiedOpenMwSimulationRuntime);
        ScopedCurrentPath currentPath(workingDirectory);
        Utf8Argv argv(executablePath, arguments);
        return runCommunityMpDedicatedServer(argv.argc(), argv.argv());
    }

    int runClientInProcess(const std::filesystem::path& executablePath, const std::filesystem::path& workingDirectory,
        const std::vector<NativeString>& arguments)
    {
        ScopedCurrentPath currentPath(workingDirectory);
        Utf8Argv argv(executablePath, arguments);
        return runApplication(argv.argc(), argv.argv());
    }

#ifdef _WIN32
    NativeString quoteWindowsArgument(const NativeString& argument)
    {
        if (argument.empty())
            return L"\"\"";

        if (argument.find_first_of(L" \t\n\v\"") == NativeString::npos)
            return argument;

        NativeString result;
        result.push_back(L'"');

        std::size_t backslashes = 0;
        for (const wchar_t c : argument)
        {
            if (c == L'\\')
            {
                ++backslashes;
                continue;
            }

            if (c == L'"')
            {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(c);
                backslashes = 0;
                continue;
            }

            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(c);
        }

        result.append(backslashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }

    int runChild(const std::filesystem::path& childPath, const std::filesystem::path& workingDirectory,
        const std::vector<NativeString>& arguments)
    {
        NativeString commandLine = quoteWindowsArgument(pathToNativeString(childPath));
        for (const NativeString& argument : arguments)
        {
            commandLine.push_back(L' ');
            commandLine += quoteWindowsArgument(argument);
        }

        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};

        const std::wstring application = childPath.wstring();
        const std::wstring workingDir = workingDirectory.wstring();
        if (!CreateProcessW(application.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                workingDir.c_str(), &startupInfo, &processInfo))
        {
            std::wstringstream stream;
            stream << L"failed to launch " << application << L" (Win32 error " << GetLastError() << L")";
            printError(stream.str());
            return 1;
        }

        CloseHandle(processInfo.hThread);
        WaitForSingleObject(processInfo.hProcess, INFINITE);

        DWORD exitCode = 1;
        if (!GetExitCodeProcess(processInfo.hProcess, &exitCode))
            exitCode = 1;
        CloseHandle(processInfo.hProcess);

        return exitCode > static_cast<DWORD>(std::numeric_limits<int>::max()) ? 1 : static_cast<int>(exitCode);
    }
#else
    int runChild(const std::filesystem::path& childPath, const std::filesystem::path& workingDirectory,
        const std::vector<NativeString>& arguments)
    {
        std::vector<std::string> childArguments;
        childArguments.push_back(childPath.filename().string());
        childArguments.insert(childArguments.end(), arguments.begin(), arguments.end());

        std::vector<char*> rawArguments;
        rawArguments.reserve(childArguments.size() + 1);
        for (std::string& argument : childArguments)
            rawArguments.push_back(argument.data());
        rawArguments.push_back(nullptr);

        const pid_t pid = fork();
        if (pid == -1)
        {
            printError(std::string("failed to fork child process: ") + std::strerror(errno));
            return 1;
        }

        if (pid == 0)
        {
            chdir(workingDirectory.c_str());
            execv(childPath.c_str(), rawArguments.data());
            std::cerr << "failed to launch " << childPath << ": " << std::strerror(errno) << '\n';
            _exit(127);
        }

        int status = 0;
        while (waitpid(pid, &status, 0) == -1)
        {
            if (errno != EINTR)
            {
                printError(std::string("failed waiting for child process: ") + std::strerror(errno));
                return 1;
            }
        }

        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return 1;
    }
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try
    {
        const std::filesystem::path executablePath = getExecutablePath(argc, argv);
        const std::filesystem::path executableDirectory = canonicalDirectory(executablePath.parent_path());
        const ParsedArguments arguments = parseArguments(argc, argv);

        if (arguments.mHelp)
        {
            printUsage(executablePath);
            return 0;
        }

        const std::filesystem::path targetPath = getTargetPath(executableDirectory, arguments.mMode);
        if (arguments.mPrintTarget)
        {
            printLine(pathToNativeString(targetPath));
            return 0;
        }

        const bool runExternalChild = (arguments.mMode == Mode::Client && arguments.mExternalClient)
            || (arguments.mMode == Mode::Server && arguments.mExternalServer);

        if (runExternalChild && !std::filesystem::exists(targetPath))
        {
            printError(COMMUNITYMP_TEXT("CommunityMP could not find the selected mode executable next to ")
                + pathToNativeString(executablePath.filename()) + COMMUNITYMP_TEXT(": ")
                + pathToNativeString(targetPath.filename()));
            return 1;
        }

        ModeLock lock;
        if (!lock.acquire(executableDirectory, arguments.mMode))
        {
            printError(COMMUNITYMP_TEXT("CommunityMP ") + getModeName(arguments.mMode)
                + COMMUNITYMP_TEXT(" is already running from this install."));
            printError(
                COMMUNITYMP_TEXT("Run the other mode if you want one client and one dedicated server side by side."));
            return 2;
        }

        if (runExternalChild)
            return runChild(targetPath, executableDirectory, arguments.mForwarded);

        if (arguments.mMode == Mode::Server)
            return runServerInProcess(executablePath, executableDirectory, arguments.mForwarded);

        return runClientInProcess(executablePath, executableDirectory, arguments.mForwarded);
    }
    catch (const std::exception& e)
    {
        std::cerr << "communitymp: " << e.what() << '\n';
        return 1;
    }
}
