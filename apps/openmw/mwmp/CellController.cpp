#include <components/detournavigator/navigator.hpp>
#include <components/esm3/cellid.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/containerstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldimp.hpp"
#include "../mwworld/worldmodel.hpp"

#include <optional>

#include "CellController.hpp"
#include "CellIdentity.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ObjectList.hpp"
#include "LocalActor.hpp"
#include "LocalPlayer.hpp"
using namespace mwmp;

namespace
{
    std::string cellDescription(const ESM::Cell& cell)
    {
        return getCanonicalCellDescription(cell);
    }

    bool isCellStoreActive(const MWWorld::CellStore* cellStore)
    {
        return cellStore != nullptr && cellStore->getCell() != nullptr
            && MWBase::Environment::get().getWorld()->isCellActive(*cellStore);
    }

    std::string makeActorIndex(unsigned int refNum, unsigned int mpNum)
    {
        return Utils::toString(refNum) + "-" + Utils::toString(mpNum);
    }

    std::string getLegacyActorIndexForServerMpNum(unsigned int mpNum)
    {
        if (mpNum == 0 || !mwmp::Main::isInitialized())
            return "";

        const std::optional<unsigned int> localRefNum
            = mwmp::Main::get().getNetworking()->getObjectList()->getLocalRefNumForServerMpNum(mpNum);
        if (!localRefNum.has_value())
            return "";

        return makeActorIndex(*localRefNum, 0);
    }

    std::string resolveActorRecordIndex(
        const std::map<std::string, std::string>& actorRecords, unsigned int refNum, unsigned int mpNum)
    {
        const std::string preferredIndex = makeActorIndex(refNum, mpNum);
        if (actorRecords.count(preferredIndex) > 0)
            return preferredIndex;

        const std::string legacyIndex = getLegacyActorIndexForServerMpNum(mpNum);
        if (!legacyIndex.empty() && actorRecords.count(legacyIndex) > 0)
            return legacyIndex;

        return preferredIndex;
    }
}

std::map<std::string, mwmp::Cell *> CellController::cellsInitialized;
std::map<std::string, std::string> CellController::localActorsToCells;
std::map<std::string, std::string> CellController::dedicatedActorsToCells;
std::map<std::string, unsigned int> CellController::queuedDeathStates;
std::map<std::string, PacketGuid> CellController::queuedAuthorityGuids;

mwmp::CellController::CellController()
{

}

CellController::~CellController()
{

}

void CellController::updateLocal(bool forceUpdate)
{
    // Loop through Cells, deleting inactive ones and updating LocalActors in active ones
    for (auto it = cellsInitialized.begin(); it != cellsInitialized.end();)
    {
        mwmp::Cell *mpCell = it->second;

        if (!isCellStoreActive(mpCell->getCellStore()))
        {
            mpCell->uninitializeLocalActors();
            mpCell->uninitializeDedicatedActors();
            delete it->second;
            cellsInitialized.erase(it++);
        }
        else
        {
            applyQueuedActorAuthority(mpCell->getCellStore()->getCell()->getEsm3());
            mpCell->updateLocal(forceUpdate);
            ++it;
        }
    }

    // If there are cellsInitialized remaining, loop through them and initialize new LocalActors for eligible ones
    // 
    //
    // Note: This cannot be combined with the above loop because initializing LocalActors in a Cell before they are
    //       deleted from their previous one can make their records stay deleted
    if (cellsInitialized.size() > 0)
    {
        for (auto& cell : cellsInitialized)
        {
            mwmp::Cell* mpCell = cell.second;
            if (mpCell->shouldInitializeActors == true)
            {
                mpCell->shouldInitializeActors = false;
                mpCell->initializeLocalActors();
            }
        }
    }
}

void CellController::updateDedicated(float dt)
{
    for (const auto &cell : cellsInitialized)
        cell.second->updateDedicated(dt);
}

