#pragma once
#include <cstdint>
#include <vector>
// Metres, right-handed coordinates, +Y up. Normals point into air.
struct DoughVertex {
    float x, y, z;
    float nx, ny, nz;
    float cooked, browned, moisture; // Each in [0,1], visual parameters.
};
struct DoughFrame {
    std::vector<DoughVertex> vertices;
    std::vector<std::uint32_t> indices; // CCW triangles viewed from air.
};
