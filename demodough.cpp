#include "demodough.h"
#include <cmath>
DoughFrame makeDemoDough(float cooked, float browned, float moisture, float puff, float time) {
    DoughFrame f;
    constexpr unsigned majorSegments = 128;
    constexpr unsigned tubeSegments = 64;
    constexpr float pi = 3.14159265358979323846f;
    const float majorRadius = .08f;
    const float tubeRadius = .022f * (1 + .5f * puff);
    const float verticalRadius = .018f * (1 + .9f * puff);
    for (unsigned u = 0; u < majorSegments; ++u)
        for (unsigned v = 0; v < tubeSegments; ++v) {
            float a = 2 * pi * u / majorSegments, b = 2 * pi * v / tubeSegments;
            float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
            float nx = cb * ca / tubeRadius, ny = sb / verticalRadius, nz = cb * sa / tubeRadius;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            // Uneven browning is illustrative, not calculated heat transfer.
            float local = browned * (.7f + .3f * (.5f + .5f * sb));
            f.vertices.push_back({(majorRadius + tubeRadius * cb) * ca, verticalRadius * sb,
                                  (majorRadius + tubeRadius * cb) * sa, nx / len, ny / len,
                                  nz / len, cooked, local, moisture});
        }
    for (unsigned u = 0; u < majorSegments; ++u)
        for (unsigned v = 0; v < tubeSegments; ++v) {
            unsigned a = u * tubeSegments + v, b = ((u + 1) % majorSegments) * tubeSegments + v,
                     c = ((u + 1) % majorSegments) * tubeSegments + (v + 1) % tubeSegments,
                     d = u * tubeSegments + (v + 1) % tubeSegments;
            f.indices.insert(f.indices.end(), {a, d, b, b, d, c});
        }
    // Cheap volume-preserving affine wobble; illustrative animation only.
    const float sx = 1 + .13f * std::sin(time * 1.8f);
    const float sy = 1 + .10f * std::sin(time * 1.8f + 1.2f);
    const float sz = 1 / (sx * sy), shear = .12f * std::sin(time * 1.3f);
    for (auto& v : f.vertices) {
        const float x = v.x, y = v.y;
        v.x = sx * x + shear * y + .006f * std::sin(time * .8f);
        v.y = sy * y;
        v.z *= sz;
        float nx = v.nx / sx, ny = (v.ny - shear * nx) / sy, nz = v.nz / sz;
        float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        v.nx = nx / length;
        v.ny = ny / length;
        v.nz = nz / length;
    }
    return f;
}
