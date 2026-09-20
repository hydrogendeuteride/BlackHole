#pragma once

#include "render/renderpass.h"
#include "render/graph/types.h"
#include "star_catalog.h"

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
    bool stars = false;
    float star_brightness = 0.5f;
    float star_size = 0.012f;
    float star_magnitude = 7.5f;
    float star_rotation = 0.0f;
    bool star_redshift = true;
    bool disk = false;
    bool disk_clouds = true;
    float disk_speed = 1.0f;
    float disk_time = 0.0f;
    float disk_height = 0.08f;
    float disk_contrast = 0.8f;
    float disk_inner = 3.0f;
    float disk_outer = 10.0f;
    float disk_brightness = 3.0f;
    float disk_temperature = 6500.0f;
    float disk_absorption = 8.0f;
    float disk_emission = 1.0f;
    bool disk_redshift = true;
    bool disk_doppler = true;
    bool disk_beaming = true;
    uint32_t star_count() const { return _catalog.count; }

private:
    float _disk_direction = 1.0f;
    EngineContext *_context = nullptr;
    VkDescriptorSetLayout _layout = VK_NULL_HANDLE;
    StarCatalog _catalog;
    AllocatedImage _fallback{};
};
