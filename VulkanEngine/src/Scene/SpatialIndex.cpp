#include "Scene/SpatialIndex.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool entityLess(entt::entity a, entt::entity b)
    {
        return entt::to_integral(a) < entt::to_integral(b);
    }

    void sortUnique(std::vector<entt::entity>& entities)
    {
        std::sort(entities.begin(), entities.end(), entityLess);
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    }
}

SpatialIndex::SpatialIndex(float cellSize)
    : cellSize(cellSize > 0.0f ? cellSize : 16.0f)
{
}

void SpatialIndex::clear()
{
    cells.clear();
    oversized.clear();
    entries = 0;
}

SpatialIndex::Cell SpatialIndex::cellOf(const glm::vec3& p, float size)
{
    return Cell{
        static_cast<int>(std::floor(p.x / size)),
        static_cast<int>(std::floor(p.y / size)),
        static_cast<int>(std::floor(p.z / size))
    };
}

void SpatialIndex::insert(entt::entity entity, const AABB& bounds)
{
    if (!bounds.valid)
        return;

    const Cell minCell = cellOf(bounds.min, cellSize);
    const Cell maxCell = cellOf(bounds.max, cellSize);

    const int64_t sx = static_cast<int64_t>(maxCell.x) - minCell.x + 1;
    const int64_t sy = static_cast<int64_t>(maxCell.y) - minCell.y + 1;
    const int64_t sz = static_cast<int64_t>(maxCell.z) - minCell.z + 1;
    const int64_t cellCount = sx * sy * sz;

    // Huge objects go into an oversized list so a single object cannot explode
    // the grid. Oversized entries are always included as query candidates.
    if (cellCount <= 0 || cellCount > 4096)
    {
        oversized.push_back(entity);
        ++entries;
        return;
    }

    for (int z = minCell.z; z <= maxCell.z; ++z)
        for (int y = minCell.y; y <= maxCell.y; ++y)
            for (int x = minCell.x; x <= maxCell.x; ++x)
                cells[Cell{ x, y, z }].push_back(entity);

    ++entries;
}

void SpatialIndex::collectAll(std::vector<entt::entity>& outEntities) const
{
    for (const auto& [cell, bucket] : cells)
    {
        (void)cell;
        outEntities.insert(outEntities.end(), bucket.begin(), bucket.end());
    }
    outEntities.insert(outEntities.end(), oversized.begin(), oversized.end());
}

void SpatialIndex::query(const AABB& bounds, std::vector<entt::entity>& outEntities) const
{
    outEntities.clear();
    if (!bounds.valid)
        return;

    const Cell minCell = cellOf(bounds.min, cellSize);
    const Cell maxCell = cellOf(bounds.max, cellSize);

    const int64_t sx = static_cast<int64_t>(maxCell.x) - minCell.x + 1;
    const int64_t sy = static_cast<int64_t>(maxCell.y) - minCell.y + 1;
    const int64_t sz = static_cast<int64_t>(maxCell.z) - minCell.z + 1;
    const int64_t cellCount = sx * sy * sz;

    if (cellCount <= 0 || cellCount > 4096)
    {
        collectAll(outEntities);
        sortUnique(outEntities);
        return;
    }

    for (int z = minCell.z; z <= maxCell.z; ++z)
        for (int y = minCell.y; y <= maxCell.y; ++y)
            for (int x = minCell.x; x <= maxCell.x; ++x)
            {
                auto it = cells.find(Cell{ x, y, z });
                if (it != cells.end())
                    outEntities.insert(outEntities.end(), it->second.begin(), it->second.end());
            }

    outEntities.insert(outEntities.end(), oversized.begin(), oversized.end());
    sortUnique(outEntities);
}