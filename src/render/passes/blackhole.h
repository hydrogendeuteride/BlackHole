#pragma once

#include "render/renderpass.h"
#include "render/graph/types.h"

class RenderGraph;

class BlackholePass : public IRenderPass
{
public:
    void init(EngineContext *context) override;
    void cleanup() override;
    void execute(VkCommandBuffer) override {}
    const char *getName() const override { return "Blackhole"; }
    RGImageHandle register_graph(RenderGraph *graph, RGImageHandle color, RGImageHandle depth);

    bool enabled = false;
    bool meshes = true;
    glm::dvec3 center{0.0, 0.7, 0.0};
    float radius = 0.45f;
    float step = 0.025f;
    float thickness = 0.08f;

private:
    EngineContext *_context = nullptr;
    VkDescriptorSetLayout _layout = VK_NULL_HANDLE;
};
