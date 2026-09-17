#pragma once

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Physics
{
    struct SymmetricInertia
    {
        double xx{0.0};
        double yy{0.0};
        double zz{0.0};
        double xy{0.0};
        double xz{0.0};
        double yz{0.0};

        [[nodiscard]] glm::dmat3 matrix() const
        {
            glm::dmat3 out(0.0);
            out[0][0] = xx;
            out[1][1] = yy;
            out[2][2] = zz;
            out[1][0] = out[0][1] = xy;
            out[2][0] = out[0][2] = xz;
            out[2][1] = out[1][2] = yz;
            return out;
        }

        [[nodiscard]] static SymmetricInertia from_matrix(
                const glm::dmat3 &value)
        {
            return {
                    .xx = value[0][0],
                    .yy = value[1][1],
                    .zz = value[2][2],
                    .xy = 0.5 * (value[1][0] + value[0][1]),
                    .xz = 0.5 * (value[2][0] + value[0][2]),
                    .yz = 0.5 * (value[2][1] + value[1][2]),
            };
        }
    };

    inline bool finite_matrix(const glm::dmat3 &value)
    {
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                if (!std::isfinite(value[column][row]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    inline glm::dmat3 symmetrized(const glm::dmat3 &value)
    {
        return 0.5 * (value + glm::transpose(value));
    }

    inline std::array<double, 3> principal_moments(
            const glm::dmat3 &tensor)
    {
        const glm::dmat3 value = symmetrized(tensor);
        const double p1 = value[1][0] * value[1][0] +
                value[2][0] * value[2][0] +
                value[2][1] * value[2][1];
        std::array<double, 3> result{};
        if (!(p1 > 0.0))
        {
            result = {value[0][0], value[1][1], value[2][2]};
            std::sort(result.begin(), result.end());
            return result;
        }

        constexpr double pi = 3.14159265358979323846;
        const double q = (value[0][0] + value[1][1] + value[2][2]) / 3.0;
        const double p2 =
                (value[0][0] - q) * (value[0][0] - q) +
                (value[1][1] - q) * (value[1][1] - q) +
                (value[2][2] - q) * (value[2][2] - q) +
                2.0 * p1;
        const double p = std::sqrt(std::max(0.0, p2 / 6.0));
        if (!(p > 0.0) || !std::isfinite(p))
        {
            return {q, q, q};
        }

        const glm::dmat3 normalized =
                (value - glm::dmat3(1.0) * q) / p;
        const double r = std::clamp(glm::determinant(normalized) / 2.0,
                                    -1.0,
                                    1.0);
        const double phi = std::acos(r) / 3.0;
        const double largest = q + 2.0 * p * std::cos(phi);
        const double smallest = q + 2.0 * p * std::cos(phi + 2.0 * pi / 3.0);
        const double middle = 3.0 * q - largest - smallest;
        result = {smallest, middle, largest};
        std::sort(result.begin(), result.end());
        return result;
    }

    inline bool valid_inertia_tensor(const glm::dmat3 &value,
                                     const bool require_rigid_body_triangle = true)
    {
        if (!finite_matrix(value))
        {
            return false;
        }
        const glm::dmat3 tensor = symmetrized(value);
        const auto moments = principal_moments(tensor);
        if (!(moments[0] > 0.0) || !std::isfinite(moments[2]))
        {
            return false;
        }
        if (!require_rigid_body_triangle)
        {
            return true;
        }
        const double tolerance = std::max(1.0, moments[2]) * 1.0e-9;
        return moments[0] + moments[1] + tolerance >= moments[2];
    }

    struct MassProperties
    {
        double mass_kg{0.0};
        glm::dvec3 center_of_mass_local_m{0.0};
        glm::dmat3 inertia_local_kgm2{0.0};

        [[nodiscard]] bool valid() const
        {
            return mass_kg > 0.0 &&
                   std::isfinite(mass_kg) &&
                   std::isfinite(center_of_mass_local_m.x) &&
                   std::isfinite(center_of_mass_local_m.y) &&
                   std::isfinite(center_of_mass_local_m.z) &&
                   valid_inertia_tensor(inertia_local_kgm2);
        }

        [[nodiscard]] glm::dmat3 inverse_inertia_local() const
        {
            return valid() ? glm::inverse(inertia_local_kgm2)
                           : glm::dmat3(0.0);
        }
    };

    inline glm::dmat3 parallel_axis_tensor(const double mass_kg,
                                           const glm::dvec3 &offset_m)
    {
        return mass_kg *
                (glm::dmat3(1.0) * glm::dot(offset_m, offset_m) -
                 glm::outerProduct(offset_m, offset_m));
    }

    inline bool near(const MassProperties &a,
                     const MassProperties &b,
                     const double relative_tolerance = 1.0e-6)
    {
        if (!a.valid() || !b.valid())
        {
            return false;
        }
        const double mass_scale = std::max({1.0, a.mass_kg, b.mass_kg});
        if (std::abs(a.mass_kg - b.mass_kg) > relative_tolerance * mass_scale ||
            glm::length(a.center_of_mass_local_m - b.center_of_mass_local_m) >
                    relative_tolerance)
        {
            return false;
        }
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                const double scale = std::max({1.0,
                                               std::abs(a.inertia_local_kgm2[column][row]),
                                               std::abs(b.inertia_local_kgm2[column][row])});
                if (std::abs(a.inertia_local_kgm2[column][row] -
                             b.inertia_local_kgm2[column][row]) >
                    relative_tolerance * scale)
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace Physics
