#include "core/game_api.h"
#include "core/engine.h"
#include "core/context.h"
#include "core/pipeline/manager.h"
#include "render/renderpass.h"
#include "render/passes/tonemap.h"
#include "render/passes/fxaa.h"

#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace GameAPI
{
namespace
{
    SDL_Rect display_usable_bounds(int display_index)
    {
        SDL_Rect bounds{0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(display_index, &bounds) != 0 || bounds.w <= 0 || bounds.h <= 0)
        {
            if (SDL_GetDisplayBounds(display_index, &bounds) != 0)
            {
                bounds = SDL_Rect{0, 0, 1920, 1080};
            }
        }
        return bounds;
    }

    struct DisplayPixelMode
    {
        SDL_DisplayMode mode{};
        float scale_x{1.0f};
        float scale_y{1.0f};
        bool scale_modes{false};
    };

    DisplayPixelMode display_pixel_mode(int display_index, SDL_Window *window)
    {
        DisplayPixelMode result{};
        SDL_GetDesktopDisplayMode(display_index, &result.mode);

        const char *video_driver = SDL_GetCurrentVideoDriver();
        if (video_driver && SDL_strcmp(video_driver, "wayland") == 0)
        {
            const int logical_width = result.mode.w;
            const int logical_height = result.mode.h;

            // Wayland desktop modes use compositor-logical coordinates. SDL exposes
            // the current native pixel mode first, followed by smaller emulated modes.
            bool has_native_mode = false;
            SDL_DisplayMode native_mode{};
            if (SDL_GetDisplayMode(display_index, 0, &native_mode) == 0 &&
                native_mode.w >= result.mode.w && native_mode.h >= result.mode.h)
            {
                has_native_mode = native_mode.w > result.mode.w || native_mode.h > result.mode.h;
                result.mode = native_mode;
                if (has_native_mode && logical_width > 0 && logical_height > 0)
                {
                    result.scale_x = static_cast<float>(native_mode.w) / static_cast<float>(logical_width);
                    result.scale_y = static_cast<float>(native_mode.h) / static_cast<float>(logical_height);
                }
            }

            // Some Wayland compositors expose only the scaled desktop mode.
            // Recover the pixel size for the window's current display from the
            // Vulkan drawable-to-logical size ratio.
            if (!has_native_mode && window && SDL_GetWindowDisplayIndex(window) == display_index)
            {
                int window_width = 0;
                int window_height = 0;
                int drawable_width = 0;
                int drawable_height = 0;
                SDL_GetWindowSize(window, &window_width, &window_height);
                SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);

                if (window_width > 0 && window_height > 0 &&
                    drawable_width > 0 && drawable_height > 0)
                {
                    const float scale_x = static_cast<float>(drawable_width) / static_cast<float>(window_width);
                    const float scale_y = static_cast<float>(drawable_height) / static_cast<float>(window_height);
                    if (std::isfinite(scale_x) && std::isfinite(scale_y) &&
                        scale_x > 1.0f && scale_y > 1.0f)
                    {
                        result.scale_x = scale_x;
                        result.scale_y = scale_y;
                        result.scale_modes = true;
                        result.mode.w = std::max(result.mode.w,
                                                 static_cast<int>(std::lround(result.mode.w * scale_x)));
                        result.mode.h = std::max(result.mode.h,
                                                 static_cast<int>(std::lround(result.mode.h * scale_y)));
                    }
                }
            }
        }

        return result;
    }

    struct WindowFrameMargins
    {
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};
    };

    WindowFrameMargins window_frame_margins(SDL_Window *window)
    {
        WindowFrameMargins margins{};
        if (!window)
        {
            return margins;
        }

        int top = 0;
        int left = 0;
        int bottom = 0;
        int right = 0;
        if (SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) == 0)
        {
            margins.left = std::max(0, left);
            margins.top = std::max(0, top);
            margins.right = std::max(0, right);
            margins.bottom = std::max(0, bottom);
        }
        return margins;
    }

    void clamp_window_rect_to_display(SDL_Window *window, int display_index, int &x, int &y, int &w, int &h)
    {
        w = std::max(1, w);
        h = std::max(1, h);

        const SDL_Rect bounds = display_usable_bounds(display_index);
        if (bounds.w <= 0 || bounds.h <= 0)
        {
            return;
        }

        const WindowFrameMargins frame = window_frame_margins(window);
        w = std::min(w, std::max(1, bounds.w - frame.left - frame.right));
        h = std::min(h, std::max(1, bounds.h - frame.top - frame.bottom));

        const int min_x = bounds.x + frame.left;
        const int min_y = bounds.y + frame.top;
        const int max_x = bounds.x + bounds.w - w - frame.right;
        const int max_y = bounds.y + bounds.h - h - frame.bottom;
        x = std::clamp(x, min_x, std::max(min_x, max_x));
        y = std::clamp(y, min_y, std::max(min_y, max_y));
    }

    VulkanEngine::WindowMode to_engine_window_mode(WindowMode mode)
    {
        switch (mode)
        {
            case WindowMode::FullscreenDesktop: return VulkanEngine::WindowMode::FullscreenDesktop;
            case WindowMode::FullscreenExclusive: return VulkanEngine::WindowMode::FullscreenExclusive;
            case WindowMode::Windowed:
            default: return VulkanEngine::WindowMode::Windowed;
        }
    }

    WindowMode from_engine_window_mode(VulkanEngine::WindowMode mode)
    {
        switch (mode)
        {
            case VulkanEngine::WindowMode::FullscreenDesktop: return WindowMode::FullscreenDesktop;
            case VulkanEngine::WindowMode::FullscreenExclusive: return WindowMode::FullscreenExclusive;
            case VulkanEngine::WindowMode::Windowed:
            default: return WindowMode::Windowed;
        }
    }
}

