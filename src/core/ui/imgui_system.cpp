#include "imgui_system.h"

#include "core/assets/locator.h"
#include "core/context.h"
#include "core/device/images.h"
#include "core/device/swapchain.h"
#include "core/ui/imgui_style.h"

#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

#include "device.h"

namespace
{
    // Refreshed every font rebuild; survives atlas recreation. One ImGui context,
    // so a file-scope pointer is sufficient.
    ImFont *g_bold_font = nullptr;

    VkDescriptorPool create_imgui_descriptor_pool(VkDevice device)
    {
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool));
        return pool;
    }

    uint32_t clamp_imgui_image_count(uint32_t count)
    {
        if (count < 2) return 2;
        if (count > 8) return 8;
        return count;
    }

    constexpr float kLayoutBaselineWidth = 1920.0f;
    constexpr float kLayoutBaselineHeight = 1080.0f;
    constexpr float kLayoutScaleMin = 0.85f;
    constexpr float kLayoutScaleMax = 1.2f;
    constexpr float kLayoutScaleStep = 0.05f;
    // Frames a new scale must hold before fonts rebuild. The Korean glyph atlas
    // is large; a live window-resize drag must not rebuild it every frame.
    constexpr int kScaleStableFrames = 8;
} // namespace

ImGuiSystem::ImGuiSystem() = default;
ImGuiSystem::~ImGuiSystem() = default;

void ImGuiSystem::init(EngineContext *context)
{
    if (_initialized)
    {
        return;
    }

    _context = context;
    if (_context == nullptr || _context->getDevice() == nullptr || _context->getSwapchain() == nullptr || _context->window == nullptr)
    {
        Logger::info("[ImGuiSystem] init skipped (missing context/device/swapchain/window)");
        return;
    }

    VkDevice device = _context->getDevice()->device();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ui::apply_game_imgui_style();

    ImGuiIO &io = ImGui::GetIO();

    _swapchain_format = _context->getSwapchain()->swapchainImageFormat();

    _dpi_scale = std::clamp(computeDpiScale(), 0.5f, 4.0f);
    _layout_scale = computeLayoutScale();
    _baseline_style = std::make_unique<ImGuiStyle>(ImGui::GetStyle());
    applyScaledStyle();
    rebuildFonts();

    _imgui_pool = create_imgui_descriptor_pool(device);

    ImGui_ImplSDL2_InitForVulkan(_context->window);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = _context->getDevice()->instance();
    init_info.PhysicalDevice = _context->getDevice()->physicalDevice();
    init_info.Device = _context->getDevice()->device();
    init_info.QueueFamily = _context->getDevice()->graphicsQueueFamily();
    init_info.Queue = _context->getDevice()->graphicsQueue();
    init_info.DescriptorPool = _imgui_pool;

    const auto &images = _context->getSwapchain()->swapchainImages();
    uint32_t image_count = images.empty() ? 3u : clamp_imgui_image_count(static_cast<uint32_t>(images.size()));
    init_info.MinImageCount = image_count;
    init_info.ImageCount = image_count;

    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo = {};
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchain_format;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    if (!ImGui_ImplVulkan_CreateFontsTexture())
    {
        Logger::warn("[ImGuiSystem] Warning: ImGui_ImplVulkan_CreateFontsTexture() failed");
    }

    const float atlas_scale = _font_atlas_dpi_scale * _font_atlas_layout_scale;
    io.FontGlobalScale = atlas_scale > 0.0f ? _layout_scale / atlas_scale : 1.0f;

    _initialized = true;
}

