#include "imgui_pass.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "core/context.h"
#include "core/device/images.h"
#include "core/device/swapchain.h"
#include "render/graph/graph.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    struct ClipRectBackup
    {
        ImDrawCmd *cmd{nullptr};
        ImVec4 rect{};
    };

    bool compute_imgui_letterbox_clip(EngineContext *ctx, const ImDrawData *draw_data, ImVec4 &out_clip)
    {
        if (ctx == nullptr || ctx->getSwapchain() == nullptr || draw_data == nullptr)
        {
            return false;
        }

        const VkExtent2D draw = ctx->getDrawExtent();
        const VkExtent2D swap = ctx->getSwapchain()->swapchainExtent();
        if (draw.width == 0 || draw.height == 0 || swap.width == 0 || swap.height == 0)
        {
            return false;
        }

        const ImVec2 scale = draw_data->FramebufferScale;
        if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || scale.x <= 0.0f || scale.y <= 0.0f)
        {
            return false;
        }

        const VkRect2D rect = vkutil::compute_letterbox_rect(draw, swap);
        if (rect.extent.width == 0 || rect.extent.height == 0)
        {
            return false;
        }

        const ImVec2 display_pos = draw_data->DisplayPos;
        out_clip = ImVec4(
            display_pos.x + static_cast<float>(rect.offset.x) / scale.x,
            display_pos.y + static_cast<float>(rect.offset.y) / scale.y,
            display_pos.x + static_cast<float>(rect.offset.x + static_cast<int32_t>(rect.extent.width)) / scale.x,
            display_pos.y + static_cast<float>(rect.offset.y + static_cast<int32_t>(rect.extent.height)) / scale.y);
        return out_clip.z > out_clip.x && out_clip.w > out_clip.y;
    }

    std::vector<ClipRectBackup> clip_draw_data_to_rect(ImDrawData *draw_data, const ImVec4 &clip_rect)
    {
        std::vector<ClipRectBackup> backups;
        if (draw_data == nullptr)
        {
            return backups;
        }

        int cmd_count = 0;
        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            cmd_count += draw_data->CmdLists[n]->CmdBuffer.Size;
        }
        backups.reserve(static_cast<size_t>(std::max(cmd_count, 0)));

        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            ImDrawList *cmd_list = draw_data->CmdLists[n];
            for (int i = 0; i < cmd_list->CmdBuffer.Size; ++i)
            {
                ImDrawCmd *cmd = &cmd_list->CmdBuffer[i];
                backups.push_back(ClipRectBackup{cmd, cmd->ClipRect});
                cmd->ClipRect.x = std::max(cmd->ClipRect.x, clip_rect.x);
                cmd->ClipRect.y = std::max(cmd->ClipRect.y, clip_rect.y);
                cmd->ClipRect.z = std::min(cmd->ClipRect.z, clip_rect.z);
                cmd->ClipRect.w = std::min(cmd->ClipRect.w, clip_rect.w);
            }
        }

        return backups;
    }

    void restore_clip_rects(const std::vector<ClipRectBackup> &backups)
    {
        for (const ClipRectBackup &backup : backups)
        {
            if (backup.cmd)
            {
                backup.cmd->ClipRect = backup.rect;
            }
        }
    }
} // namespace

void ImGuiPass::init(EngineContext *context)
{
    _context = context;
}

void ImGuiPass::cleanup()
{
}

void ImGuiPass::execute(VkCommandBuffer)
{
    // ImGui is executed via the render graph now.
}

void ImGuiPass::register_graph(RenderGraph *graph, RGImageHandle swapchainHandle)
{
    if (!graph || !swapchainHandle.valid()) return;

    graph->add_pass(
        "ImGui",
        RGPassType::Graphics,
        [swapchainHandle](RGPassBuilder &builder, EngineContext *)
        {
            builder.write_color(swapchainHandle, false, {});
        },
        [this, swapchainHandle](VkCommandBuffer cmd, const RGPassResources &res, EngineContext *ctx)
        {
            draw_imgui(cmd, ctx, res, swapchainHandle);
        });
}

void ImGuiPass::draw_imgui(VkCommandBuffer cmd,
                           EngineContext *context,
                           const RGPassResources &resources,
                           RGImageHandle targetHandle) const
{
    EngineContext *ctxLocal = context ? context : _context;
    if (!ctxLocal) return;

    VkImageView targetImageView = resources.image_view(targetHandle);
    if (targetImageView == VK_NULL_HANDLE) return;

    // Dynamic rendering is handled by the RenderGraph; just render draw data.
    ImDrawData *draw_data = ImGui::GetDrawData();
    ImVec4 letterbox_clip{};
    std::vector<ClipRectBackup> clip_backups;
    if (compute_imgui_letterbox_clip(ctxLocal, draw_data, letterbox_clip))
    {
        clip_backups = clip_draw_data_to_rect(draw_data, letterbox_clip);
    }

    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
    restore_clip_rects(clip_backups);
}
