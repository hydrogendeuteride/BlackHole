#include "core/ui/imgui_style.h"

#include "imgui.h"

namespace ui
{
    void apply_game_imgui_style()
    {
        ImGui::StyleColorsDark();

        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(7.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        style.WindowRounding = 5.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 5.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.TabBarBorderSize = 1.0f;

        ImVec4 *c = style.Colors;
        const ImVec4 bg = ImVec4(0.055f, 0.070f, 0.085f, 0.96f);
        const ImVec4 bg_deep = ImVec4(0.035f, 0.047f, 0.060f, 1.00f);
        const ImVec4 bg_panel = ImVec4(0.075f, 0.100f, 0.120f, 0.72f);
        const ImVec4 bg_field = ImVec4(0.100f, 0.140f, 0.170f, 0.82f);
        const ImVec4 accent = ImVec4(0.120f, 0.720f, 0.880f, 1.00f);
        const ImVec4 accent_hover = ImVec4(0.160f, 0.840f, 1.000f, 1.00f);
        const ImVec4 accent_active = ImVec4(0.080f, 0.580f, 0.740f, 1.00f);
        const ImVec4 accent_muted = ImVec4(0.090f, 0.270f, 0.340f, 0.78f);
        const ImVec4 line = ImVec4(0.180f, 0.330f, 0.400f, 0.48f);

        c[ImGuiCol_Text] = ImVec4(0.900f, 0.950f, 0.980f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.460f, 0.550f, 0.620f, 1.00f);
        c[ImGuiCol_WindowBg] = bg;
        c[ImGuiCol_ChildBg] = bg_panel;
        c[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.060f, 0.075f, 0.98f);
        c[ImGuiCol_Border] = line;
        c[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

        c[ImGuiCol_FrameBg] = bg_field;
        c[ImGuiCol_FrameBgHovered] = accent_muted;
        c[ImGuiCol_FrameBgActive] = ImVec4(accent_active.x, accent_active.y, accent_active.z, 0.88f);
        c[ImGuiCol_TitleBg] = bg_deep;
        c[ImGuiCol_TitleBgActive] = ImVec4(0.060f, 0.110f, 0.135f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(bg_deep.x, bg_deep.y, bg_deep.z, 0.82f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.050f, 0.068f, 0.080f, 0.95f);

        c[ImGuiCol_ScrollbarBg] = ImVec4(0.030f, 0.040f, 0.050f, 0.45f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.180f, 0.280f, 0.330f, 0.75f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.230f, 0.430f, 0.500f, 0.88f);
        c[ImGuiCol_ScrollbarGrabActive] = accent_active;

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accent_hover;
        c[ImGuiCol_Button] = ImVec4(0.100f, 0.180f, 0.220f, 0.82f);
        c[ImGuiCol_ButtonHovered] = ImVec4(accent_hover.x, accent_hover.y, accent_hover.z, 0.72f);
        c[ImGuiCol_ButtonActive] = accent_active;
        c[ImGuiCol_Header] = ImVec4(0.100f, 0.220f, 0.280f, 0.72f);
        c[ImGuiCol_HeaderHovered] = ImVec4(accent_hover.x, accent_hover.y, accent_hover.z, 0.62f);
        c[ImGuiCol_HeaderActive] = ImVec4(accent_active.x, accent_active.y, accent_active.z, 0.82f);

        c[ImGuiCol_Separator] = line;
        c[ImGuiCol_SeparatorHovered] = ImVec4(accent_hover.x, accent_hover.y, accent_hover.z, 0.70f);
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = ImVec4(0.140f, 0.320f, 0.380f, 0.32f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(accent_hover.x, accent_hover.y, accent_hover.z, 0.66f);
        c[ImGuiCol_ResizeGripActive] = accent;

        c[ImGuiCol_Tab] = ImVec4(0.070f, 0.100f, 0.120f, 0.88f);
        c[ImGuiCol_TabHovered] = ImVec4(accent_hover.x, accent_hover.y, accent_hover.z, 0.62f);
        c[ImGuiCol_TabActive] = ImVec4(0.100f, 0.200f, 0.250f, 0.96f);
        c[ImGuiCol_TabUnfocused] = ImVec4(0.055f, 0.070f, 0.085f, 0.92f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.080f, 0.125f, 0.150f, 0.95f);

        c[ImGuiCol_PlotLines] = ImVec4(0.500f, 0.780f, 0.880f, 1.00f);
        c[ImGuiCol_PlotLinesHovered] = accent_hover;
        c[ImGuiCol_PlotHistogram] = ImVec4(0.950f, 0.720f, 0.260f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.000f, 0.820f, 0.360f, 1.00f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.070f, 0.110f, 0.135f, 1.00f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.160f, 0.300f, 0.360f, 0.80f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.120f, 0.220f, 0.270f, 0.55f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(0.150f, 0.250f, 0.300f, 0.14f);

        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
        c[ImGuiCol_DragDropTarget] = ImVec4(0.950f, 0.720f, 0.260f, 0.90f);
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_NavWindowingHighlight] = ImVec4(0.900f, 0.950f, 0.980f, 0.70f);
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.010f, 0.015f, 0.020f, 0.55f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.010f, 0.015f, 0.020f, 0.68f);
    }
}
