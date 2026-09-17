#version 460
#extension GL_GOOGLE_include_directive : require

#include "atmosphere/include/cloud_profile.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outTransmittance;
layout(set = 0, binding = 0) uniform sampler2D coverageTex;
layout(set = 0, binding = 1) uniform sampler2D weatherTex;
layout(set = 0, binding = 2) uniform sampler3D shapeTex;
layout(set = 0, binding = 3, std140) uniform ShapeParams
{
    vec4 detail; // scale, evolution xy, 3D texture available
} shape;
layout(push_constant) uniform ShadowPush
{
    vec4 plane;
    vec4 axis_x;
    vec4 axis_y;
    vec4 sun;
    vec4 layer;
    vec4 motion;
    vec4 noise;
    vec4 misc;
} pc;

const float PI = 3.141592653589793;
const int MAX_STEPS = 16;

bool finite_value(float v) { return !isnan(v) && !isinf(v); }
bool finite_vector(vec3 v) { return !any(isnan(v)) && !any(isinf(v)); }

vec2 weather_uv(inout vec3 dir)
{
    vec3 ref = abs(dir.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 east = normalize(cross(ref, dir));
    vec3 north = cross(dir, east);
    vec3 tangent = east * pc.axis_x.w + north * pc.axis_y.w;
    dir = dir * pc.motion.y + tangent * pc.motion.x;
    dir = vec3(pc.motion.w * dir.x + pc.motion.z * dir.z, dir.y,
               -pc.motion.z * dir.x + pc.motion.w * dir.z);
    float longitude = dot(dir.xz, dir.xz) > 1e-12 ? atan(dir.z, dir.x) : 0.0;
    vec2 uv = vec2(longitude / (2.0 * PI) + 0.5,
                   acos(clamp(dir.y, -1.0, 1.0)) / PI);
    if (pc.misc.x > 0.5) uv.y = 1.0 - uv.y;
    return vec2(fract(uv.x), clamp(uv.y, 0.001, 0.999));
}

float shape_lod(float footprint)
{
    ivec3 size = textureSize(shapeTex, 0);
    return log2(max(footprint * float(max(size.x, max(size.y, size.z))), 1.0));
}

float shape_noise(vec3 p, float lod)
{
    return textureLod(shapeTex, fract(p),
        clamp(lod, 0.0, float(max(textureQueryLevels(shapeTex) - 1, 0)))).r;
}

float weather_noise(vec2 p, float footprint)
{
    ivec2 size = textureSize(weatherTex, 0);
    float lod = log2(max(footprint * float(max(size.x, size.y)), 1.0));
    return textureLod(weatherTex, fract(p),
        clamp(lod, 0.0, float(max(textureQueryLevels(weatherTex) - 1, 0)))).r;
}

float sample_density(vec3 dir, float h, float footprint)
{
    vec2 uv = weather_uv(dir);
    // Explicit, bounded LOD: no implicit derivatives inside divergent ray loops.
    float longitude = max(length(dir.xz), 0.05);
    vec2 uvFootprint = min(vec2(footprint / (2.0 * PI * longitude), footprint / PI), vec2(1.0));
    vec2 size = vec2(textureSize(coverageTex, 0));
    float lod = clamp(log2(max(max(uvFootprint.x * size.x, uvFootprint.y * size.y), 1.0)),
                      0.0, float(max(textureQueryLevels(coverageTex) - 1, 0)));
    float coverage = textureLod(coverageTex, uv, lod).r;
    size = vec2(textureSize(weatherTex, 0));
    lod = clamp(log2(max(pc.noise.x * max(uvFootprint.x * size.x, uvFootprint.y * size.y), 1.0)),
                0.0, float(max(textureQueryLevels(weatherTex) - 1, 0)));
    vec2 weatherSample = textureLod(weatherTex,
        fract(uv * pc.noise.x + vec2(0.173, 0.547) + pc.noise.yz), lod).rg;
    if (!finite_value(coverage) || !finite_value(weatherSample.r) ||
        !finite_value(weatherSample.g)) return 0.0;
    coverage = clamp(coverage, 0.0, 1.0);
    float weather = clamp((weatherSample.r - 0.5) * 1.25 + 0.5, 0.0, 1.0);
    float weatherField = mix(coverage, weather, pc.layer.w);
    float field = mix(coverage, weatherField, 0.5);
    float density = clamp((field - pc.layer.z) / max(1.0 - pc.layer.z, 0.001), 0.0, 1.0);
    density = pow(density, mix(1.45, 0.75, weatherField));
    // RG weather maps carry cloud type. Legacy R maps reuse R without another fetch.
    float type = pc.misc.w > 0.5
        ? (weatherSample.g > 1.0 / 255.0 ? weatherSample.g : weatherSample.r) : 0.5;
    CloudVerticalProfile profile = sample_vertical_profile(h, weatherField, type, pc.misc.y);
    vec2 shear = vec2(pc.axis_x.w, pc.axis_y.w)
        * ((profile.hLocal - 0.5) * mix(0.018, 0.080, weatherField));
    vec2 evolution = shape.detail.yz;
    float sliceU, sliceV, erodeNoise, erodeLod;
    if (shape.detail.w > 0.5)
    {
        // Same metric coordinates, column masks and evolution as visible clouds.
        float frequency = clamp(shape.detail.x / 12.0, 0.125, 8.0);
        float radiusM = pc.plane.w * (1.0 + pc.layer.x + h * pc.layer.y);
        vec3 metric = dir * radiusM;
        float footprintM = footprint * radiusM;
        float tile = 480000.0 / frequency;
        vec3 evolutionA = vec3(evolution, 0.35 * (evolution.x + evolution.y));
        vec3 evolutionB = vec3(-evolution.y, -evolution.x, 0.40 * (evolution.x - evolution.y));
        sliceU = shape_noise(metric / tile + vec3(shear.x, 0, shear.y)
            + vec3(0.619, 0.281, 0.113) + evolutionA, shape_lod(footprintM / tile));
        sliceV = shape_noise(vec3(metric.z, metric.x, -metric.y) / (tile * 0.79)
            + vec3(-shear.y, 0, shear.x) + vec3(0.173, 0.547, 0.763) + evolutionB,
            shape_lod(footprintM / (tile * 0.79)));
        float detailTile = 120000.0 / frequency;
        erodeLod = shape_lod(footprintM / detailTile);
        erodeNoise = shape_noise(metric / detailTile + vec3(0.377, 0.113, 0.531)
            + evolutionA * 1.35, erodeLod);
    }
    else
    {
        vec2 scale = vec2(shape.detail.x, 1.65);
        float detailFootprint = max(uvFootprint.x, uvFootprint.y) * shape.detail.x;
        sliceU = weather_noise(vec2(uv.x + shear.x, profile.hLocal) * scale
            + vec2(0.619, 0.281) + evolution, detailFootprint);
        sliceV = weather_noise(vec2(uv.y + shear.y, profile.hLocal) * scale * vec2(0.85, 1.13)
            + vec2(0.113, 0.763) - evolution.yx, detailFootprint);
        erodeNoise = weather_noise(vec2(uv.x + shear.x, profile.hLocal) * scale * 4.0
            + vec2(0.377, 0.531) + evolution * 1.35, detailFootprint * 4.0);
        ivec2 size = textureSize(weatherTex, 0);
        erodeLod = log2(max(detailFootprint * 4.0 * float(max(size.x, size.y)), 1.0));
    }
    if (!finite_value(sliceU) || !finite_value(sliceV) || !finite_value(erodeNoise)) return 0.0;
    sliceU = clamp((sliceU - 0.5) * 1.55 + 0.5, 0.0, 1.0);
    sliceV = clamp((sliceV - 0.5) * 1.55 + 0.5, 0.0, 1.0);
    float column = smoothstep(0.18, 0.82, mix(sliceU, sliceV, 0.45));
    density *= profile.density * column;
    float erosion = mix(erodeNoise, 1.0 - erodeNoise, clamp(profile.hLocal * 3.0, 0.0, 1.0))
        * 0.45 * clamp(pc.misc.z, 0.0, 1.0) * (1.0 - smoothstep(1.5, 4.0, erodeLod));
    return clamp((density - erosion) / max(1.0 - erosion, 0.001), 0.0, 1.0);
}

void main()
{
    outTransmittance = 1.0;
    vec2 xy = pc.plane.xy + (inUV * 2.0 - 1.0) * pc.plane.z;
    float rho = length(xy);
    float base = 1.0 + pc.layer.x;
    if (!finite_value(rho) || rho >= base || pc.layer.y <= 0.0) return;
    float mu = sqrt(max((base - rho) * (base + rho), 0.0)) / base;
    if (mu <= 0.05) return;
    float grazing = smoothstep(0.05, 0.15, mu);
    vec3 plane = pc.axis_x.xyz * xy.x + pc.axis_y.xyz * xy.y;
    int steps = clamp(int(pc.noise.w), 4, MAX_STEPS);
    float dh = pc.layer.y / float(steps);
    float opticalDepth = 0.0;
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        if (i >= steps) break;
        float h = (float(i) + 0.5) / float(steps);
        float r0 = base + float(i) * dh;
        float r1 = base + float(i + 1) * dh;
        float r = base + h * pc.layer.y;
        float t0 = sqrt(max((r0 - rho) * (r0 + rho), 0.0));
        float t1 = sqrt(max((r1 - rho) * (r1 + rho), 0.0));
        // Rationalized difference: avoids subtracting nearly equal roots.
        float lengthM = dh * (r0 + r1) / max(t0 + t1, 0.001) * pc.plane.w;
        float t = sqrt(max((r - rho) * (r + rho), 0.0));
        vec3 dir = (plane + pc.sun.xyz * t) / r;
        if (!finite_vector(dir) || !finite_value(lengthM)) return;
        float footprint = 2.0 * pc.plane.z / (256.0 * mu);
        float density = sample_density(dir, h, footprint);
        opticalDepth = min(opticalDepth + density * lengthM * pc.sun.w, 10.0);
        if (!finite_value(opticalDepth)) return;
        if (opticalDepth >= 10.0) break;
    }
    outTransmittance = mix(1.0, exp(-opticalDepth), grazing);
}