void CellController::initializeCell(const ESM::Cell& cell)
{
    std::string mapIndex = cellDescription(cell);

    // If this key doesn't exist, create it
    if (cellsInitialized.count(mapIndex) == 0)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Initializing mwmp::Cell %s", cellDescription(cell).c_str());

        MWWorld::CellStore *cellStore = getCellStore(cell);

        if (!cellStore) return;

        mwmp::Cell *mpCell = new mwmp::Cell(cellStore);
        cellsInitialized[mapIndex] = mpCell;

        LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized mwmp::Cell %s", cellDescription(cell).c_str());
    }

    if (isActiveWorldCell(cell))
        applyQueuedActorAuthority(cell);
}

void CellController::uninitializeCell(const ESM::Cell& cell)
{
    std::string mapIndex = cellDescription(cell);

    // If this key exists, erase the key-value pair from the map
    if (cellsInitialized.count(mapIndex) > 0)
    {
        mwmp::Cell* mpCell = cellsInitialized.at(mapIndex);
        mpCell->uninitializeLocalActors();
        mpCell->uninitializeDedicatedActors();
        delete cellsInitialized.at(mapIndex);
        cellsInitialized.erase(mapIndex);
    }
}

void CellController::uninitializeCells()
{
    if (cellsInitialized.size() > 0)
    {
        for (auto it = cellsInitialized.cbegin(); it != cellsInitialized.cend(); it++)
        {
            mwmp::Cell* mpCell = it->second;
            mpCell->uninitializeLocalActors();
            mpCell->uninitializeDedicatedActors();
            delete it->second;
        }

        cellsInitialized.clear();
    }
}

void CellController::readPositions(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readPositions(actorList);
}

void CellController::readAnimFlags(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readAnimFlags(actorList);
}

void CellController::readAnimPlay(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readAnimPlay(actorList);
}

void CellController::readStatsDynamic(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readStatsDynamic(actorList);
}

void CellController::readDeath(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readDeath(actorList);
}

void CellController::readEquipment(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readEquipment(actorList);
}

void CellController::readSpeech(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readSpeech(actorList);
}

void CellController::readSpellsActive(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readSpellsActive(actorList);
}

void CellController::readAi(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readAi(actorList);
}

void CellController::readAttack(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readAttack(actorList);
}

void CellController::readCast(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readCast(actorList);
}

void CellController::readCellChange(ActorList& actorList)
{
    std::string mapIndex = cellDescription(actorList.cell);

    initializeCell(actorList.cell);

    // If this now exists, send it the data
    if (cellsInitialized.count(mapIndex) > 0)
        cellsInitialized[mapIndex]->readCellChange(actorList);
}

bool CellController::hasQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    return queuedDeathStates.count(actorIndex) > 0;
}

unsigned int CellController::getQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    return queuedDeathStates[actorIndex];
}

void CellController::clearQueuedDeathState(MWWorld::Ptr ptr)
{
    std::string actorIndex = generateMapIndex(ptr);

    queuedDeathStates.erase(actorIndex);
}

void CellController::setQueuedDeathState(MWWorld::Ptr ptr, unsigned int deathState)
{
    std::string actorIndex = generateMapIndex(ptr);

    queuedDeathStates[actorIndex] = deathState;
}

void CellController::setLocalActorRecord(std::string actorIndex, std::string cellIndex)
{
    localActorsToCells[actorIndex] = cellIndex;
}

void CellController::removeLocalActorRecord(std::string actorIndex)
{
    localActorsToCells.erase(actorIndex);
}

std::pair<unsigned int, unsigned int> CellController::getActorNetworkId(const MWWorld::Ptr& ptr) const
{
    if (ptr.mRef == nullptr)
        return { 0, 0 };

    const unsigned int localRefNum = ptr.getCellRef().getRefNum().mIndex;
    unsigned int serverMpNum = 0;

    if (Main::isInitialized())
        serverMpNum = Main::get().getNetworking()->getObjectList()->getServerMpNum(ptr);

    if (serverMpNum != 0)
        return { 0, serverMpNum };

    return { localRefNum, 0 };
}

bool CellController::isLocalActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    const auto [refNum, mpNum] = getActorNetworkId(ptr);
    std::string actorIndex = resolveActorRecordIndex(localActorsToCells, refNum, mpNum);

    return localActorsToCells.count(actorIndex) > 0;
}