// ----------------------------------------------------------------------------
// Post Processing - FXAA
// ----------------------------------------------------------------------------

void Engine::set_fxaa_enabled(bool enabled)
{
    if (!_engine->_renderPassManager) return;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        fxaa->set_enabled(enabled);
    }
}

bool Engine::get_fxaa_enabled() const
{
    if (!_engine->_renderPassManager) return false;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        return fxaa->enabled();
    }
    return false;
}

void Engine::set_fxaa_edge_threshold(float threshold)
{
    if (!_engine->_renderPassManager) return;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        fxaa->set_edge_threshold(threshold);
    }
}

float Engine::get_fxaa_edge_threshold() const
{
    if (!_engine->_renderPassManager) return 0.125f;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        return fxaa->edge_threshold();
    }
    return 0.125f;
}

void Engine::set_fxaa_edge_threshold_min(float threshold)
{
    if (!_engine->_renderPassManager) return;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        fxaa->set_edge_threshold_min(threshold);
    }
}

float Engine::get_fxaa_edge_threshold_min() const
{
    if (!_engine->_renderPassManager) return 0.0312f;
    if (auto* fxaa = _engine->_renderPassManager->getPass<FxaaPass>())
    {
        return fxaa->edge_threshold_min();
    }
    return 0.0312f;
}

// ----------------------------------------------------------------------------
// Post Processing - SSR
// ----------------------------------------------------------------------------

void Engine::set_ssr_enabled(bool enabled)
{
    if (_engine->_context)
    {
        _engine->_context->enableSSR = enabled;
    }
}

bool Engine::get_ssr_enabled() const
{
    return _engine->_context ? _engine->_context->enableSSR : false;
}

void Engine::set_reflection_mode(ReflectionMode mode)
{
    if (_engine->_context)
    {
        // Guard against requesting RT reflection modes on unsupported hardware.
        if (mode != ReflectionMode::SSROnly)
        {
            if (!_engine->_deviceManager
                || !_engine->_deviceManager->supportsRayQuery()
                || !_engine->_deviceManager->supportsAccelerationStructure())
            {
                mode = ReflectionMode::SSROnly;
            }
        }

        _engine->_context->reflectionMode = static_cast<uint32_t>(mode);
    }
}

ReflectionMode Engine::get_reflection_mode() const
{
    if (!_engine->_context) return ReflectionMode::SSROnly;
    return static_cast<ReflectionMode>(_engine->_context->reflectionMode);
}

// ----------------------------------------------------------------------------
// Post Processing - Tonemapping
// ----------------------------------------------------------------------------

