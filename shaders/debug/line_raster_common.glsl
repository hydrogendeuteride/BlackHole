#ifndef DEBUG_LINE_RASTER_COMMON_GLSL
#define DEBUG_LINE_RASTER_COMMON_GLSL

const int kEndpointIndex[6] = int[6](0, 0, 1, 1, 0, 1);
const float kSideSign[6] = float[6](-1.0, 1.0, -1.0, -1.0, 1.0, 1.0);
const float kDirectionEpsilon = 1.0e-6;

vec2 safe_ndc_xy(vec4 clipPosition)
{
    return clipPosition.xy / max(clipPosition.w, kDirectionEpsilon);
}

void stable_line_basis(vec4 clipA,
                       vec4 clipB,
                       out vec2 tangentPx,
                       out vec2 normalPx)
{
    const vec2 directionNdc = safe_ndc_xy(clipB) - safe_ndc_xy(clipA);
    const vec2 viewportScale = max(
            pc.invViewportSizeNdc, vec2(kDirectionEpsilon));
    const vec2 directionPx = directionNdc / viewportScale;
    const float directionLengthPx = length(directionPx);
    tangentPx = directionLengthPx > kDirectionEpsilon
            ? directionPx / directionLengthPx
            : vec2(1.0, 0.0);
    normalPx = vec2(-tangentPx.y, tangentPx.x);
}

#endif
