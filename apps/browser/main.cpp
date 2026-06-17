#include <QApplication>
#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/ClientSettings.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/settings/settings.hpp>
#include <apps/browser/netutils/QueryClient.hpp>
#include <apps/browser/netutils/Utils.hpp>
#include "MainWindow.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view queryMasterOnceArgument = "--query-master-once";
    constexpr std::string_view queryServerOnceArgument = "--query-server-once";
    constexpr std::string_view pingServerOnceArgument = "--ping-server-once";
    constexpr const char* queryMasterOnceEnv = "COMMUNITYMP_HUB_QUERY_ONCE";
    constexpr const char* legacyQueryMasterOnceEnv = "TES3MP_BROWSER_QUERY_ONCE";

    bool hasArgument(int argc, char* argv[], std::string_view argument)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == argument)
                return true;
        }

        return false;
    }

    bool shouldQueryMasterOnce(int argc, char* argv[])
    {
        return hasArgument(argc, argv, queryMasterOnceArgument) || std::getenv(queryMasterOnceEnv) != nullptr
            || std::getenv(legacyQueryMasterOnceEnv) != nullptr;
    }

    bool shouldQueryServerOnce(int argc, char* argv[])
    {
        return hasArgument(argc, argv, queryServerOnceArgument);
    }

    bool shouldPingServerOnce(int argc, char* argv[])
    {
        return hasArgument(argc, argv, pingServerOnceArgument);
    }

    std::string getArgumentValue(int argc, char* argv[], std::string_view prefix, const std::string& fallback)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument(argv[i]);
            if (argument.starts_with(prefix))
                return std::string(argument.substr(prefix.size()));
        }

        return fallback;
    }

    int getArgumentValue(int argc, char* argv[], std::string_view prefix, int fallback)
    {
        const std::string value = getArgumentValue(argc, argv, prefix, "");
        if (value.empty())
            return fallback;

        return std::stoi(value);
    }

    int queryMasterOnce()
    {
        std::cout << "Running master query" << std::endl;
        const auto servers = QueryClient::Get().Query();
        if (QueryClient::Get().Status() != ID_MASTER_QUERY)
        {
            std::cerr << "Master query failed" << std::endl;
            return 2;
        }

        std::cout << "Master query returned " << servers.size() << " server(s)" << std::endl;
        for (const auto& [address, data] : servers)
            std::cout << mwmp::packetAddressToString(address, true, ':') << " " << data.GetName() << std::endl;

        return 0;
    }

    bool getServerTarget(int argc, char* argv[], std::string& serverAddr, int& serverPort)
    {
        serverAddr = getArgumentValue(argc, argv, "--server-address=", "");
        serverPort = getArgumentValue(argc, argv, "--server-port=", 0);
        if (!serverAddr.empty() && serverPort > 0)
            return true;

        std::cerr << "Missing --server-address or --server-port" << std::endl;
        return false;
    }

    int queryServerOnce(int argc, char* argv[])
    {
        std::string serverAddr;
        int serverPort = 0;
        if (!getServerTarget(argc, argv, serverAddr, serverPort))
            return 2;

        std::cout << "Running master update for " << serverAddr << ":" << serverPort << std::endl;
        auto server = QueryClient::Get().Update(mwmp::makePacketAddress(serverAddr.c_str(), serverPort));
        if (QueryClient::Get().Status() != ID_MASTER_UPDATE || !mwmp::isPacketAddressAssigned(server.first))
        {
            std::cerr << "Master update failed" << std::endl;
            return 2;
        }

        std::cout << "Master update returned " << mwmp::packetAddressToString(server.first, true, ':') << " "
                  << server.second.GetName() << std::endl;
        std::cout << "Master update details: listedPlayers=" << server.second.players.size()
                  << " reportedPlayers=" << server.second.GetPlayers()
                  << " maxPlayers=" << server.second.GetMaxPlayers()
                  << " plugins=" << server.second.plugins.size()
                  << " rules=" << server.second.rules.size() << std::endl;
        return 0;
    }

    int pingServerOnce(int argc, char* argv[])
    {
        std::string serverAddr;
        int serverPort = 0;
        if (!getServerTarget(argc, argv, serverAddr, serverPort))
            return 2;

        const unsigned int ping = PingServer(serverAddr.c_str(), static_cast<unsigned short>(serverPort));
        if (ping == PING_UNREACHABLE)
        {
            std::cerr << "Server ping failed" << std::endl;
            return 3;
        }

        std::cout << "Server ping returned " << ping << " ms" << std::endl;
        return 0;
    }
}

std::string loadSettings()
{
    return mwmp::ClientSettings::load().string();
}

int main(int argc, char *argv[])
{
    loadSettings();

    std::string addr = Settings::Manager::getString("address", "Master");
    int port = Settings::Manager::getInt("port", "Master");

    // initialize resources, if needed
    // Q_INIT_RESOURCE(resfile);

    QueryClient::Get().SetServer(addr, port);
    if (shouldQueryMasterOnce(argc, argv))
    {
        addr = getArgumentValue(argc, argv, "--master-address=", addr);
        port = getArgumentValue(argc, argv, "--master-port=", port);
        QueryClient::Get().SetServer(addr, port);
        std::cout << "Querying master " << addr << ":" << port << std::endl;
        return queryMasterOnce();
    }
    if (shouldQueryServerOnce(argc, argv))
    {
        addr = getArgumentValue(argc, argv, "--master-address=", addr);
        port = getArgumentValue(argc, argv, "--master-port=", port);
        QueryClient::Get().SetServer(addr, port);
        std::cout << "Querying server details through master " << addr << ":" << port << std::endl;
        return queryServerOnce(argc, argv);
    }
    if (shouldPingServerOnce(argc, argv))
        return pingServerOnce(argc, argv);

    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8(mwmp::Branding::productName));
    app.setApplicationVersion(QString::fromUtf8(mwmp::Branding::productVersion));
    MainWindow d;

    d.show();
    return app.exec();
}