void Engine::set_exposure(float exposure)
{
    if (!_engine->_renderPassManager) return;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        tonemap->setExposure(exposure);
    }
}

float Engine::get_exposure() const
{
    if (!_engine->_renderPassManager) return 1.0f;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        return tonemap->exposure();
    }
    return 1.0f;
}

void Engine::set_tonemap_operator(TonemapOperator op)
{
    if (!_engine->_renderPassManager) return;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        tonemap->setMode(static_cast<int>(op));
    }
}

TonemapOperator Engine::get_tonemap_operator() const
{
    if (!_engine->_renderPassManager) return TonemapOperator::ACES;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        return static_cast<TonemapOperator>(tonemap->mode());
    }
    return TonemapOperator::ACES;
}

// ----------------------------------------------------------------------------
// Post Processing - Bloom
// ----------------------------------------------------------------------------

void Engine::set_bloom_enabled(bool enabled)
{
    if (!_engine->_renderPassManager) return;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        tonemap->setBloomEnabled(enabled);
    }
}

bool Engine::get_bloom_enabled() const
{
    if (!_engine->_renderPassManager) return false;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        return tonemap->bloomEnabled();
    }
    return false;
}

void Engine::set_bloom_threshold(float threshold)
{
    if (!_engine->_renderPassManager) return;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        tonemap->setBloomThreshold(threshold);
    }
}

float Engine::get_bloom_threshold() const
{
    if (!_engine->_renderPassManager) return 1.0f;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        return tonemap->bloomThreshold();
    }
    return 1.0f;
}

void Engine::set_bloom_intensity(float intensity)
{
    if (!_engine->_renderPassManager) return;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        tonemap->setBloomIntensity(intensity);
    }
}

float Engine::get_bloom_intensity() const
{
    if (!_engine->_renderPassManager) return 0.7f;
    if (auto* tonemap = _engine->_renderPassManager->getPass<TonemapPass>())
    {
        return tonemap->bloomIntensity();
    }
    return 0.7f;
}

// ----------------------------------------------------------------------------
// Rendering
// ----------------------------------------------------------------------------

WindowSettings Engine::get_window_settings() const
{
    WindowSettings out{};
    if (!_engine)
    {
        return out;
    }

    out.mode = from_engine_window_mode(_engine->_windowMode);
    out.display_index = _engine->_windowDisplayIndex;
    out.logical_width = _engine->_logicalRenderExtent.width;
    out.logical_height = _engine->_logicalRenderExtent.height;
    out.render_scale = _engine->renderScale;

    if (_engine->_window)
    {
        int display_index = SDL_GetWindowDisplayIndex(_engine->_window);
        if (display_index >= 0)
        {
            out.display_index = display_index;
        }
        SDL_GetWindowPosition(_engine->_window, &out.x, &out.y);
        SDL_GetWindowSize(_engine->_window, &out.width, &out.height);
    }

    return out;
}

void Engine::apply_window_settings(const WindowSettings& settings)
{
    if (!_engine || !_engine->_window)
    {
        return;
    }

    set_logical_render_extent(settings.logical_width, settings.logical_height);
    set_render_scale(settings.render_scale);

    _engine->setWindowMode(to_engine_window_mode(settings.mode), settings.display_index);

    if (settings.mode == WindowMode::Windowed)
    {
        int x = settings.x;
        int y = settings.y;
        int width = settings.width;
        int height = settings.height;
        clamp_window_rect_to_display(_engine->_window, settings.display_index, x, y, width, height);

        SDL_SetWindowSize(_engine->_window, width, height);
        SDL_SetWindowPosition(_engine->_window, x, y);

        SDL_PumpEvents();
        if (_engine->_swapchainManager)
        {
            _engine->_swapchainManager->resize_swapchain(_engine->_window);
            if (_engine->_ui)
            {
                _engine->_ui->onSwapchainRecreated();
            }
        }
    }
}