bool CellController::isLocalActor(int refNum, int mpNum)
{
    std::string actorIndex = resolveActorRecordIndex(localActorsToCells, refNum, mpNum);

    return localActorsToCells.count(actorIndex) > 0;
}

LocalActor *CellController::getLocalActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return nullptr;

    const auto [refNum, mpNum] = getActorNetworkId(ptr);
    std::string actorIndex = resolveActorRecordIndex(localActorsToCells, refNum, mpNum);
    auto actorRecord = localActorsToCells.find(actorIndex);
    if (actorRecord == localActorsToCells.end())
        return nullptr;

    auto cell = cellsInitialized.find(actorRecord->second);
    if (cell == cellsInitialized.end() || cell->second == nullptr)
        return nullptr;

    return cell->second->getLocalActor(actorIndex);
}

LocalActor *CellController::getLocalActor(int refNum, int mpNum)
{
    std::string actorIndex = resolveActorRecordIndex(localActorsToCells, refNum, mpNum);
    auto actorRecord = localActorsToCells.find(actorIndex);
    if (actorRecord == localActorsToCells.end())
        return nullptr;

    auto cell = cellsInitialized.find(actorRecord->second);
    if (cell == cellsInitialized.end() || cell->second == nullptr)
        return nullptr;

    return cell->second->getLocalActor(actorIndex);
}

void CellController::setDedicatedActorRecord(std::string actorIndex, std::string cellIndex)
{
    dedicatedActorsToCells[actorIndex] = cellIndex;
}

void CellController::removeDedicatedActorRecord(std::string actorIndex)
{
    dedicatedActorsToCells.erase(actorIndex);
}

bool CellController::isDedicatedActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    const auto [refNum, mpNum] = getActorNetworkId(ptr);
    std::string actorIndex = resolveActorRecordIndex(dedicatedActorsToCells, refNum, mpNum);

    return dedicatedActorsToCells.count(actorIndex) > 0;
}

bool CellController::isDedicatedActor(int refNum, int mpNum)
{
    std::string actorIndex = resolveActorRecordIndex(dedicatedActorsToCells, refNum, mpNum);

    return dedicatedActorsToCells.count(actorIndex) > 0;
}

DedicatedActor *CellController::getDedicatedActor(MWWorld::Ptr ptr)
{
    if (ptr.mRef == nullptr)
        return nullptr;

    const auto [refNum, mpNum] = getActorNetworkId(ptr);
    std::string actorIndex = resolveActorRecordIndex(dedicatedActorsToCells, refNum, mpNum);
    auto actorRecord = dedicatedActorsToCells.find(actorIndex);
    if (actorRecord == dedicatedActorsToCells.end())
        return nullptr;

    auto cell = cellsInitialized.find(actorRecord->second);
    if (cell == cellsInitialized.end() || cell->second == nullptr)
        return nullptr;

    return cell->second->getDedicatedActor(actorIndex);
}

DedicatedActor *CellController::getDedicatedActor(int refNum, int mpNum)
{
    std::string actorIndex = resolveActorRecordIndex(dedicatedActorsToCells, refNum, mpNum);
    auto actorRecord = dedicatedActorsToCells.find(actorIndex);
    if (actorRecord == dedicatedActorsToCells.end())
        return nullptr;

    auto cell = cellsInitialized.find(actorRecord->second);
    if (cell == cellsInitialized.end() || cell->second == nullptr)
        return nullptr;

    return cell->second->getDedicatedActor(actorIndex);
}

std::string CellController::generateMapIndex(int refNum, int mpNum)
{
    std::string mapIndex = "";
    mapIndex = Utils::toString(refNum) + "-" + Utils::toString(mpNum);
    return mapIndex;
}

std::string CellController::generateMapIndex(MWWorld::Ptr ptr)
{
    const auto [refNum, mpNum] = getActorNetworkId(ptr);
    return generateMapIndex(refNum, mpNum);
}

std::string CellController::generateMapIndex(BaseActor baseActor)
{
    return generateMapIndex(baseActor.refNum, baseActor.mpNum);
}

