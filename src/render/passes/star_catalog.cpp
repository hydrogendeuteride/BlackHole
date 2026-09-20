#include "star_catalog.h"

#include "core/context.h"
#include "core/assets/manager.h"
#include "core/device/resource.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

void StarCatalog::init(EngineContext *context)
{
    // Must match stars.glsl. Duplicate each star into every cell touched by
    // its spherical cap, including longitude wrapping and polar caps.
    constexpr int WIDTH = 256;
    constexpr int HEIGHT = 128;
    constexpr double PI = std::numbers::pi;
    constexpr double SUPPORT = PI / 180.0; // One degree, covers the widest PSF.
    struct Star { glm::vec4 direction; glm::vec4 photometry; };
    std::vector<Star> data;
    std::vector<std::vector<uint32_t>> bins(WIDTH * HEIGHT);
    const auto path = context->getAssets()->assetPath("stars/hyg_v41.csv");
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Missing star catalog: " + path);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        uint32_t id;
        double ra, dec;
        float mag, bv;
        if (!(row >> id >> ra >> dec >> mag >> bv) ||
            !std::isfinite(ra) || !std::isfinite(dec) || !std::isfinite(mag) || !std::isfinite(bv) ||
            ra < 0.0 || ra >= 24.0 || dec < -90.0 || dec > 90.0)
            throw std::runtime_error("Invalid star catalog row: " + path);
        ra *= PI / 12.0;
        dec *= PI / 180.0;
        const uint32_t index = static_cast<uint32_t>(data.size());
        data.push_back({glm::vec4(std::cos(dec) * std::cos(ra), std::sin(dec),
                                  std::cos(dec) * std::sin(ra), 0.0),
                        glm::vec4(mag, bv, 0.0f, 0.0f)});
        const double longitude = ra / (2.0 * PI) + 0.5;
        const double latitude = 0.5 - dec / PI;
        const int y0 = std::clamp(int(std::floor((latitude - SUPPORT / PI) * HEIGHT)), 0, HEIGHT - 1);
        const int y1 = std::clamp(int(std::floor((latitude + SUPPORT / PI) * HEIGHT)), 0, HEIGHT - 1);
        const bool polar = std::abs(dec) + SUPPORT >= PI * 0.5;
        const double span = polar ? 0.5 : std::asin(std::sin(SUPPORT) / std::cos(dec)) / (2.0 * PI);
        const int x0 = polar ? 0 : int(std::floor((longitude - span) * WIDTH));
        const int x1 = polar ? WIDTH - 1 : int(std::floor((longitude + span) * WIDTH));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                bins[y * WIDTH + (x % WIDTH + WIDTH) % WIDTH].push_back(index);
    }
    if (data.empty()) throw std::runtime_error("Empty star catalog: " + path);
    // Header: two uints per cell (offset, count), followed by star indices.
    std::vector<uint32_t> lookup(WIDTH * HEIGHT * 2);
    for (size_t i = 0; i < bins.size(); ++i)
    {
        lookup[i * 2] = static_cast<uint32_t>(lookup.size());
        lookup[i * 2 + 1] = static_cast<uint32_t>(bins[i].size());
        lookup.insert(lookup.end(), bins[i].begin(), bins[i].end());
    }
    auto *resources = context->getResources();
    stars = resources->upload_buffer(data.data(), data.size() * sizeof(Star), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    cells = resources->upload_buffer(lookup.data(), lookup.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    count = static_cast<uint32_t>(data.size());
    Logger::info("[Stars] Loaded {} HYG v4.1 stars", count);
}

void StarCatalog::cleanup(EngineContext *context)
{
    if (stars.buffer) context->getResources()->destroy_buffer(stars);
    if (cells.buffer) context->getResources()->destroy_buffer(cells);
    stars = {};
    cells = {};
    count = 0;
}
