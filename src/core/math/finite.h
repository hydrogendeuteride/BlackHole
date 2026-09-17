#pragma once

#include <glm/vec3.hpp>

#include <cmath>

namespace MathUtil
{
    [[nodiscard]] inline bool finite(const glm::vec3 &value) noexcept
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y) &&
               std::isfinite(value.z);
    }

    [[nodiscard]] inline bool finite(const glm::dvec3 &value) noexcept
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y) &&
               std::isfinite(value.z);
    }
} // namespace MathUtil
