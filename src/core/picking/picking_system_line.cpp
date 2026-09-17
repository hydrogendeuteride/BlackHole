#include "picking_system.h"
#include "picking_system_internal.h"

#include "core/config.h"
#include "core/context.h"
#include "core/device/images.h"
#include "core/device/swapchain.h"
#include "scene/planet/planet_system.h"

#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace PickingInternal;

glm::vec2 PickingSystem::window_to_swapchain_pixels(const glm::vec2 &window_pos) const
{
    if (_context == nullptr || _context->window == nullptr || _context->getSwapchain() == nullptr)
    {
        return window_pos;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(_context->window, &win_w, &win_h);

    int draw_w = 0, draw_h = 0;
    SDL_Vulkan_GetDrawableSize(_context->window, &draw_w, &draw_h);

    glm::vec2 scale{1.0f, 1.0f};
    if (win_w > 0 && win_h > 0 && draw_w > 0 && draw_h > 0)
    {
        scale.x = static_cast<float>(draw_w) / static_cast<float>(win_w);
        scale.y = static_cast<float>(draw_h) / static_cast<float>(win_h);
    }

    glm::vec2 drawable_pos{window_pos.x * scale.x, window_pos.y * scale.y};

    VkExtent2D drawable_extent{0, 0};
    if (draw_w > 0 && draw_h > 0)
    {
        drawable_extent.width = static_cast<uint32_t>(draw_w);
        drawable_extent.height = static_cast<uint32_t>(draw_h);
    }
    if ((drawable_extent.width == 0 || drawable_extent.height == 0) && _context->getSwapchain())
    {
        drawable_extent = _context->getSwapchain()->windowExtent();
    }

    VkExtent2D swap = _context->getSwapchain()->swapchainExtent();
    if (drawable_extent.width == 0 || drawable_extent.height == 0 || swap.width == 0 || swap.height == 0)
    {
        return drawable_pos;
    }

    const float sx = static_cast<float>(swap.width) / static_cast<float>(drawable_extent.width);
    const float sy = static_cast<float>(swap.height) / static_cast<float>(drawable_extent.height);
    return glm::vec2{drawable_pos.x * sx, drawable_pos.y * sy};
}

bool PickingSystem::compute_camera_ray(const glm::vec2 &window_pos, CameraRay &out_ray) const
{
    if (_context == nullptr || _context->scene == nullptr)
    {
        return false;
    }

    SwapchainManager *swapchain = _context->getSwapchain();
    if (swapchain == nullptr)
    {
        return false;
    }

    VkExtent2D dstExtent = swapchain->swapchainExtent();
    if (dstExtent.width == 0 || dstExtent.height == 0)
    {
        return false;
    }

    VkExtent2D logicalExtent{kRenderWidth, kRenderHeight};
    if (_context)
    {
        VkExtent2D ctxLogical = _context->getLogicalRenderExtent();
        if (ctxLogical.width > 0 && ctxLogical.height > 0)
        {
            logicalExtent = ctxLogical;
        }
    }

    glm::vec2 logicalPos{};
    const glm::vec2 swapchain_pos = window_to_swapchain_pixels(window_pos);
    if (!vkutil::map_window_to_letterbox_src(swapchain_pos, logicalExtent, dstExtent, logicalPos))
    {
        return false;
    }

    const double width = static_cast<double>(logicalExtent.width);
    const double height = static_cast<double>(logicalExtent.height);
    if (!(width > 0.0) || !(height > 0.0))
    {
        return false;
    }

    // Convert from logical view coordinates (top-left origin) to NDC in [-1, 1].
    const double ndcX = (2.0 * static_cast<double>(logicalPos.x) / width) - 1.0;
    const double ndcY = 1.0 - (2.0 * static_cast<double>(logicalPos.y) / height);

    const Camera &cam = _context->scene->getMainCamera();
    const double fovRad = static_cast<double>(cam.fovDegrees) * (3.14159265358979323846 / 180.0);
    const double tanHalfFov = std::tan(fovRad * 0.5);
    const double aspect = width / height;

    // Build ray in camera space using -Z forward convention.
    glm::dvec3 dirCamera(ndcX * aspect * tanHalfFov,
                         ndcY * tanHalfFov,
                         -1.0);
    const double dir_len2 = glm::dot(dirCamera, dirCamera);
    if (!(dir_len2 > 0.0) || !std::isfinite(dir_len2))
    {
        return false;
    }
    dirCamera /= std::sqrt(dir_len2);

    const WorldVec3 origin_world = _context->scene->get_world_origin();
    const WorldVec3 cam_world = cam.position_world;

    const glm::dvec3 rayOrigin = glm::dvec3(cam_world - origin_world);
    const glm::mat4 camRotation = cam.getRotationMatrix();
    const glm::vec3 dir_camera_f(static_cast<float>(dirCamera.x),
                                 static_cast<float>(dirCamera.y),
                                 static_cast<float>(dirCamera.z));
    glm::vec3 dir_world_f = glm::vec3(camRotation * glm::vec4(dir_camera_f, 0.0f));
    const float dir_world_len2_f = glm::dot(dir_world_f, dir_world_f);
    if (!(dir_world_len2_f > 0.0f) || !std::isfinite(dir_world_len2_f))
    {
        return false;
    }
    dir_world_f = glm::normalize(dir_world_f);

    out_ray.origin_world = origin_world;
    out_ray.camera_world = cam_world;
    out_ray.origin_local = rayOrigin;
    out_ray.dir_local = glm::dvec3(dir_world_f);
    out_ray.fov_y_rad = fovRad;
    out_ray.viewport_height_px = height;
    return true;
}

bool PickingSystem::pick_line_at_window_pos(const glm::vec2 &window_pos, PickInfo &out_pick, double &out_depth_m) const
{
    out_depth_m = 0.0;
    clear_pick(out_pick);

    if (_context == nullptr || _context->scene == nullptr)
    {
        return false;
    }

    if (!_settings.enabled || !_settings.enable_line_picking ||
        (_owned_line_pick_segments.empty() && _line_pick_batches.empty()))
    {
        return false;
    }

    float radius_px = _settings.line_pick_radius_px;
    if (!std::isfinite(radius_px) || radius_px <= 0.0f)
    {
        return false;
    }

    CameraRay ray{};
    if (!compute_camera_ray(window_pos, ray))
    {
        return false;
    }

    const double tan_half_fov = std::tan(ray.fov_y_rad * 0.5);
    if (!std::isfinite(tan_half_fov) || !(tan_half_fov > 0.0) || !(ray.viewport_height_px > 0.0))
    {
        return false;
    }

    const auto point_occluded_by_planet = [&](const glm::dvec3 &point_local) {
        PlanetSystem *planets = _context->scene->get_planet_system();
        if (!planets)
        {
            return false;
        }

        const glm::dvec3 to_point = point_local - ray.origin_local;
        const double point_depth = glm::length(to_point);
        if (!(point_depth > 0.0) || !std::isfinite(point_depth))
        {
            return false;
        }

        const glm::dvec3 point_dir = to_point / point_depth;
        for (const PlanetSystem::PlanetBody &body : planets->bodies())
        {
            if (!body.visible)
            {
                continue;
            }

            const double radius_m = std::max(0.0, body.radius_m);
            const glm::dvec3 center_local = glm::dvec3(body.center_world - ray.origin_world);
            double planet_depth = 0.0;
            if (intersect_ray_sphere_depth(ray.origin_local, point_dir, center_local, radius_m, planet_depth) &&
                planet_depth + 1.0 < point_depth)
            {
                return true;
            }
        }
        return false;
    };

    bool any_hit = false;
    double best_px_dist = std::numeric_limits<double>::infinity();
    double best_depth = std::numeric_limits<double>::infinity();
    uint32_t best_group_id = 0;
    const LinePickSegmentData *best_seg = nullptr;
    double best_t = 0.0;

    const auto consider_segment = [&](const uint32_t group_id, const LinePickSegmentData &seg) {
        if (group_id >= _line_pick_groups.size())
        {
            return;
        }

        const glm::dvec3 a_local = world_to_local_d(seg.a_world, ray.origin_world);
        const glm::dvec3 b_local = world_to_local_d(seg.b_world, ray.origin_world);

        const RaySegmentClosest c = closest_points_ray_segment(ray.origin_local, ray.dir_local, a_local, b_local);
        if (!std::isfinite(c.dist2) || !std::isfinite(c.ray_s) || !std::isfinite(c.seg_t))
        {
            return;
        }
        if (!(c.ray_s > 0.0))
        {
            return;
        }
        const glm::dvec3 picked_point_local = a_local + (b_local - a_local) * std::clamp(c.seg_t, 0.0, 1.0);
        if (point_occluded_by_planet(picked_point_local))
        {
            return;
        }

        const double meters_per_px = (2.0 * tan_half_fov * c.ray_s) / ray.viewport_height_px;
        if (!std::isfinite(meters_per_px) || !(meters_per_px > 0.0))
        {
            return;
        }

        const double dist_m = std::sqrt(std::max(0.0, c.dist2));
        const double dist_px = dist_m / meters_per_px;
        if (!(dist_px <= static_cast<double>(radius_px)))
        {
            return;
        }

        if (!any_hit || dist_px < best_px_dist || (dist_px == best_px_dist && c.ray_s < best_depth))
        {
            any_hit = true;
            best_px_dist = dist_px;
            best_depth = c.ray_s;
            best_group_id = group_id;
            best_seg = &seg;
            best_t = std::clamp(c.seg_t, 0.0, 1.0);
        }
    };

    for (const LinePickSegment &owned : _owned_line_pick_segments)
    {
        consider_segment(owned.group_id, owned.data);
    }

    for (const LinePickBatch &batch : _line_pick_batches)
    {
        if (batch.group_id >= _line_pick_groups.size() || batch.segments == nullptr || batch.count == 0)
        {
            continue;
        }

        for (size_t i = 0; i < batch.count; ++i)
        {
            consider_segment(batch.group_id, batch.segments[i]);
        }
    }

    if (!any_hit || best_seg == nullptr)
    {
        return false;
    }

    const LinePickGroup &group = _line_pick_groups[best_group_id];

    const WorldVec3 hit_world = (1.0 - best_t) * best_seg->a_world + best_t * best_seg->b_world;
    double hit_time_s = std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(best_seg->a_time_s) && std::isfinite(best_seg->b_time_s))
    {
        hit_time_s = best_seg->a_time_s + (best_seg->b_time_s - best_seg->a_time_s) * best_t;
    }

    out_pick.mesh = nullptr;
    out_pick.scene = nullptr;
    out_pick.node = nullptr;
    out_pick.ownerType = RenderObject::OwnerType::None;
    out_pick.ownerName = group.owner_name;
    out_pick.objectName = group.owner_name;
    out_pick.memberName = group.owner_name;
    out_pick.nodeName.clear();
    out_pick.nodeParentName.clear();
    out_pick.nodeChildren.clear();
    out_pick.nodePath.clear();
    out_pick.worldPos = hit_world;
    out_pick.worldTransform = glm::mat4(1.0f);
    out_pick.indexCount = 0;
    out_pick.firstIndex = 0;
    out_pick.surfaceIndex = 0;
    out_pick.time_s = hit_time_s;
    out_pick.line_payload = group.payload;
    out_pick.line_segment_primary_id = best_seg->primary_id;
    out_pick.line_segment_secondary_id = best_seg->secondary_id;
    out_pick.kind = PickInfo::Kind::Line;
    out_pick.selectionLevel = SelectionLevel::Object;
    out_pick.valid = true;

    out_depth_m = best_depth;
    return true;
}

