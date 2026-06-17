#include "Cells.hpp"

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Cell.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/ServerSimulation.hpp>
#include <apps/openmw-mp/Utils.hpp>

#include <iostream>

static std::string tempCellDescription;
static std::string tempRegionId;

unsigned int CellFunctions::GetCellStateChangesSize(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return static_cast<unsigned int>(player->cellStateChanges.size());
}

unsigned int CellFunctions::GetCellStateType(unsigned short pid, unsigned int index) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cellStateChanges.at(index).type;
}

const char *CellFunctions::GetCellStateDescription(unsigned short pid, unsigned int index) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->cellStateChanges.size())
        return "invalid";

    tempCellDescription = player->cellStateChanges.at(index).cell.getDescription();
    return tempCellDescription.c_str();
}

const char *CellFunctions::GetCell(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    tempCellDescription = player->cell.getDescription();
    return tempCellDescription.c_str();
}

int CellFunctions::GetExteriorX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mX;
}

int CellFunctions::GetExteriorY(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mY;
}

bool CellFunctions::IsInExterior(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->cell.isExterior();
}

const char *CellFunctions::GetRegion(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    tempRegionId = player->cell.mRegion.empty() ? "" : player->cell.mRegion.getRefIdString();
    return tempRegionId.c_str();
}

bool CellFunctions::IsChangingRegion(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->isChangingRegion;
}

unsigned int CellFunctions::GetCellChangeReason(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, mwmp::CELL_CHANGE_REASON_NORMAL);

    return player->cellChangeReason;
}

bool CellFunctions::GetCellSimulationInterest(const char *cellDescription) noexcept
{
    ESM::Cell esmCell = Utils::getCellFromDescription(cellDescription);
    Cell *cell = CellController::get()->getCell(&esmCell);

    return cell != nullptr && cell->hasSimulationInterest();
}

void CellFunctions::SetCell(unsigned short pid, const char *cellDescription) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %s", player->npc.mName.c_str(),
                       player->cell.getDescription().c_str(), cellDescription);

    player->cell = Utils::getCellFromDescription(cellDescription);
    player->cellChangeReason = mwmp::CELL_CHANGE_REASON_SERVER;
}

void CellFunctions::SetCellChangeReason(unsigned short pid, unsigned int reason) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (!mwmp::isValidCellChangeReason(reason))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Script attempted to set invalid cell change reason %u for %s",
                           reason, player->npc.mName.c_str());
        return;
    }

    player->cellChangeReason = reason;
}

void CellFunctions::SetExteriorCell(unsigned short pid, int x, int y) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %i,%i", player->npc.mName.c_str(),
                       player->cell.getDescription().c_str(), x, y);

    // If the player is currently in an interior, turn off the interior flag
    // from the cell
    if (!player->cell.isExterior())
        player->cell.mData.mFlags &= ~ESM::Cell::Interior;

    player->cell.mData.mX = x;
    player->cell.mData.mY = y;
    player->cellChangeReason = mwmp::CELL_CHANGE_REASON_SERVER;
}

void CellFunctions::SetCellSimulationInterest(const char *cellDescription, bool enabled) noexcept
{
    ESM::Cell esmCell = Utils::getCellFromDescription(cellDescription);
    Cell *cell = enabled ? CellController::get()->addCell(esmCell) : CellController::get()->getCell(&esmCell);
    if (cell == nullptr)
        return;

    cell->setSimulationInterest(enabled);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script %s server simulation interest for %s",
                       enabled ? "enabled" : "disabled", cell->getShortDescription().c_str());
}

void CellFunctions::SendCell(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    ++player->positionSequence;
    mwmp::Networking::getPtr()->getServerSimulation().acceptServerAuthoredPlayerState(*player, true);

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_CELL_CHANGE);
    packet->setPlayer(player);

    packet->Send(false);
}
