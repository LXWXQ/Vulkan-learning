#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct AABB
{
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 0.0f };
    bool valid = false;

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }
};

// D-7: uniform-grid spatial index for CPU broad phase.
// Current scene has only Sponza + skybox; a grid is enough to validate the
// insert -> AABB query -> exact frustum test path and maps well to future chunks.
class SpatialIndex
{
public:
    explicit SpatialIndex(float cellSize = 16.0f);

    void clear();
    void insert(entt::entity entity, const AABB& bounds);
    void query(const AABB& bounds, std::vector<entt::entity>& outEntities) const;

    size_t cellCount() const { return cells.size(); }
    size_t entryCount() const { return entries; }private:
    struct Cell
    {
        int x = 0, y = 0, z = 0;
        bool operator==(const Cell& other) const { return x == other.x && y == other.y && z == other.z; }
    };

    struct CellHash
    {
        size_t operator()(const Cell& c) const
        {
            const uint64_t hx = static_cast<uint32_t>(c.x) & 0x1FFFFFu;
            const uint64_t hy = static_cast<uint32_t>(c.y) & 0x1FFFFFu;
            const uint64_t hz = static_cast<uint32_t>(c.z) & 0x1FFFFFu;
            return static_cast<size_t>((hx << 42) | (hy << 21) | hz);
        }
    };

    static Cell cellOf(const glm::vec3& p, float size);
    void collectAll(std::vector<entt::entity>& outEntities) const;

    float cellSize;
    size_t entries = 0;
    std::unordered_map<Cell, std::vector<entt::entity>, CellHash> cells;
    std::vector<entt::entity> oversized;
};