bool PickingSystem::pick_line(const glm::vec2 &window_pos, PickInfo &out_pick, double *out_depth_m) const
{
    double depth_m = 0.0;
    if (!pick_line_at_window_pos(window_pos, out_pick, depth_m))
    {
        return false;
    }
    if (!pick_allowed(out_pick, PickUse::Click))
    {
        clear_pick(out_pick);
        return false;
    }
    if (out_depth_m)
    {
        *out_depth_m = depth_m;
    }
    return true;
}

void PickingSystem::clear_line_picks()
{
    _line_pick_groups.clear();
    _owned_line_pick_segments.clear();
    _line_pick_batches.clear();
}

uint32_t PickingSystem::add_line_pick_group(std::string owner_name, const LinePickPayload payload)
{
    LinePickGroup g{};
    g.owner_name = std::move(owner_name);
    g.payload = payload;
    const uint32_t id = static_cast<uint32_t>(_line_pick_groups.size());
    _line_pick_groups.push_back(std::move(g));
    return id;
}

void PickingSystem::add_line_pick_segment(uint32_t group_id,
                                          const WorldVec3 &a_world,
                                          const WorldVec3 &b_world,
                                          double a_time_s,
                                          double b_time_s,
                                          uint64_t primary_id,
                                          uint64_t secondary_id)
{
    if (group_id >= _line_pick_groups.size())
    {
        return;
    }

    LinePickSegment seg{};
    seg.group_id = group_id;
    seg.data.a_world = a_world;
    seg.data.b_world = b_world;
    seg.data.a_time_s = a_time_s;
    seg.data.b_time_s = b_time_s;
    seg.data.primary_id = primary_id;
    seg.data.secondary_id = secondary_id;
    _owned_line_pick_segments.push_back(std::move(seg));
}

void PickingSystem::add_line_pick_segments(uint32_t group_id, const std::span<const LinePickSegmentData> segments)
{
    if (group_id >= _line_pick_groups.size() || segments.empty())
    {
        return;
    }

    LinePickBatch batch{};
    batch.group_id = group_id;
    batch.segments = segments.data();
    batch.count = segments.size();
    _line_pick_batches.push_back(batch);
}
