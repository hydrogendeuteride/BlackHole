#pragma once

#include "render/passes/atmosphere.h"

#include "core/assets/ktx_loader.h"
#include "core/assets/manager.h"
#include "core/assets/texture_cache.h"
#include "core/context.h"
#include "core/descriptor/descriptors.h"
#include "core/descriptor/manager.h"
#include "core/device/device.h"
#include "core/device/resource.h"
#include "core/device/swapchain.h"
#include "core/frame/resources.h"
#include "core/math/finite.h"
#include "core/pipeline/manager.h"
#include "core/pipeline/sampler.h"
#include "core/world.h"

#include "render/graph/graph.h"
#include "render/pipelines.h"

#include "scene/planet/planet_system.h"
#include "scene/vk_scene.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>

#include <glm/gtc/packing.hpp>

namespace atmosphere::detail
{
    constexpr uint32_t k_transmittance_lut_width = 256;
    constexpr uint32_t k_transmittance_lut_height = 64;
    constexpr uint32_t k_sky_view_lut_width = 256;
    constexpr uint32_t k_sky_view_lut_height = 144;
    constexpr uint32_t k_aerial_lut_res = 64;
    constexpr uint32_t k_aerial_lut_slices = 32;
    constexpr uint32_t k_misc_flags_mask = 0xFFu;
    constexpr uint32_t k_misc_noise_blend_shift = 8u;
    constexpr uint32_t k_misc_detail_erode_shift = 16u;
    constexpr uint32_t k_misc_jitter_frame_shift = 24u;
    constexpr uint32_t k_flag_cloud_noise_3d = 8u;
    constexpr uint32_t k_flag_jitter_blue_noise = 16u;

    struct AtmosphereBodySelection
    {
        const PlanetSystem::PlanetBody *body = nullptr;
        glm::vec3 center_local{0.0f};
        float radius_m = 0.0f;
    };

    struct AtmospherePush
    {
        glm::vec4 planet_center_radius;
        glm::vec4 atmosphere_params;
        glm::vec4 beta_rayleigh;
        glm::vec4 beta_mie;
        glm::vec4 jitter_params;
        glm::vec4 terrain_params; // xyz: terrain/ocean, w: cloud type bias
        glm::vec4 cloud_layer;
        glm::vec4 cloud_params;
        glm::vec4 cloud_animation; // xy: evolution periods, z: resolution-aware step scale
        glm::vec4 cloud_optics;   // rgb: lighting tint, w: extinction (1/m)
        glm::ivec4 misc;
    };

    struct AtmosphereLutPush
    {
        glm::vec4 radii_heights;
        glm::ivec4 misc;
    };

    struct AtmosphereSkyViewPush
    {
        glm::vec4 planet_center_radius;
        glm::vec4 atmosphere_params;
        glm::vec4 beta_rayleigh;
        glm::vec4 beta_mie;
        glm::vec4 absorption_color_steps;
        glm::vec4 camera_local;
        glm::vec4 sun_dir;
    };

    struct AtmosphereAerialPush
    {
        glm::vec4 planet_center_radius;
        glm::vec4 atmosphere_params;
        glm::vec4 beta_rayleigh;
        glm::vec4 beta_mie;
        glm::vec4 absorption_color_steps;
        glm::vec4 camera_local;
        glm::vec4 sun_dir;
        glm::vec4 ray_x;
        glm::vec4 ray_y;
        glm::vec4 ray_z;
    };

    struct CloudTemporalPush
    {
        glm::mat4 previous_view_proj;
        glm::vec4 origin_delta_blend;
        glm::vec4 viewport_params;
        glm::vec4 planet_center_radius;
        glm::vec4 cloud_motion; // x: wind speed, y: wind angle
        glm::ivec4 misc;
    };

    static_assert(sizeof(AtmospherePush) == 176);
    static_assert(sizeof(AtmosphereSkyViewPush) == 112);
    static_assert(sizeof(AtmosphereAerialPush) == 160);
    static_assert(sizeof(CloudTemporalPush) == 144);

    static uint32_t cloud_resolution_div(int div)
    {
        if (div >= 4) return 4u;
        if (div >= 2) return 2u;
        return 1u;
    }

