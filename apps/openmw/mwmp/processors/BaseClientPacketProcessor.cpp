#include "BaseClientPacketProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

PacketGuid BaseClientPacketProcessor::guid;
PacketGuid BaseClientPacketProcessor::myGuid;
PacketAddress BaseClientPacketProcessor::serverAddr;
bool BaseClientPacketProcessor::request;

LocalPlayer *BaseClientPacketProcessor::getLocalPlayer()
{
    return Main::get().getLocalPlayer();
}

