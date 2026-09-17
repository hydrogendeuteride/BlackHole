#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 outColor;
layout(location = 1) noperspective out float outSide;
layout(location = 2) flat out float outEdgeSoftness;
layout(location = 3) noperspective out float outDashCoordPx;

struct DebugVertex
{
    vec4 clipPosition;
    vec4 color;
    float dashCoordPx;
};

layout(buffer_reference, std430) readonly buffer DebugVertexBuffer
{
    DebugVertex vertices[];
};

layout(push_constant) uniform DebugPush
{
    layout(offset = 0) mat4 viewproj;
    layout(offset = 64) DebugVertexBuffer vertexBuffer;
    layout(offset = 72) vec2 invViewportSizeNdc;
    layout(offset = 80) float halfLineWidthPx;
    layout(offset = 84) float aaPx;
} pc;

#include "debug/line_raster_common.glsl"

void main()
{
    const uint segmentIndex = uint(gl_InstanceIndex);
    const uint segmentBase = segmentIndex * 2u;

    DebugVertex a = pc.vertexBuffer.vertices[segmentBase + 0u];
    DebugVertex b = pc.vertexBuffer.vertices[segmentBase + 1u];

    vec4 clipA = a.clipPosition;
    vec4 clipB = b.clipPosition;

    vec2 tangentPx;
    vec2 normalPx;
    stable_line_basis(clipA, clipB, tangentPx, normalPx);

    const int localVertex = gl_VertexIndex % 6;
    const int endpoint = kEndpointIndex[localVertex];
    const float side = kSideSign[localVertex];
    const float capDirection = endpoint == 0 ? -1.0 : 1.0;

    vec4 clipPos = endpoint == 0 ? clipA : clipB;
    DebugVertex src = endpoint == 0 ? a : b;

    const float halfWidthPx = max(pc.halfLineWidthPx, 0.5);
    // Square caps overlap adjacent segments by half a line width, hiding the
    // sub-pixel cracks produced by independently expanded curve segments.
    const vec2 offsetPx = normalPx * side + tangentPx * capDirection;
    const vec2 offsetNdc =
            offsetPx * pc.invViewportSizeNdc * halfWidthPx;
    clipPos.xy += offsetNdc * clipPos.w;

    gl_Position = clipPos;
    outColor = src.color;
    outSide = side;
    outEdgeSoftness = clamp(1.0 - (max(pc.aaPx, 0.0) / halfWidthPx), 0.0, 1.0);
    outDashCoordPx = src.dashCoordPx;
}
