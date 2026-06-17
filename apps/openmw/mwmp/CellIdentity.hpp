#ifndef OPENMW_MWMP_CELLIDENTITY_HPP
#define OPENMW_MWMP_CELLIDENTITY_HPP

#include <string>

#include <components/esm3/loadcell.hpp>

#include "../mwworld/cell.hpp"

namespace mwmp
{
    inline std::string getCanonicalCellDescription(const ESM::Cell& cell)
    {
        if (cell.isExterior())
            return std::to_string(cell.mData.mX) + ", " + std::to_string(cell.mData.mY);
        if (!cell.mName.empty())
            return cell.mName;
        if (!cell.mId.empty())
            return cell.mId.serializeText();
        return std::to_string(cell.mData.mX) + ", " + std::to_string(cell.mData.mY);
    }

    inline ESM::Cell makeActorPacketCell(const MWWorld::Cell& cell)
    {
        ESM::Cell packetCell;

        if (cell.isExterior())
        {
            packetCell.mData.mX = cell.getGridX();
            packetCell.mData.mY = cell.getGridY();
        }
        else
        {
            packetCell.mData.mFlags = ESM::Cell::Interior;
            packetCell.mName = std::string(cell.getNameId());
        }

        packetCell.mRegion = cell.getRegion();
        packetCell.updateId();
        return packetCell;
    }
}

#endif