bool CellController::hasLocalAuthority(const ESM::Cell& cell)
{
    if (isInitializedCell(cell) && isActiveWorldCell(cell))
    {
        Cell* mpCell = getCell(cell);
        return mpCell != nullptr && mpCell->hasLocalAuthority();
    }

    return false;
}

bool CellController::isInitializedCell(const std::string& cellDescription)
{
    return (cellsInitialized.count(cellDescription) > 0);
}

bool CellController::isInitializedCell(const ESM::Cell& cell)
{
    return isInitializedCell(cellDescription(cell));
}

bool CellController::isActiveWorldCell(const ESM::Cell& cell)
{
    return isCellStoreActive(getCellStore(cell));
}

void CellController::applyActorAuthority(const ESM::Cell& cell, const PacketGuid& guid)
{
    const std::string mapIndex = cellDescription(cell);
    if (cellsInitialized.count(mapIndex) == 0)
        return;

    queuedAuthorityGuids.erase(mapIndex);

    mwmp::Cell* mpCell = cellsInitialized.at(mapIndex);

    if (!mwmp::isPacketGuidAssigned(guid))
    {
        mpCell->setServerActorAuthority(true);
        return;
    }

    mpCell->setAuthority(guid);

    if (guid == Main::get().getLocalPlayer()->guid)
    {
        mpCell->uninitializeDedicatedActors();
        mpCell->initializeLocalActors();
        mpCell->updateLocal(true);

        MWBase::Environment::get().getWorld()->refreshNavigator();
    }
    else
    {
        mpCell->uninitializeLocalActors();
    }
}

void CellController::queueActorAuthority(const ESM::Cell& cell, const PacketGuid& guid)
{
    queuedAuthorityGuids[cellDescription(cell)] = guid;
}

bool CellController::applyQueuedActorAuthority(const ESM::Cell& cell)
{
    const std::string mapIndex = cellDescription(cell);
    auto authority = queuedAuthorityGuids.find(mapIndex);
    if (authority == queuedAuthorityGuids.end() || cellsInitialized.count(mapIndex) == 0)
        return false;

    const PacketGuid guid = authority->second;
    queuedAuthorityGuids.erase(authority);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Applying queued ID_ACTOR_AUTHORITY about %s", mapIndex.c_str());
    applyActorAuthority(cell, guid);
    return true;
}

Cell *CellController::getCell(const ESM::Cell& cell)
{
    const auto found = cellsInitialized.find(cellDescription(cell));
    if (found == cellsInitialized.end())
        return nullptr;

    return found->second;
}

MWWorld::CellStore *CellController::getCellStore(const ESM::Cell& cell)
{
    MWWorld::CellStore* cellStore = nullptr;
    MWWorld::WorldModel* worldModel = MWBase::Environment::get().getWorldModel();

    if (cell.isExterior())
    {
        cellStore = &worldModel->getExterior(
            ESM::ExteriorCellLocation(cell.mData.mX, cell.mData.mY, ESM::Cell::sDefaultWorldspaceId));
    }
    else
    {
        try
        {
            const std::string cellName = cell.mName.empty() ? cell.mId.serializeText() : cell.mName;
            cellStore = &worldModel->getInterior(cellName);
        }
        catch (std::exception&)
        {
            cellStore = nullptr;
        }
    }

    return cellStore;
}

bool CellController::isSameCell(const ESM::Cell& cell, const ESM::Cell& otherCell)
{
    if (&cell == nullptr || &otherCell == nullptr) return false;

    bool isCellExterior = false;
    bool isOtherCellExterior = false;

    try
    {
        isCellExterior = cell.isExterior();
        isOtherCellExterior = otherCell.isExterior();
    }
    catch (std::exception&)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed cell comparison");
        return false;
    }

    if (isCellExterior && isOtherCellExterior)
    {
        if (cell.mData.mX == otherCell.mData.mX && cell.mData.mY == otherCell.mData.mY)
            return true;
    }
    else if (Misc::StringUtils::ciEqual(cell.mName, otherCell.mName))
        return true;

    return false;
}

int CellController::getCellSize() const
{
    return 8192;
}

