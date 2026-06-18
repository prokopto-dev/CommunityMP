#include "ServerPathgridNavigator.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <utility>

namespace
{
    float squaredDistance(const mwmp::WorldPathgridPointRecord& point, const ESM::Position& position)
    {
        const double dx = static_cast<double>(point.x) - position.pos[0];
        const double dy = static_cast<double>(point.y) - position.pos[1];
        const double dz = static_cast<double>(point.z) - position.pos[2];
        return static_cast<float>(dx * dx + dy * dy + dz * dz);
    }

    float distance(const mwmp::WorldPathgridPointRecord& left, const mwmp::WorldPathgridPointRecord& right)
    {
        const double dx = static_cast<double>(left.x) - right.x;
        const double dy = static_cast<double>(left.y) - right.y;
        const double dz = static_cast<double>(left.z) - right.z;
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    bool finitePosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2]);
    }
}

namespace mwmp
{
    void ServerPathgridNavigator::rebuild(std::string cellKey,
        const std::vector<WorldPathgridPointRecord>& points, const std::vector<WorldPathgridEdgeRecord>& edges)
    {
        clear();
        mStats.cellKey = std::move(cellKey);
        mStats.pointCount = points.size();
        mStats.rawEdgeCount = edges.size();
        mStats.available = !points.empty();
        mPoints = points;
        std::sort(mPoints.begin(), mPoints.end(),
            [](const WorldPathgridPointRecord& left, const WorldPathgridPointRecord& right) {
                return left.pointIndex < right.pointIndex;
            });
        mAdjacency.resize(mPoints.size());

        std::vector<std::set<std::size_t>> adjacencySets(mPoints.size());
        for (const WorldPathgridEdgeRecord& edge : edges)
        {
            const std::size_t from = offsetForPointIndex(edge.fromPoint);
            const std::size_t to = offsetForPointIndex(edge.toPoint);
            if (from == invalidPoint || to == invalidPoint || from == to)
            {
                ++mStats.invalidEdgeCount;
                continue;
            }

            adjacencySets[from].insert(to);
            adjacencySets[to].insert(from);
        }

        for (std::size_t offset = 0; offset < adjacencySets.size(); ++offset)
        {
            mAdjacency[offset].reserve(adjacencySets[offset].size());
            for (const std::size_t neighborOffset : adjacencySets[offset])
            {
                mAdjacency[offset].push_back({ neighborOffset, distanceBetweenOffsets(offset, neighborOffset) });
                ++mStats.usableDirectedEdgeCount;
            }
        }

        rebuildConnectedComponents();
    }

    void ServerPathgridNavigator::clear()
    {
        mStats = {};
        mPoints.clear();
        mAdjacency.clear();
        mComponentByOffset.clear();
    }

    const ServerPathgridNavigationStats& ServerPathgridNavigator::statistics() const
    {
        return mStats;
    }

    bool ServerPathgridNavigator::hasPathgrid() const
    {
        return mStats.available;
    }

    bool ServerPathgridNavigator::arePointsConnected(const std::size_t startPoint, const std::size_t endPoint) const
    {
        const std::size_t startOffset = offsetForPointIndex(startPoint);
        const std::size_t endOffset = offsetForPointIndex(endPoint);
        return startOffset != invalidPoint && endOffset != invalidPoint
            && startOffset < mComponentByOffset.size() && endOffset < mComponentByOffset.size()
            && mComponentByOffset[startOffset] == mComponentByOffset[endOffset];
    }

    std::size_t ServerPathgridNavigator::closestPointIndex(const ESM::Position& position) const
    {
        if (mPoints.empty() || !finitePosition(position))
            return invalidPoint;

        std::size_t bestOffset = 0;
        float bestDistance = squaredDistance(mPoints[0], position);
        for (std::size_t offset = 1; offset < mPoints.size(); ++offset)
        {
            const float candidate = squaredDistance(mPoints[offset], position);
            if (candidate < bestDistance)
            {
                bestDistance = candidate;
                bestOffset = offset;
            }
        }

        return mPoints[bestOffset].pointIndex;
    }

