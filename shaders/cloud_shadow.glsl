#ifdef CLOUD_SHADOW_OCEAN
layout(set = 2, binding = 1) uniform sampler2D cloudShadowTex;
layout(set = 2, binding = 3, std140) uniform CloudShadowUniform
#else
layout(set = 1, binding = 4) uniform sampler2D cloudShadowTex;
layout(push_constant) uniform CloudShadowPush
#endif
{
    vec4 origin;
    vec4 axis_x;
    vec4 axis_y;
    vec4 planet;
    vec4 layer;
} cloudShadow;

float cloud_shadow_visibility(vec3 pos)
{
    if (cloudShadow.axis_y.w <= 0.0 || cloudShadow.axis_x.w <= 0.0 ||
        any(isnan(pos)) || any(isinf(pos))) return 1.0;
    vec3 radial = pos - cloudShadow.planet.xyz;
    float radius = cloudShadow.planet.w;
    float distanceM = length(radial);
    if (isnan(distanceM) || isinf(distanceM) || distanceM <= 0.0 || radius <= 0.0) return 1.0;
    // Only the sun-facing shell was integrated; never project it onto the night side.
    vec3 sun = cross(cloudShadow.axis_x.xyz, cloudShadow.axis_y.xyz);
    if (dot(radial / distanceM, sun) <= 0.0) return 1.0;
    float height = distanceM - radius;
    float belowCloud = 1.0 - smoothstep(cloudShadow.layer.x,
                                      cloudShadow.layer.x + max(cloudShadow.layer.y, 1.0), height);
    if (belowCloud <= 0.0) return 1.0;
    vec3 offset = pos - cloudShadow.origin.xyz;
    vec2 uv = vec2(dot(offset, cloudShadow.axis_x.xyz), dot(offset, cloudShadow.axis_y.xyz)) /
              (2.0 * cloudShadow.axis_x.w) + 0.5;
    if (any(isnan(uv)) || any(isinf(uv))) return 1.0;
    float edge = max(abs(uv.x * 2.0 - 1.0), abs(uv.y * 2.0 - 1.0));
    if (edge >= 0.95) return 1.0;
    float visibility = textureLod(cloudShadowTex, uv, 0.0).r;
    if (isnan(visibility) || isinf(visibility)) return 1.0;
    float strength = clamp(cloudShadow.axis_y.w, 0.0, 2.0);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(strength, 1.0));
    float weight = belowCloud * min(strength, 1.0) * (1.0 - smoothstep(0.65, 0.95, edge));
    return mix(1.0, visibility, weight);
}
