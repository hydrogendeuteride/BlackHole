#include "blackhole.h"

#include "core/context.h"
#include "core/assets/ibl_manager.h"
#include "core/assets/manager.h"
#include "core/device/device.h"
#include "core/device/resource.h"
#include "core/device/swapchain.h"
#include "core/frame/resources.h"
#include "core/pipeline/manager.h"
#include "core/pipeline/sampler.h"
#include "render/graph/graph.h"

namespace
{
    struct BlackholeData
    {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 camera;
        glm::vec4 center_radius;
        glm::vec4 params;
    };
}

void BlackholePass::init(EngineContext *context)
{
    _context = context;
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    for (uint32_t i = 1; i <= 3; ++i)
    {
        builder.add_binding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }
    _layout = builder.build(context->getDevice()->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    GraphicsPipelineCreateInfo info{};
    info.vertexShaderPath = context->getAssets()->shaderPath("common/fullscreen.vert.spv");
    info.fragmentShaderPath = context->getAssets()->shaderPath("blackhole/blackhole.frag.spv");
    info.setLayouts = {_layout};
    info.configure = [context](PipelineBuilder &b)
    {
        b.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        b.set_polygon_mode(VK_POLYGON_MODE_FILL);
        b.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        b.set_multisampling_none();
        b.disable_depthtest();
        b.disable_blending();
        b.set_color_attachment_format(context->getSwapchain()->drawImage().imageFormat);
    };
    context->pipelines->createGraphicsPipeline("blackhole", info);
}

void BlackholePass::cleanup()
{
    _context->pipelines->unregisterGraphics("blackhole");
    vkDestroyDescriptorSetLayout(_context->getDevice()->device(), _layout, nullptr);
}

RGImageHandle BlackholePass::register_graph(RenderGraph *graph, RGImageHandle color, RGImageHandle depth)
{
    if (!enabled || !_context->ibl || radius <= 0.0f) return color;
    auto *ibl = _context->ibl;
    VkImageView env = ibl->backgroundIs2D() ? ibl->background().imageView : VK_NULL_HANDLE;
    if (!env && ibl->specularIs2D()) env = ibl->specular().imageView;
    VkPipeline pipeline{};
    VkPipelineLayout pipeline_layout{};
    if (!env || !_context->pipelines->getGraphics("blackhole", pipeline, pipeline_layout)) return color;

    RGImageDesc desc{};
    desc.name = "hdr.blackhole";
    desc.format = _context->getSwapchain()->drawImage().imageFormat;
    desc.extent = _context->getDrawExtent();
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    auto output = graph->create_image(desc);
    graph->add_pass("Blackhole", RGPassType::Graphics,
        [=](RGPassBuilder &b, EngineContext *)
        {
            b.read(color, RGImageUsage::SampledFragment);
            b.read(depth, RGImageUsage::SampledFragment);
            b.write_color(output, true);
        },
        [this, color, depth, env, pipeline, pipeline_layout](VkCommandBuffer cmd,
            const RGPassResources &res, EngineContext *ctx)
        {
            auto *device = ctx->getDevice();
            auto *resources = ctx->getResources();
            const auto &scene = ctx->getSceneData();
            BlackholeData data{};
            data.view = scene.view;
            data.proj = scene.proj;
            data.camera = glm::vec4(-glm::transpose(glm::mat3(scene.view)) * glm::vec3(scene.view[3]), 1.0f);
            data.center_radius = glm::vec4(glm::vec3(center - ctx->origin_world), radius);
            data.params = glm::vec4(step, thickness, meshes ? 1.0f : 0.0f, 0.0f);
            auto buffer = resources->create_buffer(sizeof(data), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VMA_MEMORY_USAGE_CPU_TO_GPU);
            std::memcpy(buffer.info.pMappedData, &data, sizeof(data));
            vmaFlushAllocation(device->allocator(), buffer.allocation, 0, sizeof(data));
            ctx->currentFrame->_deletionQueue.push_function([resources, buffer]() { resources->destroy_buffer(buffer); });
            auto set = ctx->currentFrame->_frameDescriptors.allocate(device->device(), _layout);
            DescriptorWriter writer;
            writer.write_buffer(0, buffer.buffer, sizeof(data), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            writer.write_image(1, res.image_view(color), ctx->getSamplers()->linearClampEdge(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.write_image(2, res.image_view(depth), ctx->getSamplers()->nearestClampEdge(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.write_image(3, env, ctx->getSamplers()->linearRepeatClampEdge(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.update_set(device->device(), set);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &set, 0, nullptr);
            auto extent = ctx->getDrawExtent();
            VkViewport viewport{0.f, 0.f, float(extent.width), float(extent.height), 0.f, 1.f};
            VkRect2D scissor{{0, 0}, extent};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    return output;
}
