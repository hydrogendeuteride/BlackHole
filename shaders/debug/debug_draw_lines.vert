#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 outColor;
layout(location = 1) noperspective out float outSide;
layout(location = 2) flat out float outEdgeSoftness;
layout(location = 3) noperspective out float outDashCoordPx;

struct DebugDrawVertex
{
    // Matches DebugDrawVertex on the CPU: vec3 position + float dash_coord_px.
    vec4 positionDash;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer DebugDrawVertexBuffer
{
    DebugDrawVertex vertices[];
};

layout(push_constant) uniform DebugDrawPush
{
    layout(offset = 0) mat4 viewproj;
    layout(offset = 64) DebugDrawVertexBuffer vertexBuffer;
    layout(offset = 72) vec2 invViewportSizeNdc;
    layout(offset = 80) float halfLineWidthPx;
    layout(offset = 84) float aaPx;
} pc;

const float kClipPlaneEpsilon = 1.0e-4;

#include "debug/line_raster_common.glsl"

float clip_plane_distance(vec4 clipPos, int planeIndex)
{
    if (planeIndex == 0)
    {
        return clipPos.x + clipPos.w;
    }
    if (planeIndex == 1)
    {
        return -clipPos.x + clipPos.w;
    }
    if (planeIndex == 2)
    {
        return clipPos.y + clipPos.w;
    }
    if (planeIndex == 3)
    {
        return -clipPos.y + clipPos.w;
    }
    if (planeIndex == 4)
    {
        return clipPos.z;
    }
    return clipPos.w - clipPos.z;
}

bool clip_segment_to_view(inout vec4 clipA, inout vec4 clipB)
{
    float t0 = 0.0;
    float t1 = 1.0;
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        const float d0 = clip_plane_distance(clipA, planeIndex);
        const float d1 = clip_plane_distance(clipB, planeIndex);
        const bool aInside = d0 >= kClipPlaneEpsilon;
        const bool bInside = d1 >= kClipPlaneEpsilon;
        if (aInside && bInside)
        {
            continue;
        }
        if (!aInside && !bInside)
        {
            return false;
        }

        const float denominator = d1 - d0;
        if (abs(denominator) <= kDirectionEpsilon)
        {
            return false;
        }

        const float t = clamp(
                (kClipPlaneEpsilon - d0) / denominator, 0.0, 1.0);
        if (!aInside)
        {
            t0 = max(t0, t);
        }
        else
        {
            t1 = min(t1, t);
        }
        if (t0 > t1)
        {
            return false;
        }
    }

    const vec4 originalA = clipA;
    const vec4 originalB = clipB;
    clipA = mix(originalA, originalB, t0);
    clipB = mix(originalA, originalB, t1);
    return true;
}

void main()
{
    const uint segmentIndex = uint(gl_InstanceIndex);
    const uint segmentBase = segmentIndex * 2u;

    DebugDrawVertex a = pc.vertexBuffer.vertices[segmentBase + 0u];
    DebugDrawVertex b = pc.vertexBuffer.vertices[segmentBase + 1u];

    vec4 clipA = pc.viewproj * vec4(a.positionDash.xyz, 1.0);
    vec4 clipB = pc.viewproj * vec4(b.positionDash.xyz, 1.0);
    if (!clip_segment_to_view(clipA, clipB))
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        outColor = vec4(0.0);
        outSide = 0.0;
        outEdgeSoftness = 1.0;
        outDashCoordPx = -1.0;
        return;
    }

    vec2 tangentPx;
    vec2 normalPx;
    stable_line_basis(clipA, clipB, tangentPx, normalPx);
    const int localVertex = gl_VertexIndex % 6;
    const int endpoint = kEndpointIndex[localVertex];
    const float side = kSideSign[localVertex];
    const float capDirection = endpoint == 0 ? -1.0 : 1.0;

    vec4 clipPosition = endpoint == 0 ? clipA : clipB;
    DebugDrawVertex source = endpoint == 0 ? a : b;

    const float halfWidthPx = max(pc.halfLineWidthPx, 0.5);
    // Square caps overlap adjacent segments by half a line width, hiding the
    // sub-pixel cracks produced by independently expanded curve segments.
    const vec2 offsetPx = normalPx * side + tangentPx * capDirection;
    const vec2 offsetNdc =
            offsetPx * pc.invViewportSizeNdc * halfWidthPx;
    clipPosition.xy += offsetNdc * clipPosition.w;

    gl_Position = clipPosition;
    outColor = source.color;
    outSide = side;
    outEdgeSoftness = clamp(
            1.0 - (max(pc.aaPx, 0.0) / halfWidthPx), 0.0, 1.0);
    outDashCoordPx = source.positionDash.w;
}