    ServerPathgridRoute ServerPathgridNavigator::buildRoute(const ESM::Position& start, const ESM::Position& end,
        const std::size_t maxExpandedNodes) const
    {
        ServerPathgridRoute route;
        route.attempted = true;
        route.pathgridAvailable = hasPathgrid();
        if (!route.pathgridAvailable)
        {
            route.failureReason = "no-pathgrid";
            return route;
        }
        if (!finitePosition(start) || !finitePosition(end))
        {
            route.failureReason = "non-finite-position";
            return route;
        }

        route.startPoint = closestPointIndex(start);
        route.endPoint = closestPointIndex(end);
        const std::size_t startOffset = offsetForPointIndex(route.startPoint);
        const std::size_t endOffset = offsetForPointIndex(route.endPoint);
        if (startOffset == invalidPoint || endOffset == invalidPoint)
        {
            route.failureReason = "missing-nearest-point";
            return route;
        }
        if (startOffset == endOffset)
        {
            route.reachable = true;
            route.direct = true;
            route.waypoints.push_back({ route.startPoint, positionForOffset(startOffset) });
            return route;
        }
        if (!arePointsConnected(route.startPoint, route.endPoint))
        {
            route.failureReason = "disconnected-pathgrid-component";
            return route;
        }

        struct QueueNode
        {
            std::size_t offset = invalidPoint;
            float score = 0.f;
            bool operator<(const QueueNode& other) const { return score > other.score; }
        };

        constexpr float infinity = std::numeric_limits<float>::infinity();
        std::vector<float> gScore(mPoints.size(), infinity);
        std::vector<std::size_t> cameFrom(mPoints.size(), invalidPoint);
        std::vector<bool> closed(mPoints.size(), false);
        std::priority_queue<QueueNode> open;

        gScore[startOffset] = 0.f;
        open.push({ startOffset, distanceBetweenOffsets(startOffset, endOffset) });

        bool found = false;
        while (!open.empty())
        {
            const QueueNode current = open.top();
            open.pop();
            if (current.offset >= mPoints.size() || closed[current.offset])
                continue;

            closed[current.offset] = true;
            ++route.expandedNodeCount;
            if (current.offset == endOffset)
            {
                found = true;
                break;
            }
            if (route.expandedNodeCount >= maxExpandedNodes)
            {
                route.failureReason = "node-expansion-limit";
                return route;
            }

            for (const Neighbor& neighbor : mAdjacency[current.offset])
            {
                if (neighbor.offset >= mPoints.size() || closed[neighbor.offset])
                    continue;

                const float tentative = gScore[current.offset] + neighbor.cost;
                if (tentative >= gScore[neighbor.offset])
                    continue;

                cameFrom[neighbor.offset] = current.offset;
                gScore[neighbor.offset] = tentative;
                open.push({ neighbor.offset, tentative + distanceBetweenOffsets(neighbor.offset, endOffset) });
            }
        }

        if (!found)
        {
            route.failureReason = "no-route";
            return route;
        }

        route.reachable = true;
        route.pathCost = gScore[endOffset];

        std::vector<std::size_t> reversedOffsets;
        for (std::size_t cursor = endOffset; cursor != invalidPoint; cursor = cameFrom[cursor])
        {
            reversedOffsets.push_back(cursor);
            if (cursor == startOffset)
                break;
        }

        route.waypoints.reserve(reversedOffsets.size());
        for (auto it = reversedOffsets.rbegin(); it != reversedOffsets.rend(); ++it)
            route.waypoints.push_back({ mPoints[*it].pointIndex, positionForOffset(*it) });

        return route;
    }

    std::size_t ServerPathgridNavigator::offsetForPointIndex(const std::size_t pointIndex) const
    {
        const auto found = std::lower_bound(mPoints.begin(), mPoints.end(), pointIndex,
            [](const WorldPathgridPointRecord& point, const std::size_t value) {
                return point.pointIndex < value;
            });

        if (found == mPoints.end() || found->pointIndex != pointIndex)
            return invalidPoint;

        return static_cast<std::size_t>(found - mPoints.begin());
    }

    ESM::Position ServerPathgridNavigator::positionForOffset(const std::size_t offset) const
    {
        ESM::Position position;
        if (offset >= mPoints.size())
            return position;

        position.pos[0] = static_cast<float>(mPoints[offset].x);
        position.pos[1] = static_cast<float>(mPoints[offset].y);
        position.pos[2] = static_cast<float>(mPoints[offset].z);
        return position;
    }

    float ServerPathgridNavigator::distanceBetweenOffsets(const std::size_t left, const std::size_t right) const
    {
        if (left >= mPoints.size() || right >= mPoints.size())
            return 0.f;

        return distance(mPoints[left], mPoints[right]);
    }

    void ServerPathgridNavigator::rebuildConnectedComponents()
    {
        mComponentByOffset.assign(mPoints.size(), invalidPoint);
        std::size_t component = 0;
        std::vector<std::size_t> stack;
        for (std::size_t offset = 0; offset < mPoints.size(); ++offset)
        {
            if (mComponentByOffset[offset] != invalidPoint)
                continue;

            std::size_t componentSize = 0;
            stack.clear();
            stack.push_back(offset);
            mComponentByOffset[offset] = component;
            while (!stack.empty())
            {
                const std::size_t current = stack.back();
                stack.pop_back();
                ++componentSize;

                for (const Neighbor& neighbor : mAdjacency[current])
                {
                    if (neighbor.offset >= mComponentByOffset.size()
                        || mComponentByOffset[neighbor.offset] != invalidPoint)
                        continue;

                    mComponentByOffset[neighbor.offset] = component;
                    stack.push_back(neighbor.offset);
                }
            }

            ++component;
            mStats.largestConnectedComponentSize = std::max(mStats.largestConnectedComponentSize, componentSize);
        }

        mStats.connectedComponentCount = component;
    }
}
