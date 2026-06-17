#include "PacketGameSettings.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxGameSettings = 3000;
    constexpr uint32_t maxVrSettings = 3000;
}

PacketGameSettings::PacketGameSettings() : PlayerPacket()
{
    packetID = ID_GAME_SETTINGS;
    orderChannel = CHANNEL_SYSTEM;
}

void PacketGameSettings::Packet(PacketStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->difficulty, send);
    RW(player->consoleAllowed, send);
    RW(player->bedRestAllowed, send);
    RW(player->wildernessRestAllowed, send);
    RW(player->waitAllowed, send);
    RW(player->enforcedLogLevel, send);
    RW(player->physicsFramerate, send);

    std::string mapIndex;
    std::string mapValue;

    uint32_t gameSettingCount = 0;
    if (send)
        gameSettingCount = static_cast<uint32_t>(player->gameSettings.size());

    if (!RW(gameSettingCount, send))
        return;

    if (send)
    {
        for (auto&& gameSetting : player->gameSettings)
        {
            mapIndex = gameSetting.first;
            mapValue = gameSetting.second;
            RW(mapIndex, send, false);
            RW(mapValue, send, false);
        }
    }
    else
    {
        if (gameSettingCount > maxGameSettings)
        {
            packetValid = false;
            player->gameSettings.clear();
            return;
        }

        player->gameSettings.clear();
        for (unsigned int n = 0; n < gameSettingCount; n++)
        {
            mapIndex.clear();
            mapValue.clear();

            if (!RW(mapIndex, send, false) || !RW(mapValue, send, false))
                return;

            player->gameSettings[mapIndex] = mapValue;
        }
    }

    uint32_t vrSettingCount = 0;
    if (send)
        vrSettingCount = static_cast<uint32_t>(player->vrSettings.size());

    if (!RW(vrSettingCount, send))
        return;

    if (send)
    {
        for (auto&& vrSetting : player->vrSettings)
        {
            mapIndex = vrSetting.first;
            mapValue = vrSetting.second;
            RW(mapIndex, send, false);
            RW(mapValue, send, false);
        }
    }
    else
    {
        if (vrSettingCount > maxVrSettings)
        {
            packetValid = false;
            player->vrSettings.clear();
            return;
        }

        player->vrSettings.clear();
        for (unsigned int n = 0; n < vrSettingCount; n++)
        {
            mapIndex.clear();
            mapValue.clear();

            if (!RW(mapIndex, send, false) || !RW(mapValue, send, false))
                return;

            player->vrSettings[mapIndex] = mapValue;
        }
    }
}