std::vector<DisplayInfo> Engine::get_displays() const
{
    std::vector<DisplayInfo> displays;
    const int count = SDL_GetNumVideoDisplays();
    if (count <= 0)
    {
        return displays;
    }

    displays.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        DisplayInfo info{};
        info.index = i;
        if (const char *name = SDL_GetDisplayName(i))
        {
            info.name = name;
        }
        else
        {
            info.name = "Display " + std::to_string(i + 1);
        }

        const SDL_Rect bounds = display_usable_bounds(i);
        const DisplayPixelMode pixel_mode = display_pixel_mode(i, _engine ? _engine->_window : nullptr);
        info.x = bounds.x;
        info.y = bounds.y;
        info.width = bounds.w;
        info.height = bounds.h;
        info.pixel_width = pixel_mode.mode.w > 0 ? pixel_mode.mode.w : bounds.w;
        info.pixel_height = pixel_mode.mode.h > 0 ? pixel_mode.mode.h : bounds.h;
        info.pixel_scale_x = pixel_mode.scale_x;
        info.pixel_scale_y = pixel_mode.scale_y;
        displays.push_back(std::move(info));
    }

    return displays;
}

std::vector<DisplayResolution> Engine::get_display_resolutions(int display_index) const
{
    std::vector<DisplayResolution> modes;
    const int display_count = SDL_GetNumVideoDisplays();
    if (display_count <= 0)
    {
        return modes;
    }

    display_index = std::clamp(display_index, 0, display_count - 1);
    const DisplayPixelMode pixel_mode = display_pixel_mode(display_index, _engine ? _engine->_window : nullptr);
    const int mode_count = SDL_GetNumDisplayModes(display_index);
    for (int i = 0; i < mode_count; ++i)
    {
        SDL_DisplayMode mode{};
        if (SDL_GetDisplayMode(display_index, i, &mode) != 0 || mode.w <= 0 || mode.h <= 0)
        {
            continue;
        }

        if (pixel_mode.scale_modes)
        {
            mode.w = static_cast<int>(std::lround(static_cast<float>(mode.w) * pixel_mode.scale_x));
            mode.h = static_cast<int>(std::lround(static_cast<float>(mode.h) * pixel_mode.scale_y));
        }

        const auto same_mode = [&](const DisplayResolution &existing) {
            return existing.width == mode.w &&
                   existing.height == mode.h &&
                   existing.refresh_rate == mode.refresh_rate;
        };
        if (std::find_if(modes.begin(), modes.end(), same_mode) == modes.end())
        {
            modes.push_back(DisplayResolution{mode.w, mode.h, mode.refresh_rate});
        }
    }

    std::sort(modes.begin(), modes.end(), [](const DisplayResolution &a, const DisplayResolution &b) {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        return a.refresh_rate > b.refresh_rate;
    });

    return modes;
}

void Engine::set_logical_render_extent(uint32_t width, uint32_t height)
{
    if (_engine)
    {
        _engine->setLogicalRenderExtent(VkExtent2D{std::max(1u, width), std::max(1u, height)});
    }
}

void Engine::set_render_scale(float scale)
{
    if (_engine)
    {
        _engine->setRenderScale(scale);
    }
}

float Engine::get_render_scale() const
{
    return _engine ? _engine->renderScale : 1.0f;
}

void Engine::set_pass_enabled(const std::string& passName, bool enabled)
{
    _engine->_rgPassToggles[passName] = enabled;
}

bool Engine::get_pass_enabled(const std::string& passName) const
{
    auto it = _engine->_rgPassToggles.find(passName);
    if (it != _engine->_rgPassToggles.end())
    {
        return it->second;
    }
    return true; // Default to enabled if not in map
}

void Engine::hot_reload_shaders()
{
    if (_engine->_pipelineManager)
    {
        _engine->_pipelineManager->hotReloadChanged();
    }
}

// ----------------------------------------------------------------------------
// Time
// ----------------------------------------------------------------------------

float Engine::get_delta_time() const
{
    if (_engine->_sceneManager)
    {
        return _engine->_sceneManager->getDeltaTime();
    }
    return 0.0f;
}

// ----------------------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------------------

Stats Engine::get_stats() const
{
    Stats s;
    s.frametime = _engine->stats.frametime;
    s.drawTime = _engine->stats.mesh_draw_time;
    s.sceneUpdateTime = _engine->stats.scene_update_time;
    s.triangleCount = _engine->stats.triangle_count;
    s.drawCallCount = _engine->stats.drawcall_count;
    return s;
}

} // namespace GameAPI
