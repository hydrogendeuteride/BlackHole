#pragma once

#include "render/graph/types.h"
#include <glm/glm.hpp>

// Shared by the shadow-map generator and deferred lighting (80-byte GPU layout).
struct CloudShadowData
{
    glm::vec4 origin{0.0f};       // local-space origin of the sun-perpendicular map
    glm::vec4 axis_x{0.0f};       // xyz: unit axis, w: map half extent in meters
    glm::vec4 axis_y{0.0f};       // xyz: unit axis, w: shadow strength (zero disables)
    glm::vec4 planet{0.0f};      // local center and radius
    glm::vec4 layer{0.0f};       // base height, thickness, sample count, map size
};
static_assert(sizeof(CloudShadowData) == 80);

// Generator uses planet-radius units, never subtracts float world positions.
struct CloudShadowPush
{
    glm::vec4 plane;   // xy: projected center / radius, z: extent / radius, w: radius
    glm::vec4 axis_x;
    glm::vec4 axis_y;
    glm::vec4 sun;     // xyz: unit direction, w: extinction * density
    glm::vec4 layer;   // base / radius, thickness / radius, coverage, weather blend
    glm::vec4 motion;  // wind sin/cos, overlay sin/cos
    glm::vec4 noise;   // scale, evolution xy, step count
    glm::vec4 misc;    // flip V, type bias, erosion, weather available
};
static_assert(sizeof(CloudShadowPush) == 128);

struct CloudShadowMap
{
    RGImageHandle image;
    CloudShadowData data;
};
