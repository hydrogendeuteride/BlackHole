#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "input_structures.glsl"
#include "blackbody.glsl"
#include "planet_gbuffer_payload.glsl"
#include "planet_terrain_common.glsl"
#include "planet_ocean_mask_common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec3 inObjectPos;
layout(location = 6) in vec3 inObjectNormal;

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec4 outNorm;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out uint outObjectID;
layout(location = 4) out vec4 outExtra;

// Keep push constants layout in sync with mesh.vert / GPUDrawPushConstants
struct Vertex {
    vec3 position; float uv_x;
    vec3 normal;   float uv_y;
    vec4 color;
    vec4 tangent;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{
    Vertex vertices[];
};

layout(push_constant) uniform constants
{
    mat4 render_matrix;
    mat3 normal_matrix;
    VertexBuffer vertexBuffer;
    uint objectID;
} PushConstants;

// Takes the continuous water mask (not the binarized ocean coverage) so
// rivers and shorelines with partial mask values still get a glossy,
// darkened surface from deferred lighting. Full-ocean pixels are overwritten
// by the ocean pass anyway, so there is no double-specular there.
void apply_planet_water_override(inout vec3 albedo, inout float roughness, float waterMask)
{
    if (waterMask <= 0.0)
    {
        return;
    }

    float oceanRoughness = clamp(materialData.extra[3].w, 0.04, 1.0);
    roughness = mix(roughness, oceanRoughness, waterMask);

    float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3 oceanTarget = max(mix(vec3(luma), vec3(0.045, 0.085, 0.205), 0.65), vec3(0.028, 0.048, 0.095));
    albedo = mix(albedo, oceanTarget, waterMask * 0.6);
}

float planet_ocean_shell_depth(vec3 surfacePos, float fallbackDepth)
{
    vec3 center = terrain_planet_center_local();
    float radius = terrain_planet_radius_m();
    vec3 radial = surfacePos - center;
    float radialLen2 = dot(radial, radial);
    if (radialLen2 <= 1.0e-8)
    {
        return fallbackDepth;
    }

    float shellOffset = max(2.0, radius * 1.0e-6);
    float shellRadius = radius + terrain_height_offset_m() + shellOffset;
    vec3 shellPos = center + radial * inversesqrt(radialLen2) * shellRadius;
    vec4 clip = sceneData.viewproj * vec4(shellPos, 1.0);
    if (clip.w <= 1.0e-6)
    {
        return fallbackDepth;
    }

    float depth = clip.z / clip.w;
    if (depth < 0.0 || depth > 1.0)
    {
        return fallbackDepth;
    }

    float bias = max(abs(depth) * 1.0e-5, 1.0e-8);
    return clamp(depth - bias, 0.0, 1.0);
}

void main() {
    // Apply baseColor texture and baseColorFactor once
    vec4 baseTex = texture(colorTex, inUV);
    // Alpha from baseColor texture and factor, used for cutouts on MASK materials.
    float alpha = clamp(baseTex.a * materialData.colorFactors.a, 0.0, 1.0);
    float alphaCutoff = materialData.extra[2].x;
    if (alphaCutoff > 0.0 && alpha < alphaCutoff)
    {
        discard;
    }
    vec3 albedo = inColor * baseTex.rgb * materialData.colorFactors.rgb;
    bool isTerrain = terrain_material_enabled();

    float waterMask = isTerrain ? sample_planet_ocean_mask(inUV) : 0.0;
    float oceanCoverage = isTerrain ? sample_planet_ocean_coverage(inUV) : 0.0;
    float oceanDepthFlag = planet_ocean_flag(oceanCoverage);
    // Store continuous coverage in the G-buffer so atmosphere tint fades across antialiased shorelines.
    // Full ocean pixels write shell depth so later terrain behind it stays rejected while ocean can redraw it.
    float gbufferDepth = gl_FragCoord.z;
    if (oceanDepthFlag > 0.5)
    {
        gbufferDepth = planet_ocean_shell_depth(inWorldPos, gbufferDepth);
    }
    gl_FragDepth = gbufferDepth;

    float roughness = clamp(materialData.metal_rough_factors.y, 0.04, 1.0);
    float metallic  = clamp(materialData.metal_rough_factors.x, 0.0, 1.0);
    if (!isTerrain)
    {
        // glTF metallic-roughness in G (roughness) and B (metallic)
        vec2 mrTex = texture(metalRoughTex, inUV).gb;
        roughness = clamp(mrTex.x * materialData.metal_rough_factors.y, 0.04, 1.0);
        metallic = clamp(mrTex.y * materialData.metal_rough_factors.x, 0.0, 1.0);
    }
    apply_planet_water_override(albedo, roughness, waterMask);

    // Normal mapping: decode tangent-space normal and transform to world space
    // Expect UNORM normal map; support BC5 (RG) by reconstructing Z from XY.
    vec3 N = normalize(inNormal);
    vec3 Nw = N;
    vec3 terrainGeomNormal = N;
    vec3 terrainTerminatorNormal = N;

    if (isTerrain)
    {
        terrainTerminatorNormal = terrain_shading_normal_from_height(inUV, inWorldPos, terrainGeomNormal);
        Nw = terrainTerminatorNormal;
        Nw = apply_terrain_detail_normal(Nw, inUV, PushConstants.normal_matrix);
    }
    else
    {
        float normalScale = max(materialData.extra[0].x, 0.0);
        if (normalScale > 0.0)
        {
            vec2 enc = texture(normalMap, inUV).xy * 2.0 - 1.0;
            enc *= normalScale;
            float z2 = 1.0 - dot(enc, enc);
            float nz = z2 > 0.0 ? sqrt(z2) : 0.0;
            vec3 Nm = vec3(enc, nz);

            vec3 T = normalize(inTangent.xyz);
            vec3 B = normalize(cross(N, T)) * inTangent.w;
            Nw = normalize(T * Nm.x + B * Nm.y + N * Nm.z);
        }
    }

    // outPos.w is used as a "valid pixel" marker by downstream passes.
    // Keep 0.0 reserved for background/no-geometry, and encode small flags above 1.0.
    // Convention: materialData.extra[2].y > 0 => planet-style shadowing override.
    float gbuffer_flags = materialData.extra[2].y;
    vec3 Lsun = -sceneData.sunlightDirection.xyz;
    float terrainSunVis = isTerrain ? terrain_terminator_visibility(inUV, terrainTerminatorNormal, terrainGeomNormal, inWorldPos, Lsun) : 1.0;
    outPos = vec4(inWorldPos, encode_planet_gbuffer_pos_w(gbuffer_flags, oceanCoverage, terrainSunVis));
    outNorm = vec4(Nw, roughness);
    outAlbedo = vec4(albedo, metallic);
    // Extra G-buffer: x = AO, yzw = emissive
    // extra[0].y = AO strength, extra[0].z = hasAO flag (1 = use AO texture)
    float hasAO = materialData.extra[0].z;
    float aoStrength = clamp(materialData.extra[0].y, 0.0, 1.0);
    float ao = 1.0;
    if (hasAO > 0.5 && aoStrength > 0.0)
    {
        float aoTex = texture(occlusionTex, inUV).r;
        ao = 1.0 - aoStrength + aoStrength * aoTex;
    }

    vec3 emissive = evaluate_blackbody_emissive(emissiveTex,
        materialData.extra[9], materialData.extra[10], materialData.extra[11],
        materialData.extra[12], materialData.extra[13],
        inObjectPos, inObjectNormal, inUV, sceneData.timeParams.x);
    if (emissive == vec3(0.0) && materialData.extra[9].x <= 0.5)
    {
        vec3 emissiveFactor = materialData.extra[1].rgb;
        if (any(greaterThan(emissiveFactor, vec3(0.0))))
        {
            vec3 emissiveSample = texture(emissiveTex, inUV).rgb;
            emissive = emissiveSample * emissiveFactor;
        }
    }
    outExtra = vec4(ao, emissive);
    outObjectID = PushConstants.objectID;
}
