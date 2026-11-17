#pragma once

#include <array>
#include <vector>

namespace RayLut
{
    // Include u=2/3 and zero radial travel exactly (the circular photon orbit).
    constexpr unsigned WIDTH = 97;
    constexpr unsigned HEIGHT = 65;
    constexpr unsigned DEPTH = 17;
    constexpr double MAX_STEP = 0.04;
    constexpr double MAX_TRAVEL = 0.08;

    inline std::vector<std::array<float, 2>> generate()
    {
        std::vector<std::array<float, 2>> values(WIDTH * HEIGHT * DEPTH);
        for (unsigned z = 0; z < DEPTH; ++z)
        {
            const double h2 = MAX_STEP * MAX_STEP * z / (DEPTH - 1);
            for (unsigned y = 0; y < HEIGHT; ++y)
            {
                const double s = MAX_TRAVEL * (2.0 * y / (HEIGHT - 1) - 1.0);
                for (unsigned x = 0; x < WIDTH; ++x)
                {
                    const double u = double(x) / (WIDTH - 1);
                    // RK4 acceleration divided by the initial u. Storing only
                    // the curvature correction avoids cancellation at small u/h.
                    const auto acceleration = [u](double ratio)
                    {
                        return -ratio * (1.0 - 1.5 * u * ratio);
                    };
                    const double a = acceleration(1.0);
                    const double b = acceleration(1.0 + 0.5 * s);
                    const double c = acceleration(1.0 + 0.5 * s + 0.25 * h2 * a);
                    const double d = acceleration(1.0 + s + 0.5 * h2 * b);
                    values[(z * HEIGHT + y) * WIDTH + x] = {
                        float((a + b + c) / 6.0),
                        float((a + 2.0 * b + 2.0 * c + d) / 6.0)};
                }
            }
        }
        return values;
    }
}
