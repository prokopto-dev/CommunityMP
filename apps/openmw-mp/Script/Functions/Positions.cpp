#include "Positions.hpp"
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/ServerNetworking.hpp>
#include <apps/openmw-mp/ServerSimulation.hpp>

#include <iostream>

double PositionFunctions::GetPosX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->position.pos[0];
}

double PositionFunctions::GetPosY(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->position.pos[1];
}

double PositionFunctions::GetPosZ(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->position.pos[2];
}

double PositionFunctions::GetPreviousCellPosX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->previousCellPosition.pos[0];
}

double PositionFunctions::GetPreviousCellPosY(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->previousCellPosition.pos[1];
}

double PositionFunctions::GetPreviousCellPosZ(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->previousCellPosition.pos[2];
}

double PositionFunctions::GetRotX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->position.rot[0];
}

double PositionFunctions::GetRotZ(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->position.rot[2];
}

void PositionFunctions::SetPos(unsigned short pid, double x, double y, double z) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->position.pos[0] = static_cast<float>(x);
    player->position.pos[1] = static_cast<float>(y);
    player->position.pos[2] = static_cast<float>(z);
}

void PositionFunctions::SetRot(unsigned short pid, double x, double z) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->position.rot[0] = static_cast<float>(x);
    player->position.rot[2] = static_cast<float>(z);
}

void PositionFunctions::SetMomentum(unsigned short pid, double x, double y, double z) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->momentum.pos[0] = static_cast<float>(x);
    player->momentum.pos[1] = static_cast<float>(y);
    player->momentum.pos[2] = static_cast<float>(z);
}

void PositionFunctions::SendPos(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (mwmp::ServerNetworking::getPtr()->getServerSimulation().isRedundantServerAuthoredPosition(*player))
        return;

    ++player->positionSequence;
    if (!mwmp::ServerNetworking::getPtr()->getServerSimulation().acceptServerAuthoredPlayerState(*player))
        return;

    mwmp::PlayerPacket *packet = mwmp::ServerNetworking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_POSITION);
    packet->setPlayer(player);

    packet->SendWithReliability(false, mwmp::PacketReliability::ReliableOrdered);
}

void PositionFunctions::SendMomentum(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    mwmp::PlayerPacket *packet = mwmp::ServerNetworking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_MOMENTUM);
    packet->setPlayer(player);

    packet->Send(false);
}
