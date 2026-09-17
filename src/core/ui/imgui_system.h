#pragma once

#include <core/types.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

union SDL_Event;
class EngineContext;
struct ImGuiStyle;
struct ImFont;

class ImGuiSystem
{
public:
    using DrawCallback = std::function<void()>;

    ImGuiSystem();
    ~ImGuiSystem();

    void init(EngineContext *context);
    void cleanup();

    void processEvent(const SDL_Event &event);

    void beginFrame();
    void endFrame();

    void addDrawCallback(DrawCallback callback);
    void clearDrawCallbacks();

    void setFontConfig(std::string regular_font_asset_path,
                       std::string bold_font_asset_path = {},
                       float base_font_size = 16.0f);

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;
    bool releaseKeyboardFocus();

    // Heavier (SemiBold) font baked into the same atlas, for UI labels that need
    // more weight. Null until fonts are built; pointer is refreshed on rebuild.
    // Pass to ImDrawList::AddText or ImGui::PushFont. Falls back to the regular
    // font at the call site when null.
    static ImFont *boldFont();

    // Logical-resolution UI scale (window size vs 1920x1080 baseline), baked
    // into the font atlas and style sizes. Independent of the DPI scale.
    float layoutScale() const { return _layout_scale; }

    void onSwapchainRecreated();

private:
    float computeDpiScale() const;
    float computeLayoutScale() const;
    void updateFramebufferScale();
    void updateMainViewportToLetterbox();
    void rebuildFonts();
    void applyScaledStyle();

    EngineContext *_context = nullptr;
    std::vector<DrawCallback> _draw_callbacks;

    VkDescriptorPool _imgui_pool = VK_NULL_HANDLE;
    VkFormat _swapchain_format = VK_FORMAT_UNDEFINED;

    float _dpi_scale = 1.0f;
    float _layout_scale = 1.0f;
    float _font_atlas_dpi_scale = 1.0f;
    float _font_atlas_layout_scale = 1.0f;
    float _pending_dpi_scale = 1.0f;
    float _pending_layout_scale = 1.0f;
    int _pending_scale_frames = 0;
    std::unique_ptr<ImGuiStyle> _baseline_style;
    float _base_font_size = 16.0f;
    std::string _regular_font_asset_path{"fonts/Pretendard-Regular.otf"};
    std::string _bold_font_asset_path{"fonts/Pretendard-SemiBold.otf"};
    bool _initialized = false;
};