    static VkExtent2D scaled_extent(VkExtent2D extent, uint32_t div)
    {
        extent.width = std::max(1u, (extent.width + div - 1u) / div);
        extent.height = std::max(1u, (extent.height + div - 1u) / div);
        return extent;
    }

    static bool body_has_ocean(const PlanetSystem::PlanetBody *body)
    {
        return body && body->terrain && !body->terrain_specular_dir.empty() && body->specular_strength > 0.0f;
    }

    static float ocean_shell_offset_m(double radius_m)
    {
        const double scaled = radius_m * 1.0e-6;
        return static_cast<float>(std::max(2.0, scaled));
    }

    static float body_ocean_shell_offset_m(const PlanetSystem::PlanetBody *body)
    {
        return body_has_ocean(body) ? ocean_shell_offset_m(body->radius_m) : 0.0f;
    }

    static bool nearly_equal(float a, float b, float eps = 1.0e-3f)
    {
        return std::abs(a - b) <= eps;
    }

    static bool nearly_equal_vec3(const glm::vec3 &a, const glm::vec3 &b, float eps = 1.0e-3f)
    {
        return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps) &&
               nearly_equal(a.z, b.z, eps);
    }

    static bool nearly_equal_vec4(const glm::vec4 &a, const glm::vec4 &b, float eps = 1.0e-3f)
    {
        return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps) &&
               nearly_equal(a.z, b.z, eps) && nearly_equal(a.w, b.w, eps);
    }

    static bool nearly_equal_mat4(const glm::mat4 &a, const glm::mat4 &b, float eps = 1.0e-5f)
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (!nearly_equal(a[c][r], b[c][r], eps))
                {
                    return false;
                }
            }
        }
        return true;
    }

    static void set_fullscreen_viewport(VkCommandBuffer cmd, VkExtent2D extent)
    {
        VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    static std::string resolve_optional_face_texture_path(AssetManager &assets,
                                                          std::string_view dir,
                                                          planet::CubeFace face)
    {
        if (dir.empty())
        {
            return {};
        }

        std::string rel = std::string(dir) + "/" + planet::cube_face_name(face) + ".ktx2";
        std::string abs_path = assets.assetPath(rel);
        if (std::filesystem::exists(abs_path))
        {
            return abs_path;
        }

        rel = std::string(dir) + "/" + planet::cube_face_name(face) + ".png";
        abs_path = assets.assetPath(rel);
        if (std::filesystem::exists(abs_path))
        {
            return abs_path;
        }

        return {};
    }

    static bool find_atmosphere_body(const EngineContext &ctx,
                                     const SceneManager &scene,
                                     const PlanetSystem &planets,
                                     AtmosphereBodySelection &out_selection)
    {
        out_selection = {};

        const auto &bodies = planets.bodies();
        if (bodies.empty())
        {
            return false;
        }

        const std::string &want = ctx.atmosphere.bodyName;
        const PlanetSystem::PlanetBody *picked = nullptr;

        auto body_ok = [](const PlanetSystem::PlanetBody &body) {
            return body.visible && body.radius_m > 0.0;
        };

        if (!want.empty())
        {
            for (const PlanetSystem::PlanetBody &body : bodies)
            {
                if (body.name == want && body_ok(body))
                {
                    picked = &body;
                    break;
                }
            }
        }

        if (!picked)
        {
            const WorldVec3 cam_world = scene.getMainCamera().position_world;
            double best_d2 = 0.0;
            for (const PlanetSystem::PlanetBody &body : bodies)
            {
                if (!body_ok(body))
                {
                    continue;
                }

                const WorldVec3 delta_world = cam_world - body.center_world;
                const double dist2 = glm::dot(delta_world, delta_world);
                if (!picked || dist2 < best_d2)
                {
                    picked = &body;
                    best_d2 = dist2;
                }
            }
        }

        if (!picked)
        {
            return false;
        }

        out_selection.body = picked;
        out_selection.center_local = world_to_local(picked->center_world, scene.get_world_origin());
        out_selection.radius_m = static_cast<float>(picked->radius_m);
        return std::isfinite(out_selection.radius_m) && out_selection.radius_m > 0.0f;
    }

    static AtmospherePush build_atmosphere_push(const EngineContext &ctx,
                                                const AtmosphereBodySelection &body_selection,
                                                bool jitter_noise_ready,
                                                bool cloud_overlay_ready,
                                                bool cloud_noise_ready,
                                                bool cloud_noise_3d_ready)
    {
        const AtmosphereSettings &s = ctx.atmosphere;
        const PlanetCloudSettings &c = ctx.planetClouds;
        const PlanetSystem::PlanetBody *body = body_selection.body;
        const glm::vec3 &planet_center_local = body_selection.center_local;
        const float planet_radius_m = body_selection.radius_m;

        const bool atmosphere_enabled = ctx.enableAtmosphere;
        const bool clouds_enabled = ctx.enablePlanetClouds && cloud_overlay_ready;
        const bool cloud_noise_available = cloud_noise_ready;
        const bool cloud_detail_noise_available = cloud_noise_ready || cloud_noise_3d_ready;

        const float atm_height = std::max(0.0f, s.atmosphereHeightM);
        const float atm_radius = (atmosphere_enabled && planet_radius_m > 0.0f && atm_height > 0.0f)
            ? (planet_radius_m + atm_height)
            : 0.0f;
        const float rayleigh_height = std::max(1.0f, s.rayleighScaleHeightM);
        const float mie_height = std::max(1.0f, s.mieScaleHeightM);

        const glm::vec3 beta_rayleigh = atmosphere_enabled && MathUtil::finite(s.rayleighScattering)
            ? glm::max(s.rayleighScattering, glm::vec3(0.0f))
            : glm::vec3(0.0f);
        const glm::vec3 beta_mie = atmosphere_enabled && MathUtil::finite(s.mieScattering)
            ? glm::max(s.mieScattering, glm::vec3(0.0f))
            : glm::vec3(0.0f);

        const float mie_g = std::clamp(s.mieG, -0.99f, 0.99f);
        const float intensity = atmosphere_enabled ? std::max(0.0f, s.intensity) : 0.0f;
        const float jitter_strength = std::clamp(s.jitterStrength, 0.0f, 1.0f);
        const float planet_snap_m = std::max(0.0f, s.planetSurfaceSnapM);
        const bool terrain_height_enabled = body && body->terrain &&
            (!body->terrain_height_dir.empty()) &&
            (body->terrain_height_max_m > 0.0 || body->terrain_height_offset_m > 0.0);
        const float terrain_height_scale_m = terrain_height_enabled
            ? static_cast<float>(std::max(0.0, body->terrain_height_max_m))
            : 0.0f;
        const float terrain_height_offset_m = terrain_height_enabled
            ? static_cast<float>(std::max(0.0, body->terrain_height_offset_m))
            : 0.0f;
        const float ocean_shell_offset = body_ocean_shell_offset_m(body);
        const int view_steps = std::clamp(s.viewSteps, 4, 64);

        const float cloud_base_m = std::max(0.0f, c.baseHeightM);
        const float cloud_thickness_m = std::max(0.0f, c.thicknessM);
        const float cloud_density_scale = std::max(0.0f, c.densityScale);
        const float cloud_extinction = std::max(1.0e-6f, c.extinction);
        const glm::vec3 cloud_tint = MathUtil::finite(c.color)
            ? glm::clamp(c.color, glm::vec3(0.0f), glm::vec3(1.0f))
            : glm::vec3(1.0f);
        const float cloud_coverage = std::clamp(c.coverage, 0.0f, 0.999f);
        const float cloud_type_bias = std::clamp(c.typeBias, 0.0f, 1.0f);
        const float cloud_overlay_rot = clouds_enabled ? c.overlayRotationRad : 0.0f;
        const float cloud_overlay_sin = std::sin(cloud_overlay_rot);
        const float cloud_overlay_cos = std::cos(cloud_overlay_rot);
        const float cloud_noise_scale = std::max(0.001f, c.noiseScale);
        const float cloud_detail_scale = std::max(0.001f, c.detailScale);
        const float cloud_noise_blend = (clouds_enabled && cloud_noise_available) ? std::clamp(c.noiseBlend, 0.0f, 1.0f) : 0.0f;
        const float cloud_detail_erode = (clouds_enabled && cloud_detail_noise_available) ? std::clamp(c.detailErode, 0.0f, 1.0f) : 0.0f;
        const float cloud_weather_evolution_s = std::max(0.0f, c.weatherEvolutionPeriodS);
        const float cloud_detail_evolution_s = std::max(0.0f, c.detailEvolutionPeriodS);
        const float cloud_step_scale = c.resolutionDiv <= 1 ? 1.0f : (c.resolutionDiv <= 2 ? 0.65f : 0.5f);
        const float cloud_wind_speed = c.windSpeed;
        const float cloud_wind_angle = c.windAngleRad;
        const int cloud_steps = std::clamp(c.cloudSteps, 4, 128);

        uint32_t flags = 0u;
        if (atmosphere_enabled)
        {
            flags |= 1u;
        }
        if (clouds_enabled)
        {
            flags |= 2u;
        }
        if (clouds_enabled && c.overlayFlipV)
        {
            flags |= 4u;
        }
        if (clouds_enabled && cloud_noise_3d_ready)
        {
            flags |= k_flag_cloud_noise_3d;
        }
        if (jitter_noise_ready)
        {
            flags |= k_flag_jitter_blue_noise;
        }

        const glm::vec3 absorption_color = MathUtil::finite(s.absorptionColor)
            ? glm::clamp(s.absorptionColor, glm::vec3(0.0f), glm::vec3(1.0f))
            : glm::vec3(1.0f);
        const float absorption_strength = (atmosphere_enabled && std::isfinite(s.absorptionStrength))
            ? std::max(0.0f, s.absorptionStrength)
            : 0.0f;

        const uint32_t packed_absorption_color = glm::packUnorm4x8(glm::vec4(absorption_color, 1.0f));
        const int packed_absorption_color_bits = std::bit_cast<int32_t>(packed_absorption_color);
        const uint32_t packed_noise_blend = static_cast<uint32_t>(std::lround(cloud_noise_blend * 255.0f));
        const uint32_t packed_detail_erode = static_cast<uint32_t>(std::lround(cloud_detail_erode * 255.0f));
        const uint32_t packed_jitter_frame = ctx.frameIndex & 0xFFu;
        uint32_t packed_misc_w = (flags & k_misc_flags_mask);
        packed_misc_w |= ((packed_noise_blend & 0xFFu) << k_misc_noise_blend_shift);
        packed_misc_w |= ((packed_detail_erode & 0xFFu) << k_misc_detail_erode_shift);
        packed_misc_w |= ((packed_jitter_frame & 0xFFu) << k_misc_jitter_frame_shift);

        AtmospherePush push{};
        push.planet_center_radius = glm::vec4(planet_center_local, planet_radius_m);
        push.atmosphere_params = glm::vec4(atm_radius, rayleigh_height, mie_height, mie_g);
        push.beta_rayleigh = glm::vec4(beta_rayleigh, intensity);
        push.beta_mie = glm::vec4(beta_mie, absorption_strength);
        push.jitter_params = glm::vec4(jitter_strength, planet_snap_m, cloud_overlay_sin, cloud_overlay_cos);
        push.terrain_params = glm::vec4(terrain_height_scale_m, terrain_height_offset_m, ocean_shell_offset, cloud_type_bias);
        push.cloud_layer = glm::vec4(cloud_base_m, cloud_thickness_m, cloud_density_scale, cloud_coverage);
        push.cloud_params = glm::vec4(cloud_noise_scale, cloud_detail_scale, cloud_wind_speed, cloud_wind_angle);
        push.cloud_animation = glm::vec4(cloud_weather_evolution_s, cloud_detail_evolution_s, cloud_step_scale, 0.0f);
        push.cloud_optics = glm::vec4(cloud_tint, cloud_extinction);
        push.misc = glm::ivec4(view_steps, packed_absorption_color_bits, cloud_steps, std::bit_cast<int32_t>(packed_misc_w));
        return push;
    }

    static AtmosphereSkyViewPush build_sky_view_push(const EngineContext &ctx,
                                                     const AtmosphereBodySelection &body_selection)
    {
        const AtmosphereSettings &s = ctx.atmosphere;
        const glm::vec3 &planet_center_local = body_selection.center_local;
        const float planet_radius_m = body_selection.radius_m;

        const float atm_height = std::max(0.0f, s.atmosphereHeightM);
        const float atm_radius = (ctx.enableAtmosphere && planet_radius_m > 0.0f && atm_height > 0.0f)
            ? (planet_radius_m + atm_height)
            : 0.0f;
        const float rayleigh_height = std::max(1.0f, s.rayleighScaleHeightM);
        const float mie_height = std::max(1.0f, s.mieScaleHeightM);
        const glm::vec3 beta_rayleigh = (ctx.enableAtmosphere && MathUtil::finite(s.rayleighScattering))
            ? glm::max(s.rayleighScattering, glm::vec3(0.0f))
            : glm::vec3(0.0f);
        const glm::vec3 beta_mie = (ctx.enableAtmosphere && MathUtil::finite(s.mieScattering))
            ? glm::max(s.mieScattering, glm::vec3(0.0f))
            : glm::vec3(0.0f);
        const glm::vec3 absorption_color = MathUtil::finite(s.absorptionColor)
            ? glm::clamp(s.absorptionColor, glm::vec3(0.0f), glm::vec3(1.0f))
            : glm::vec3(1.0f);

        AtmosphereSkyViewPush push{};
        push.planet_center_radius = glm::vec4(planet_center_local, planet_radius_m);
        push.atmosphere_params = glm::vec4(atm_radius, rayleigh_height, mie_height, std::clamp(s.mieG, -0.99f, 0.99f));
        push.beta_rayleigh = glm::vec4(beta_rayleigh, std::max(0.0f, s.intensity));
        push.beta_mie = glm::vec4(beta_mie, std::max(0.0f, s.absorptionStrength));
        push.absorption_color_steps = glm::vec4(absorption_color, static_cast<float>(std::clamp(s.viewSteps, 4, 64)));
        push.camera_local = glm::vec4(ctx.scene ? ctx.scene->get_camera_local_position() : glm::vec3(0.0f), 0.0f);
        push.sun_dir = glm::vec4(-ctx.getSceneData().sunlightDirection.x,
                                 -ctx.getSceneData().sunlightDirection.y,
                                 -ctx.getSceneData().sunlightDirection.z,
                                 0.0f);
        return push;
    }

    static AtmosphereAerialPush build_aerial_push(const EngineContext &ctx,
                                                  const AtmosphereBodySelection &body_selection)
    {
        const AtmosphereSkyViewPush sky = build_sky_view_push(ctx, body_selection);

        AtmosphereAerialPush push{};
        push.planet_center_radius = sky.planet_center_radius;
        push.atmosphere_params = sky.atmosphere_params;
        push.beta_rayleigh = sky.beta_rayleigh;
        push.beta_mie = sky.beta_mie;
        push.absorption_color_steps = sky.absorption_color_steps;
        push.camera_local = sky.camera_local;
        push.sun_dir = sky.sun_dir;

        // Sub-steps per froxel slice; total samples per column = slices * sub-steps.
        const int view_steps = std::clamp(ctx.atmosphere.viewSteps, 4, 64);
        push.absorption_color_steps.w = static_cast<float>(std::clamp(view_steps / 16, 1, 4));

        // Ray basis matching atmosphere.vert: worldRay = view^T * (ndc.x/p00, ndc.y/p11, -1).
        const GPUSceneData &scene_data = ctx.getSceneData();
        const glm::mat3 view_to_world = glm::transpose(glm::mat3(scene_data.view));
        const float proj_x = scene_data.proj[0][0];
        const float proj_y = scene_data.proj[1][1];
        const float inv_proj_x = (std::abs(proj_x) > 1.0e-6f) ? (1.0f / proj_x) : 0.0f;
        const float inv_proj_y = (std::abs(proj_y) > 1.0e-6f) ? (1.0f / proj_y) : 0.0f;
        push.ray_x = glm::vec4(view_to_world[0] * inv_proj_x, 0.0f);
        push.ray_y = glm::vec4(view_to_world[1] * inv_proj_y, 0.0f);
        push.ray_z = glm::vec4(-view_to_world[2], 0.0f);
        return push;
    }
} // namespace atmosphere::detail
