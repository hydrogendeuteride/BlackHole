#version 460
#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldRay;
layout(location = 2) flat in vec3 inCamLocal;

layout(location = 0) out vec4 outCloudLighting;
layout(location = 1) out vec4 outCloudSegment;

layout(set = 1, binding = 0) uniform sampler2D cloudLightingCurrentTex;
layout(set = 1, binding = 1) uniform sampler2D cloudSegmentCurrentTex;
layout(set = 1, binding = 2) uniform sampler2D cloudLightingHistoryTex;
layout(set = 1, binding = 3) uniform sampler2D cloudSegmentHistoryTex;
layout(set = 1, binding = 4) uniform sampler2D cloudDepthCurrentTex;

layout(push_constant) uniform CloudTemporalPush
{
    mat4 previous_view_proj;
    vec4 origin_delta_blend;
    vec4 viewport_params;
    vec4 planet_center_radius;
    vec4 cloud_motion; // x: wind speed, y: wind angle
    ivec4 misc;
} pc;

void build_cloud_frame(vec3 dir, out vec3 east, out vec3 north)
{
    vec3 refAxis = mix(vec3(0.0, 1.0, 0.0),
                       vec3(1.0, 0.0, 0.0),
                       step(0.999, abs(dir.y)));
    east = normalize(cross(refAxis, dir));
    north = cross(dir, east);
}

vec3 reproject_cloud_wind(vec3 midpoint)
{
    vec3 radial = midpoint - pc.planet_center_radius.xyz;
    float radius = length(radial);
    float deltaTime = clamp(sceneData.timeParams.y, 0.0, 0.25);
    if (radius <= 1.0 || abs(pc.cloud_motion.x) <= 1e-4 || deltaTime <= 0.0)
    {
        return midpoint;
    }

    vec3 dir = radial / radius;
    vec3 east;
    vec3 north;
    build_cloud_frame(dir, east, north);
    vec2 heading = vec2(cos(pc.cloud_motion.y), sin(pc.cloud_motion.y));
    vec3 tangent = east * heading.x + north * heading.y;
    float arc = pc.cloud_motion.x * deltaTime / radius;
    vec3 previousDir = dir * cos(arc) + tangent * sin(arc);
    return pc.planet_center_radius.xyz + previousDir * radius;
}

bool segment_valid(vec2 segment)
{
    return segment.x < segment.y;
}

int segment_count(vec4 segments)
{
    int count = 0;
    if (segment_valid(segments.xy)) count++;
    if (segment_valid(segments.zw)) count++;
    return count;
}

float segment_total_length(vec4 segments)
{
    float total = 0.0;
    if (segment_valid(segments.xy)) total += max(segments.y - segments.x, 0.0);
    if (segment_valid(segments.zw)) total += max(segments.w - segments.z, 0.0);
    return total;
}

float segment_error(vec4 lhs, vec4 rhs)
{
    int lhsCount = segment_count(lhs);
    int rhsCount = segment_count(rhs);
    if (lhsCount != rhsCount) return 1e20;

    float error = 0.0;
    if (lhsCount >= 1)
    {
        error += abs(lhs.x - rhs.x) + abs(lhs.y - rhs.y);
    }
    if (lhsCount == 2)
    {
        error += abs(lhs.z - rhs.z) + abs(lhs.w - rhs.w);
    }
    return error;
}

float segment_weighted_midpoint(vec4 segments)
{
    float len0 = segment_valid(segments.xy) ? max(segments.y - segments.x, 0.0) : 0.0;
    float len1 = segment_valid(segments.zw) ? max(segments.w - segments.z, 0.0) : 0.0;
    float totalLen = max(len0 + len1, 1e-3);

    float weightedMid = 0.0;
    if (len0 > 0.0) weightedMid += (0.5 * (segments.x + segments.y)) * len0;
    if (len1 > 0.0) weightedMid += (0.5 * (segments.z + segments.w)) * len1;
    return weightedMid / totalLen;
}

void main()
{
    vec4 currentLighting = texture(cloudLightingCurrentTex, inUV);
    vec4 currentSegment = texture(cloudSegmentCurrentTex, inUV);

    outCloudLighting = currentLighting;
    outCloudSegment = currentSegment;

    int currentCount = segment_count(currentSegment);
    if (pc.misc.x == 0 || currentCount == 0)
    {
        return;
    }

    vec3 rd = normalize(inWorldRay);
    float midT = texture(cloudDepthCurrentTex, inUV).r;
    if (midT <= 0.0) midT = segment_weighted_midpoint(currentSegment);
    vec3 currentMidpoint = inCamLocal + rd * midT;
    vec3 previousMidpoint = reproject_cloud_wind(currentMidpoint) + pc.origin_delta_blend.xyz;

    vec4 prevClip = pc.previous_view_proj * vec4(previousMidpoint, 1.0);
    if (prevClip.w <= 1e-5)
    {
        return;
    }

    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThanEqual(prevUV, vec2(1.0))))
    {
        return;
    }

    vec4 historyLighting = texture(cloudLightingHistoryTex, prevUV);
    vec4 historySegment = texture(cloudSegmentHistoryTex, prevUV);
    if (segment_count(historySegment) == 0)
    {
        return;
    }

    float currentLen = max(segment_total_length(currentSegment), 1e-3);
    float segmentMismatch = segment_error(historySegment, currentSegment);
    if (segmentMismatch > max(2500.0, currentLen * 0.35 * float(currentCount)))
    {
        return;
    }

    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(cloudLightingCurrentTex, 0);
    vec4 neighborhoodMin = vec4(1e20);
    vec4 neighborhoodMax = vec4(-1e20);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            ivec2 sampleCoord = clamp(texel + ivec2(x, y), ivec2(0), size - ivec2(1));
            vec4 sampleLighting = texelFetch(cloudLightingCurrentTex, sampleCoord, 0);
            neighborhoodMin = min(neighborhoodMin, sampleLighting);
            neighborhoodMax = max(neighborhoodMax, sampleLighting);
        }
    }

    vec4 clampedHistory = clamp(historyLighting, neighborhoodMin, neighborhoodMax);
    float currentLuma = dot(currentLighting.rgb, vec3(0.2126, 0.7152, 0.0722));
    float historyLuma = dot(historyLighting.rgb, vec3(0.2126, 0.7152, 0.0722));
    float transmittanceDelta = abs(currentLighting.a - historyLighting.a);
    float relativeLumaDelta = abs(currentLuma - historyLuma) /
                              max(max(currentLuma, historyLuma), 0.05);
    float reactive = max(smoothstep(0.025, 0.18, transmittanceDelta),
                         smoothstep(0.15, 0.60, relativeLumaDelta));
    float blend = clamp(pc.origin_delta_blend.w, 0.0, 0.98) * (1.0 - reactive);
    outCloudLighting = mix(currentLighting, clampedHistory, blend);
    outCloudSegment = currentSegment;
}
