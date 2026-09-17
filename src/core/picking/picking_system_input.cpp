#include "picking_system.h"
#include "picking_system_internal.h"

#include "core/context.h"
#include "core/device/swapchain.h"
#include "scene/planet/planet_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace PickingInternal;

void PickingSystem::process_input(const InputSystem &input,
                                  bool ui_want_capture_mouse,
                                  const uint32_t consumed_mouse_button_mask)
{
    if (_context == nullptr)
    {
        return;
    }

    float click_threshold_px = _settings.click_threshold_px;
    if (!std::isfinite(click_threshold_px) || click_threshold_px < 0.0f)
    {
        click_threshold_px = 0.0f;
    }

    const uint32_t select_button_mask = _settings.select_button_mask;
    auto is_select_button = [select_button_mask, consumed_mouse_button_mask](MouseButton button) -> bool {
        const uint32_t bit = mouse_button_mask(button);
        return (select_button_mask & bit) != 0u &&
               (consumed_mouse_button_mask & bit) == 0u;
    };

    for (const InputEvent &event : input.events())
    {
        if (event.type == InputEvent::Type::MouseMove)
        {
            _mouse_pos_window = event.mouse_pos;
            ++_mouse_motion_generation;
            if (_drag_state.button_down)
            {
                _drag_state.current = _mouse_pos_window;
                const glm::vec2 delta = _drag_state.current - _drag_state.start;
                if (!_drag_state.dragging &&
                    (std::abs(delta.x) > click_threshold_px || std::abs(delta.y) > click_threshold_px))
                {
                    _drag_state.dragging = true;
                }
            }
            continue;
        }

        if (event.type == InputEvent::Type::MouseButtonDown && is_select_button(event.mouse_button))
        {
            if (!same_vec2(_mouse_pos_window, event.mouse_pos, 0.01f))
            {
                ++_mouse_motion_generation;
            }
            _mouse_pos_window = event.mouse_pos;

            if (!_settings.enabled)
            {
                continue;
            }

            if (_settings.require_cursor_normal &&
                (input.cursor_mode() != CursorMode::Normal || input.mouse_captured()))
            {
                continue;
            }

            if (_settings.respect_ui_capture_mouse && ui_want_capture_mouse)
            {
                continue;
            }

            if (_drag_state.button_down)
            {
                continue;
            }

            _drag_state.button_down = true;
            _drag_state.dragging = false;
            _drag_state.button = event.mouse_button;
            _drag_state.start = event.mouse_pos;
            _drag_state.current = _drag_state.start;
            continue;
        }

        if (event.type == InputEvent::Type::MouseButtonUp && _drag_state.button_down && event.mouse_button == _drag_state.button)
        {
            if (!same_vec2(_mouse_pos_window, event.mouse_pos, 0.01f))
            {
                ++_mouse_motion_generation;
            }
            _mouse_pos_window = event.mouse_pos;

            const bool was_down = _drag_state.button_down;
            _drag_state.button_down = false;
            if (!was_down)
            {
                _drag_state.dragging = false;
                continue;
            }

            const glm::vec2 release_pos = event.mouse_pos;
            const glm::vec2 delta = release_pos - _drag_state.start;
            const bool moved_enough_for_drag = (std::abs(delta.x) > click_threshold_px || std::abs(delta.y) > click_threshold_px);

            SceneManager *scene = _context->scene;

            if (!_settings.enabled ||
                (_settings.require_cursor_normal &&
                 (input.cursor_mode() != CursorMode::Normal || input.mouse_captured())) ||
                (_settings.respect_ui_capture_mouse && ui_want_capture_mouse))
            {
                _drag_state.dragging = false;
                continue;
            }

            const bool do_drag_select = _settings.enable_drag_select && moved_enough_for_drag;
            const bool do_click_select = _settings.enable_click_select && !do_drag_select;

            if (do_click_select)
            {
                // A single click replaces any prior multi-selection.
                _drag_selection.clear();
                const PickInfo previous_pick = _last_pick;
                PickInfo line_pick{};
                double line_depth_m = 0.0;
                bool line_hit =
                    _settings.enable_line_picking &&
                    pick_line_at_window_pos(release_pos, line_pick, line_depth_m);
                if (line_hit && !pick_allowed(line_pick, PickUse::Click))
                {
                    line_hit = false;
                }

                if (_use_id_buffer_picking)
                {
                    if (line_hit)
                    {
                        _last_pick = std::move(line_pick);
                        apply_click_selection_level(_last_pick, &previous_pick);
                        _last_pick_object_id = 0;
                        _pending_pick.active = false;
                        _pick_result_pending = false;
                    }
                    else
                    {
                        _pending_pick.active = true;
                        _pending_pick.window_pos_swapchain = window_to_swapchain_pixels(release_pos);
                    }
                }
                else if (scene)
                {
                    RenderObject hit_object{};
                    WorldVec3 hit_pos{};
                    const bool mesh_hit = scene->pick(window_to_swapchain_pixels(release_pos), hit_object, hit_pos);

                    if (mesh_hit)
                    {
                        PickInfo mesh_pick{};
                        set_pick_from_hit(hit_object, hit_pos, mesh_pick);
                        const bool mesh_pick_allowed = pick_allowed(mesh_pick, PickUse::Click);
                        const WorldVec3 cam_world = scene->getMainCamera().position_world;
                        double mesh_depth_m = glm::length(glm::dvec3(hit_pos - cam_world));
                        if (!std::isfinite(mesh_depth_m))
                        {
                            mesh_depth_m = std::numeric_limits<double>::infinity();
                        }

                        // Terrain patch AABBs can be very conservative at low LOD, causing false
                        // hits that incorrectly occlude orbit plot line picking. When the hit is a
                        // terrain planet, derive the occlusion depth from an analytic planet sphere
                        // (base radius + max terrain height). If the ray does not intersect the
                        // sphere, treat the mesh hit as non-occluding for line picks.
                        if (line_hit &&
                            hit_object.ownerType == RenderObject::OwnerType::MeshInstance &&
                            hit_object.sourceMesh == nullptr &&
                            hit_object.pickBVH == nullptr &&
                            !hit_object.ownerName.empty())
                        {
                            if (PlanetSystem *planets = scene->get_planet_system())
                            {
                                if (const PlanetSystem::PlanetBody *body = planets->find_body_by_name(hit_object.ownerName))
                                {
                                    if (body->terrain)
                                    {
                                        CameraRay ray{};
                                        if (compute_camera_ray(release_pos, ray))
                                        {
                                            const glm::dvec3 center_local =
                                                    glm::dvec3(body->center_world - ray.origin_world);
                                            const double r = std::max(0.0, body->radius_m);

                                            double t_sphere = 0.0;
                                            if (intersect_ray_sphere_depth(ray.origin_local, ray.dir_local, center_local, r, t_sphere))
                                            {
                                                mesh_depth_m = t_sphere;
                                            }
                                            else
                                            {
                                                mesh_depth_m = std::numeric_limits<double>::infinity();
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (line_hit && std::isfinite(line_depth_m) && line_depth_m < mesh_depth_m)
                        {
                            _last_pick = std::move(line_pick);
                            apply_click_selection_level(_last_pick, &previous_pick);
                            _last_pick_object_id = 0;
                        }
                        else if (mesh_pick_allowed)
                        {
                            _last_pick = std::move(mesh_pick);
                            apply_click_selection_level(_last_pick, &previous_pick);
                            _last_pick_object_id = hit_object.objectID;
                        }
                        else if (_settings.clear_last_pick_on_miss)
                        {
                            clear_pick(_last_pick);
                            _last_pick_object_id = 0;
                        }
                    }
                    else if (line_hit)
                    {
                        _last_pick = std::move(line_pick);
                        apply_click_selection_level(_last_pick, &previous_pick);
                        _last_pick_object_id = 0;
                    }
                    else if (_settings.clear_last_pick_on_miss)
                    {
                        clear_pick(_last_pick);
                        _last_pick_object_id = 0;
                    }
                }
            }
            else if (do_drag_select)
            {
                // A drag-select replaces the single primary pick with the set.
                clear_pick(_last_pick);
                _last_pick_object_id = 0;
                _drag_selection.clear();
                if (scene)
                {
                    std::vector<RenderObject> selected;
                    scene->selectRect(window_to_swapchain_pixels(_drag_state.start),
                                      window_to_swapchain_pixels(release_pos),
                                      selected);
                    _drag_selection.reserve(selected.size());
                    for (const RenderObject &obj : selected)
                    {
                        PickInfo info{};
                        glm::vec3 center_local = glm::vec3(obj.transform * glm::vec4(obj.bounds.origin, 1.0f));
                        set_pick_from_hit(obj, local_to_world(center_local, scene->get_world_origin()), info);
                        apply_drag_selection_level(info);
                        if (pick_allowed(info, PickUse::Click))
                        {
                            _drag_selection.push_back(std::move(info));
                        }
                    }
                }
            }

            _drag_state.dragging = false;
        }
    }

    // Safety: avoid stuck drag state if focus/SDL event is missed.
    if (_drag_state.button_down && !input.state().mouse_down(_drag_state.button))
    {
        _drag_state = {};
    }
}

void PickingSystem::update_hover(bool ui_want_capture_mouse)
{
    ++_hover_frame_counter;

    if (_context == nullptr || _context->scene == nullptr)
    {
        _hover_miss_cache = {};
        _last_hover_update_frame = 0;
        _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
        return;
    }

    if (!_settings.enabled || !_settings.enable_hover)
    {
        clear_pick(_hover_pick);
        _hover_miss_cache = {};
        _last_hover_update_frame = 0;
        _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
        return;
    }

    if (_manual_hover_pick)
    {
        _manual_hover_pick = false;
        _hover_miss_cache = {};
        _last_hover_update_frame = _hover_frame_counter;
        _last_hover_mouse_motion_generation = _mouse_motion_generation;
        return;
    }

    if (_settings.respect_ui_capture_mouse && ui_want_capture_mouse)
    {
        clear_pick(_hover_pick);
        _hover_miss_cache = {};
        _last_hover_update_frame = 0;
        _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
        return;
    }

    if (_settings.require_cursor_normal && _context->input &&
        (_context->input->cursor_mode() != CursorMode::Normal || _context->input->mouse_captured()))
    {
        clear_pick(_hover_pick);
        _hover_miss_cache = {};
        _last_hover_update_frame = 0;
        _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
        return;
    }

    if (_mouse_pos_window.x < 0.0f || _mouse_pos_window.y < 0.0f)
    {
        clear_pick(_hover_pick);
        _hover_miss_cache = {};
        _last_hover_update_frame = 0;
        _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
        return;
    }

    const uint32_t stationary_hover_interval = _settings.stationary_hover_update_interval_frames;
    const bool cursor_stationary_since_last_hover =
        _last_hover_mouse_motion_generation == _mouse_motion_generation;
    if (stationary_hover_interval > 0 &&
        cursor_stationary_since_last_hover &&
        _last_hover_update_frame != 0 &&
        (_hover_frame_counter - _last_hover_update_frame) < stationary_hover_interval)
    {
        return;
    }

    const Camera &camera = _context->scene->getMainCamera();
    const WorldVec3 origin_world = _context->scene->get_world_origin();
    VkExtent2D logical_extent = _context->getLogicalRenderExtent();
    VkExtent2D swapchain_extent{0, 0};
    if (SwapchainManager *swapchain = _context->getSwapchain())
    {
        swapchain_extent = swapchain->swapchainExtent();
    }

    const bool line_hover_enabled = _settings.enable_line_picking && _settings.enable_line_hover;
    const uint32_t max_stationary_miss_skips = _settings.stationary_hover_miss_skip_frames;
    if (max_stationary_miss_skips > 0 &&
        _hover_miss_cache.skipped_frames < max_stationary_miss_skips &&
        hover_miss_cache_matches(_hover_miss_cache,
                                 _mouse_pos_window,
                                 camera.position_world,
                                 camera.orientation,
                                 camera.fovDegrees,
                                 origin_world,
                                 logical_extent,
                                 swapchain_extent,
                                 line_hover_enabled))
    {
        ++_hover_miss_cache.skipped_frames;
        _last_hover_update_frame = _hover_frame_counter;
        _last_hover_mouse_motion_generation = _mouse_motion_generation;
        return;
    }

    RenderObject hover_obj{};
    WorldVec3 hover_pos{};
    SceneManager::PickOptions hover_options{};
    if (_settings.use_fast_hover_mesh_bvh)
    {
        hover_options.preciseMeshBVH = false;
        hover_options.meshBVHMaxDepth = _settings.fast_hover_mesh_bvh_max_depth;
    }
    const bool mesh_hit = _context->scene->pick(window_to_swapchain_pixels(_mouse_pos_window),
                                               hover_obj,
                                               hover_pos,
                                               hover_options);

    if (mesh_hit)
    {
        set_pick_from_hit(hover_obj, hover_pos, _hover_pick);
        apply_hover_selection_level(_hover_pick);
        if (!pick_allowed(_hover_pick, PickUse::Hover))
        {
            clear_pick(_hover_pick);
        }
    }

    PickInfo line_pick{};
    double line_depth_m = 0.0;
    bool line_hit =
        _settings.enable_line_picking &&
        _settings.enable_line_hover &&
        pick_line_at_window_pos(_mouse_pos_window, line_pick, line_depth_m);
    if (line_hit && !pick_allowed(line_pick, PickUse::Hover))
    {
        line_hit = false;
    }

    if (line_hit)
    {
        if (!mesh_hit)
        {
            _hover_pick = std::move(line_pick);
            apply_hover_selection_level(_hover_pick);
        }
        else
        {
            const WorldVec3 cam_world = _context->scene->getMainCamera().position_world;
            double mesh_depth_m = glm::length(glm::dvec3(hover_pos - cam_world));
            if (!std::isfinite(mesh_depth_m))
            {
                mesh_depth_m = std::numeric_limits<double>::infinity();
            }

            if (hover_obj.ownerType == RenderObject::OwnerType::MeshInstance &&
                hover_obj.sourceMesh == nullptr &&
                hover_obj.pickBVH == nullptr &&
                !hover_obj.ownerName.empty())
            {
                if (PlanetSystem *planets = _context->scene->get_planet_system())
                {
                    if (const PlanetSystem::PlanetBody *body = planets->find_body_by_name(hover_obj.ownerName))
                    {
                        if (body->terrain)
                        {
                            CameraRay ray{};
                            if (compute_camera_ray(_mouse_pos_window, ray))
                            {
                                const glm::dvec3 center_local = glm::dvec3(body->center_world - ray.origin_world);
                                const double r = std::max(0.0, body->radius_m);

                                double t_sphere = 0.0;
                                if (intersect_ray_sphere_depth(ray.origin_local, ray.dir_local, center_local, r, t_sphere))
                                {
                                    mesh_depth_m = t_sphere;
                                }
                                else
                                {
                                    mesh_depth_m = std::numeric_limits<double>::infinity();
                                }
                            }
                        }
                    }
                }
            }

            if (std::isfinite(line_depth_m) && line_depth_m < mesh_depth_m)
            {
                _hover_pick = std::move(line_pick);
                apply_hover_selection_level(_hover_pick);
            }
        }
    }
    else if (!mesh_hit)
    {
        clear_pick(_hover_pick);
    }

    _last_hover_update_frame = _hover_frame_counter;
    _last_hover_mouse_motion_generation = _mouse_motion_generation;

    if (_hover_pick.valid)
    {
        _hover_miss_cache = {};
    }
    else
    {
        _hover_miss_cache.valid = true;
        _hover_miss_cache.skipped_frames = 0;
        _hover_miss_cache.mouse_pos_window = _mouse_pos_window;
        _hover_miss_cache.camera_world = camera.position_world;
        _hover_miss_cache.camera_orientation = camera.orientation;
        _hover_miss_cache.camera_fov = camera.fovDegrees;
        _hover_miss_cache.origin_world = origin_world;
        _hover_miss_cache.logical_extent = logical_extent;
        _hover_miss_cache.swapchain_extent = swapchain_extent;
        _hover_miss_cache.line_hover_enabled = line_hover_enabled;
    }
}

void PickingSystem::set_manual_hover_pick(const PickInfo &pick)
{
    _hover_pick = pick;
    _manual_hover_pick = pick.valid;
    _hover_miss_cache = {};
}

void PickingSystem::clear_manual_hover_pick()
{
    _manual_hover_pick = false;
}
