#include "render/passes/atmosphere/atmosphere_internal.h"

#include "vk_mem_alloc.h"
#include <cstring>

using namespace atmosphere::detail;

CloudShadowMap AtmospherePass::register_cloud_shadow(RenderGraph *graph)
{
    CloudShadowMap result{};
    if (!graph || !_context || !_context->scene || !_context->currentFrame || !_context->enablePlanetClouds)
        return result;
    const PlanetCloudSettings &s = _context->planetClouds;
    if (!s.shadowsEnabled || !std::isfinite(s.shadowStrength) || s.shadowStrength <= 0.0f ||
        !std::isfinite(s.baseHeightM) || !std::isfinite(s.thicknessM) || s.thicknessM <= 0.0f ||
        !std::isfinite(s.densityScale) || s.densityScale <= 0.0f ||
        !std::isfinite(s.extinction) || s.extinction <= 0.0f ||
        !std::isfinite(s.shadowExtentM) || !std::isfinite(s.coverage) ||
        !std::isfinite(s.noiseBlend) || !std::isfinite(s.noiseScale) ||
        !std::isfinite(s.typeBias) || !std::isfinite(s.detailErode) ||
        !std::isfinite(s.detailScale) || !std::isfinite(s.detailEvolutionPeriodS) ||
        !std::isfinite(s.windSpeed) || !std::isfinite(s.windAngleRad) ||
        !std::isfinite(s.overlayRotationRad) || !std::isfinite(s.weatherEvolutionPeriodS))
        return result;

    AtmosphereBodySelection body{};
    PlanetSystem *planets = _context->scene->get_planet_system();
    if (!planets || !planets->enabled() || !find_atmosphere_body(*_context, *_context->scene, *planets, body))
        return result;
    // Textures are preloaded before ResourceUploads, not created while registering this pass.
    if (_cloudOverlayTex.imageView == VK_NULL_HANDLE ||
        (_cloudNoiseTex3D.imageView == VK_NULL_HANDLE && _cloudNoiseFallback3D.imageView == VK_NULL_HANDLE))
        return result;
    const double radius = body.body->radius_m;
    const glm::dvec3 camera = _context->scene->getMainCamera().position_world - body.body->center_world;
    const glm::dvec3 raw_sun = -glm::dvec3(_context->getSceneData().sunlightDirection);
    if (!std::isfinite(radius) || radius < 1.0 || radius > 1.0e12 ||
        !MathUtil::finite(camera) || !MathUtil::finite(raw_sun) ||
        !MathUtil::finite(body.center_local)) return result;
    const double camera_length = glm::length(camera), sun_length = glm::length(raw_sun);
    if (!std::isfinite(camera_length) || camera_length < 1.0 ||
        !std::isfinite(sun_length) || sun_length < 1.0e-8) return result;
    const glm::dvec3 sun = raw_sun / sun_length;
    const glm::dvec3 ref = std::abs(sun.y) < 0.9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    const glm::dvec3 axis_x = glm::normalize(glm::cross(ref, sun));
    const glm::dvec3 axis_y = glm::cross(sun, axis_x);
    const glm::dvec3 ground = camera * (radius / camera_length);
    constexpr uint32_t resolution = 256;
    const double extent = std::clamp(double(s.shadowExtentM), 8000.0, 2048000.0);
    const double texel = 2.0 * extent / resolution;
    const double x = std::round(glm::dot(ground, axis_x) / texel) * texel;
    const double y = std::round(glm::dot(ground, axis_y) / texel) * texel;
    const double base = std::max(double(s.baseHeightM), 0.0);
    const double thickness = double(s.thicknessM);
    // Reject scales that float shell geometry cannot represent reliably.
    if (base / radius > 0.25 || thickness / radius < 1.0e-6 || thickness / radius > 0.25)
        return result;
    const double time = double(_context->getSceneData().timeParams.x);
    if (!std::isfinite(time)) return result;
    const double wind = std::remainder(double(s.windSpeed) * std::max(time, 0.0) /
                                      (radius + base + 0.5 * thickness), 2.0 * std::acos(-1.0));
    const double rotation = std::remainder(double(s.overlayRotationRad), 2.0 * std::acos(-1.0));
    const double phase = s.weatherEvolutionPeriodS > 1.0e-3f
        ? 2.0 * std::acos(-1.0) * std::fmod(std::max(time, 0.0), double(s.weatherEvolutionPeriodS)) /
          double(s.weatherEvolutionPeriodS) : 0.0;

    CloudShadowPush push{};
    push.plane = glm::vec4(x / radius, y / radius, extent / radius, radius);
    push.axis_x = glm::vec4(glm::vec3(axis_x), std::cos(s.windAngleRad));
    push.axis_y = glm::vec4(glm::vec3(axis_y), std::sin(s.windAngleRad));
    push.sun = glm::vec4(glm::vec3(sun), std::min(double(s.extinction) * s.densityScale, 1.0));
    push.layer = glm::vec4(base / radius, thickness / radius, std::clamp(s.coverage, 0.0f, 0.999f),
                          _cloudNoiseTex.imageView != VK_NULL_HANDLE ? std::clamp(s.noiseBlend, 0.0f, 1.0f) : 0.0f);
    push.motion = glm::vec4(std::sin(wind), std::cos(wind), std::sin(rotation), std::cos(rotation));
    push.noise = glm::vec4(std::clamp(s.noiseScale, 0.001f, 64.0f),
                          0.05 * (std::cos(phase) - 1.0), 0.05 * std::sin(phase),
                          std::clamp(s.shadowSteps, 4, 16));
    push.misc = glm::vec4(s.overlayFlipV ? 1.0f : 0.0f, std::clamp(s.typeBias, 0.0f, 1.0f),
                          std::clamp(s.detailErode, 0.0f, 1.0f),
                          _cloudNoiseTex.imageView != VK_NULL_HANDLE ? 1.0f : 0.0f);

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (!_context->pipelines->getGraphics("atmosphere.cloud_shadow", pipeline, layout)) return result;
    const auto import_texture = [&](const char *name, const AllocatedImage &image) {
        RGImportedImageDesc desc{};
        desc.name = name;
        desc.image = image.image;
        desc.imageView = image.imageView;
        desc.format = image.imageFormat;
        desc.extent = {image.imageExtent.width, image.imageExtent.height};
        desc.depth = image.imageExtent.depth;
        desc.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        desc.currentStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        desc.currentAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        return graph->import_image(desc);
    };
    const RGImageHandle overlay = import_texture("cloud.shadow.coverage", _cloudOverlayTex);
    // A missing weather map uses the coverage map; no untracked fallback descriptor.
    const RGImageHandle weather = _cloudNoiseTex.imageView != VK_NULL_HANDLE
        ? import_texture("cloud.shadow.weather", _cloudNoiseTex) : overlay;
    const bool has_shape = _cloudNoiseTex3D.imageView != VK_NULL_HANDLE;
    const RGImageHandle shape = import_texture("cloud.shadow.shape",
        has_shape ? _cloudNoiseTex3D : _cloudNoiseFallback3D);
    const double detail_phase = s.detailEvolutionPeriodS > 1.0e-3f
        ? 2.0 * std::acos(-1.0) * std::fmod(std::max(time, 0.0), double(s.detailEvolutionPeriodS)) /
          double(s.detailEvolutionPeriodS) : 0.0;
    const glm::vec4 shape_data(std::clamp(s.detailScale, 0.001f, 96.0f),
        0.03 * (std::cos(detail_phase) - 1.0), -0.03 * std::sin(detail_phase), has_shape ? 1.0f : 0.0f);
    ResourceManager *resources = _context->getResources();
    AllocatedBuffer shape_buffer = resources->create_buffer(sizeof(shape_data),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    std::memcpy(shape_buffer.info.pMappedData, &shape_data, sizeof(shape_data));
    vmaFlushAllocation(_context->getDevice()->allocator(), shape_buffer.allocation, 0, sizeof(shape_data));
    _context->currentFrame->_deletionQueue.push_function([resources, shape_buffer]() { resources->destroy_buffer(shape_buffer); });
    RGImageDesc desc{};
    desc.name = "atmosphere.cloud.shadow";
    desc.format = VK_FORMAT_R16_SFLOAT;
    desc.extent = {resolution, resolution};
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    result.image = graph->create_image(desc);
    result.data.origin = glm::vec4(world_to_local(body.body->center_world + axis_x * x + axis_y * y,
                                                _context->scene->get_world_origin()), 0.0f);
    result.data.axis_x = glm::vec4(glm::vec3(axis_x), extent);
    result.data.axis_y = glm::vec4(glm::vec3(axis_y), std::clamp(s.shadowStrength, 0.0f, 2.0f));
    result.data.planet = glm::vec4(body.center_local, radius);
    result.data.layer = glm::vec4(base, thickness, 0, 0);
    graph->add_pass("Atmosphere.CloudShadow", RGPassType::Graphics,
        [map = result.image, overlay, weather, shape, shape_buffer](RGPassBuilder &builder, EngineContext *) {
            builder.read(overlay, RGImageUsage::SampledFragment);
            builder.read(weather, RGImageUsage::SampledFragment);
            builder.read(shape, RGImageUsage::SampledFragment);
            builder.read_buffer(shape_buffer.buffer, RGBufferUsage::UniformRead, sizeof(glm::vec4), "cloud.shadow.params");
            VkClearValue clear{};
            clear.color.float32[0] = 1.0f;
            builder.write_color(map, true, clear);
        },
        [this, overlay, weather, shape, shape_buffer, push, pipeline, layout](VkCommandBuffer cmd, const RGPassResources &res, EngineContext *ctx) {
            VkDescriptorSet inputs = ctx->currentFrame->_frameDescriptors.allocate(ctx->getDevice()->device(), _cloudShadowSetLayout);
            DescriptorWriter writer;
            writer.write_image(0, res.image_view(overlay), ctx->getSamplers()->linearRepeatClampEdge(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.write_image(1, res.image_view(weather), ctx->getSamplers()->linearRepeatClampEdge(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.write_image(2, res.image_view(shape), ctx->getSamplers()->defaultLinear(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.write_buffer(3, shape_buffer.buffer, sizeof(glm::vec4), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            writer.update_set(ctx->getDevice()->device(), inputs);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &inputs, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            set_fullscreen_viewport(cmd, {resolution, resolution});
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    return result;
}