void ImGuiSystem::cleanup()
{
    if (!_initialized)
    {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    if (_context && _context->getDevice() && _imgui_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(_context->getDevice()->device(), _imgui_pool, nullptr);
    }
    _imgui_pool = VK_NULL_HANDLE;

    ImGui::DestroyContext();

    _baseline_style.reset();
    _draw_callbacks.clear();
    _context = nullptr;
    _initialized = false;
}

void ImGuiSystem::processEvent(const SDL_Event &event)
{
    if (!_initialized)
    {
        return;
    }
    ImGui_ImplSDL2_ProcessEvent(const_cast<SDL_Event *>(&event));
}

void ImGuiSystem::beginFrame()
{
    if (!_initialized)
    {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    updateFramebufferScale();

    // Apply scale changes only after they have been stable for a few frames.
    // Window resizing scales the existing atlas; a real framebuffer DPI change
    // rebuilds it once so text remains crisp after moving between monitors.
    const float new_dpi = std::clamp(computeDpiScale(), 0.5f, 4.0f);
    const float new_layout = computeLayoutScale();
    const bool dpi_changed = std::isfinite(new_dpi) && std::abs(new_dpi - _dpi_scale) > 0.05f;
    const bool layout_changed = std::abs(new_layout - _layout_scale) > 0.001f;
    if (dpi_changed || layout_changed)
    {
        const bool same_pending = std::abs(new_dpi - _pending_dpi_scale) <= 0.01f &&
                                  std::abs(new_layout - _pending_layout_scale) <= 0.001f;
        _pending_scale_frames = same_pending ? _pending_scale_frames + 1 : 1;
        _pending_dpi_scale = new_dpi;
        _pending_layout_scale = new_layout;
        if (_pending_scale_frames >= kScaleStableFrames)
        {
            _dpi_scale = new_dpi;
            _layout_scale = new_layout;
            if (dpi_changed)
            {
                rebuildFonts();
            }
            applyScaledStyle();
            updateFramebufferScale();
            _pending_scale_frames = 0;
        }
    }
    else
    {
        _pending_scale_frames = 0;
    }

    ImGui::NewFrame();
    updateMainViewportToLetterbox();

    for (auto &cb : _draw_callbacks)
    {
        if (cb) cb();
    }
}

void ImGuiSystem::endFrame()
{
    if (!_initialized)
    {
        return;
    }
    ImGui::Render();
}

void ImGuiSystem::addDrawCallback(DrawCallback callback)
{
    _draw_callbacks.push_back(std::move(callback));
}

void ImGuiSystem::clearDrawCallbacks()
{
    _draw_callbacks.clear();
}

void ImGuiSystem::setFontConfig(std::string regular_font_asset_path,
                                std::string bold_font_asset_path,
                                const float base_font_size)
{
    _regular_font_asset_path = std::move(regular_font_asset_path);
    _bold_font_asset_path = std::move(bold_font_asset_path);
    _base_font_size = std::max(8.0f, base_font_size);
    if (_initialized)
    {
        rebuildFonts();
    }
}

ImFont *ImGuiSystem::boldFont()
{
    return g_bold_font;
}

bool ImGuiSystem::wantCaptureMouse() const
{
    if (!_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiSystem::wantCaptureKeyboard() const
{
    if (!_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSystem::releaseKeyboardFocus()
{
    if (!_initialized || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
    {
        return false;
    }
    ImGui::SetWindowFocus(nullptr);
    return true;
}

void ImGuiSystem::onSwapchainRecreated()
{
    if (!_initialized || _context == nullptr || _context->getSwapchain() == nullptr)
    {
        return;
    }

    const auto &images = _context->getSwapchain()->swapchainImages();
    uint32_t image_count = images.empty() ? 3u : clamp_imgui_image_count(static_cast<uint32_t>(images.size()));
    ImGui_ImplVulkan_SetMinImageCount(image_count);

    updateFramebufferScale();
}

float ImGuiSystem::computeDpiScale() const
{
    if (_context == nullptr || _context->window == nullptr || _context->getSwapchain() == nullptr)
    {
        return 1.0f;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(_context->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0)
    {
        return _dpi_scale > 0.0f ? _dpi_scale : 1.0f;
    }

    VkExtent2D swap = _context->getSwapchain()->swapchainExtent();
    if (swap.width == 0 || swap.height == 0)
    {
        return _dpi_scale > 0.0f ? _dpi_scale : 1.0f;
    }

    float sx = static_cast<float>(swap.width) / static_cast<float>(win_w);
    float sy = static_cast<float>(swap.height) / static_cast<float>(win_h);

    if (!std::isfinite(sx) || !std::isfinite(sy))
    {
        return 1.0f;
    }

    return 0.5f * (sx + sy);
}

float ImGuiSystem::computeLayoutScale() const
{
    if (_context == nullptr || _context->window == nullptr)
    {
        return _layout_scale;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(_context->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0)
    {
        return _layout_scale;
    }

    const float scale = std::min(static_cast<float>(win_w) / kLayoutBaselineWidth,
                                 static_cast<float>(win_h) / kLayoutBaselineHeight);
    if (!std::isfinite(scale))
    {
        return _layout_scale;
    }

    // Quantized so a resize settles on a discrete step instead of oscillating
    // around the rebuild threshold.
    const float quantized = std::round(scale / kLayoutScaleStep) * kLayoutScaleStep;
    return std::clamp(quantized, kLayoutScaleMin, kLayoutScaleMax);
}

void ImGuiSystem::applyScaledStyle()
{
    if (!_baseline_style)
    {
        return;
    }

    // ScaleAllSizes is multiplicative, so always rescale from the baseline copy.
    ImGuiStyle scaled = *_baseline_style;
    scaled.ScaleAllSizes(_layout_scale);
    ImGui::GetStyle() = scaled;
}

void ImGuiSystem::updateFramebufferScale()
{
    if (_context == nullptr || _context->window == nullptr || _context->getSwapchain() == nullptr)
    {
        return;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(_context->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0)
    {
        return;
    }

    VkExtent2D swap = _context->getSwapchain()->swapchainExtent();
    if (swap.width == 0 || swap.height == 0)
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.DisplayFramebufferScale = ImVec2(static_cast<float>(swap.width) / static_cast<float>(win_w),
                                        static_cast<float>(swap.height) / static_cast<float>(win_h));

    // Font sizes are baked using the DPI and logical layout scale active when
    // the atlas was created. Scale that atlas to the current logical layout
    // without replacing its Vulkan texture during ordinary window resizing.
    const float atlas_scale = _font_atlas_dpi_scale * _font_atlas_layout_scale;
    io.FontGlobalScale = atlas_scale > 0.0f ? _layout_scale / atlas_scale : 1.0f;
}

void ImGuiSystem::updateMainViewportToLetterbox()
{
    if (_context == nullptr || _context->getSwapchain() == nullptr)
    {
        return;
    }

    const VkExtent2D draw = _context->getDrawExtent();
    const VkExtent2D swap = _context->getSwapchain()->swapchainExtent();
    if (draw.width == 0 || draw.height == 0 || swap.width == 0 || swap.height == 0)
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 scale = io.DisplayFramebufferScale;
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || scale.x <= 0.0f || scale.y <= 0.0f)
    {
        return;
    }

    const VkRect2D rect = vkutil::compute_letterbox_rect(draw, swap);
    if (rect.extent.width == 0 || rect.extent.height == 0)
    {
        return;
    }

    ImGuiViewportP *viewport = static_cast<ImGuiViewportP *>(ImGui::GetMainViewport());
    if (viewport == nullptr)
    {
        return;
    }

    const ImVec2 min_offset(static_cast<float>(rect.offset.x) / scale.x - viewport->Pos.x,
                            static_cast<float>(rect.offset.y) / scale.y - viewport->Pos.y);
    const ImVec2 max_offset((static_cast<float>(rect.offset.x + static_cast<int32_t>(rect.extent.width)) / scale.x) -
                                    (viewport->Pos.x + viewport->Size.x),
                            (static_cast<float>(rect.offset.y + static_cast<int32_t>(rect.extent.height)) / scale.y) -
                                    (viewport->Pos.y + viewport->Size.y));

    const ImVec2 previous_extra_min(std::max(0.0f, viewport->WorkOffsetMin.x - min_offset.x),
                                    std::max(0.0f, viewport->WorkOffsetMin.y - min_offset.y));
    const ImVec2 previous_extra_max(std::min(0.0f, viewport->WorkOffsetMax.x - max_offset.x),
                                    std::min(0.0f, viewport->WorkOffsetMax.y - max_offset.y));

    viewport->WorkOffsetMin = ImVec2(min_offset.x + previous_extra_min.x,
                                     min_offset.y + previous_extra_min.y);
    viewport->WorkOffsetMax = ImVec2(max_offset.x + previous_extra_max.x,
                                     max_offset.y + previous_extra_max.y);
    viewport->BuildWorkOffsetMin = min_offset;
    viewport->BuildWorkOffsetMax = max_offset;
    viewport->UpdateWorkRect();
}

void ImGuiSystem::rebuildFonts()
{
    if (_initialized)
    {
        // In-flight frames may still sample the old atlas and the texture is
        // destroyed immediately, so drain the GPU first.
        if (_context && _context->getDevice())
        {
            vkDeviceWaitIdle(_context->getDevice()->device());
        }
        ImGui_ImplVulkan_DestroyFontsTexture();
    }

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    // Layout scale is baked into the atlas (with the DPI factor) so scaled UI
    // text stays crisp; FontGlobalScale only compensates for DPI, leaving the
    // logical font size at base * layout.
    ImFontConfig cfg{};
    cfg.SizePixels = _base_font_size * _dpi_scale * _layout_scale;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    AssetLocator locator;
    locator.init();

    const std::string regular_font_path = locator.assetPath(_regular_font_asset_path);
    ImFont *regular_font = nullptr;
    if (!regular_font_path.empty())
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(regular_font_path, ec) && !ec)
        {
            regular_font = io.Fonts->AddFontFromFileTTF(regular_font_path.c_str(),
                                                        cfg.SizePixels,
                                                        &cfg,
                                                        io.Fonts->GetGlyphRangesKorean());
        }
    }
    if (!regular_font)
    {
        regular_font = io.Fonts->AddFontDefault(&cfg);
        Logger::warn("[ImGuiSystem] Font '{}' unavailable; using Dear ImGui default font",
                     _regular_font_asset_path);
    }
    io.FontDefault = regular_font;

    // Optional SemiBold companion baked into the same atlas. UI code grabs it via
    // boldFont() for labels that read too thin in the regular weight.
    g_bold_font = nullptr;
    const std::string bold_font_path =
            _bold_font_asset_path.empty() ? std::string{} : locator.assetPath(_bold_font_asset_path);
    if (!bold_font_path.empty())
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(bold_font_path, ec) && !ec)
        {
            g_bold_font = io.Fonts->AddFontFromFileTTF(bold_font_path.c_str(),
                                                       cfg.SizePixels,
                                                       &cfg,
                                                       io.Fonts->GetGlyphRangesKorean());
        }
    }

    _font_atlas_dpi_scale = _dpi_scale;
    _font_atlas_layout_scale = _layout_scale;
    const float atlas_scale = _font_atlas_dpi_scale * _font_atlas_layout_scale;
    io.FontGlobalScale = atlas_scale > 0.0f ? _layout_scale / atlas_scale : 1.0f;

    if (_initialized)
    {
        if (!ImGui_ImplVulkan_CreateFontsTexture())
        {
            Logger::warn("[ImGuiSystem] Warning: ImGui_ImplVulkan_CreateFontsTexture() failed after font configuration change");
        }
    }
}
