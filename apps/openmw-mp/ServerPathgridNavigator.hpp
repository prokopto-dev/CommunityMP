#ifndef OPENMW_MP_SERVERPATHGRIDNAVIGATOR_HPP
#define OPENMW_MP_SERVERPATHGRIDNAVIGATOR_HPP

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <components/esm/position.hpp>

#include "WorldDatabaseStore.hpp"

namespace mwmp
{
    struct ServerPathgridNavigationStats
    {
        bool available = false;
        std::string cellKey;
        std::size_t pointCount = 0;
        std::size_t rawEdgeCount = 0;
        std::size_t usableDirectedEdgeCount = 0;
        std::size_t invalidEdgeCount = 0;
        std::size_t connectedComponentCount = 0;
        std::size_t largestConnectedComponentSize = 0;
    };

    struct ServerPathgridWaypoint
    {
        std::size_t pointIndex = std::numeric_limits<std::size_t>::max();
        ESM::Position position;
    };

    struct ServerPathgridRoute
    {
        bool attempted = false;
        bool pathgridAvailable = false;
        bool reachable = false;
        bool direct = false;
        std::size_t startPoint = std::numeric_limits<std::size_t>::max();
        std::size_t endPoint = std::numeric_limits<std::size_t>::max();
        float pathCost = 0.f;
        std::size_t expandedNodeCount = 0;
        std::string failureReason;
        std::vector<ServerPathgridWaypoint> waypoints;
    };

    class ServerPathgridNavigator
    {
    public:
        static constexpr std::size_t invalidPoint = std::numeric_limits<std::size_t>::max();

        void rebuild(std::string cellKey, const std::vector<WorldPathgridPointRecord>& points,
            const std::vector<WorldPathgridEdgeRecord>& edges);
        void clear();

        const ServerPathgridNavigationStats& statistics() const;
        bool hasPathgrid() const;
        bool arePointsConnected(std::size_t startPoint, std::size_t endPoint) const;
        std::size_t closestPointIndex(const ESM::Position& position) const;
        ServerPathgridRoute buildRoute(const ESM::Position& start, const ESM::Position& end,
            std::size_t maxExpandedNodes = 4096) const;

    private:
        struct Neighbor
        {
            std::size_t offset = invalidPoint;
            float cost = 0.f;
        };

        std::size_t offsetForPointIndex(std::size_t pointIndex) const;
        ESM::Position positionForOffset(std::size_t offset) const;
        float distanceBetweenOffsets(std::size_t left, std::size_t right) const;
        void rebuildConnectedComponents();

        ServerPathgridNavigationStats mStats;
        std::vector<WorldPathgridPointRecord> mPoints;
        std::vector<std::vector<Neighbor>> mAdjacency;
        std::vector<std::size_t> mComponentByOffset;
    };
}

#endif // OPENMW_MP_SERVERPATHGRIDNAVIGATOR_HPP
