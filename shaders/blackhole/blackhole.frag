#version 450
#extension GL_GOOGLE_include_directive : require
#include "blackhole/ray.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform BlackholeData
{
    mat4 view;
    mat4 proj;
    vec4 camera;
    vec4 center_radius;
    vec4 params; // angular step, mesh thickness, mesh lensing
    vec4 star_params; // enabled, brightness, angular sigma (degrees), magnitude limit
    vec4 star_view; // sky rotation (radians), pixel angle
} blackhole;
#include "blackhole/stars.glsl"
layout(set = 0, binding = 1) uniform sampler2D color_tex;
layout(set = 0, binding = 2) uniform sampler2D depth_tex;
layout(set = 0, binding = 3) uniform sampler2D sky_tex;

vec3 background(vec3 dir)
{
    dir = normalize(dir);
    if (blackhole.star_params.x > 0.5) return star_background(dir);
    vec2 uv = vec2(atan(dir.z, dir.x) * 0.15915494309 + 0.5,
                   acos(clamp(dir.y, -1.0, 1.0)) * 0.31830988618);
    return textureLod(sky_tex, uv, 0.0).rgb;
}

bool screen_depth(vec3 p, out vec2 uv, out float gap)
{
    vec4 clip = blackhole.proj * vec4(p, 1.0);
    if (clip.w <= 0.001) return false;
    uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return false;
    float depth = textureLod(depth_tex, uv, 0.0).r;
    if (depth <= 0.0) return false; // reversed-Z clear value
    float surface_z = -blackhole.proj[3][2] / (depth + blackhole.proj[2][2]);
    gap = surface_z - p.z;
    return true;
}

bool mesh_hit(vec3 a, vec3 b, out vec3 color)
{
    if (blackhole.params.z < 0.5) return false;
    a = (blackhole.view * vec4(a, 1.0)).xyz;
    b = (blackhole.view * vec4(b, 1.0)).xyz;
    vec3 previous = a;
    // Subdivide each curved-path segment before testing the depth surface.
    for (int j = 1; j <= 4; ++j)
    {
        vec3 p = mix(a, b, float(j) * 0.25);
        vec2 uv;
        float gap;
        if (screen_depth(p, uv, gap) && gap >= 0.0 &&
            gap <= blackhole.params.y + abs(p.z - previous.z))
        {
            vec3 lo = previous;
            vec3 hi = p;
            for (int k = 0; k < 5; ++k)
            {
                vec3 mid = (lo + hi) * 0.5;
                vec2 mid_uv;
                float mid_gap;
                if (screen_depth(mid, mid_uv, mid_gap) && mid_gap >= 0.0) hi = mid;
                else lo = mid;
            }
            if (screen_depth(hi, uv, gap) && gap >= 0.0 && gap <= blackhole.params.y)
            {
                color = textureLod(color_tex, uv, 0.0).rgb;
                return true;
            }
        }
        previous = p;
    }
    return false;
}

void main()
{
    vec2 ndc = inUV * 2.0 - 1.0;
    vec3 dir = transpose(mat3(blackhole.view)) * normalize(vec3(ndc / vec2(blackhole.proj[0][0], blackhole.proj[1][1]), -1.0));
    if (blackhole.params.w < 0.5)
    {
        outColor = textureLod(depth_tex, inUV, 0.0).r > 0.0 ? textureLod(color_tex, inUV, 0.0)
                                                          : vec4(background(dir), 1.0);
        return;
    }
    if (blackhole.params.z < 0.5 && textureLod(depth_tex, inUV, 0.0).r > 0.0)
    {
        outColor = textureLod(color_tex, inUV, 0.0);
        return;
    }
    vec3 center = blackhole.center_radius.xyz;
    float rs = blackhole.center_radius.w;
    vec3 pos = (blackhole.camera.xyz - center) / rs;
    float r = length(pos);
    if (r <= 1.001) { outColor = vec4(0, 0, 0, 1); return; }
    vec3 n = pos / r;
    float radial = dot(dir, n);
    vec3 tangent = dir - radial * n;
    float tlen = length(tangent);
    vec3 color;
    if (tlen < 1e-5)
    {
        float distance = radial < 0.0 ? (r - 1.0) * rs : 200.0 * rs;
        if (mesh_hit(blackhole.camera.xyz, blackhole.camera.xyz + dir * distance, color)) outColor = vec4(color, 1);
        else outColor = vec4(radial < 0.0 ? vec3(0) : background(dir), 1);
        return;
    }
    tangent /= tlen;
    float u = 1.0 / r;
    float du = -radial / tlen * u;
    float phi = 0.0;
    float escape = 1.0 / max(100.0, r * 2.0);
    for (int i = 0; i < 768; ++i)
    {
        // Limit both angular change and relative radial travel; avoid long mesh-skipping segments.
        float dt = min(blackhole.params.x, 0.08 * u / max(length(vec2(u, du)), 1e-6));
        float old_u = u;
        step_ray(u, du, dt);
        phi += dt;
        if (u <= 0.0) { outColor = vec4(background(dir), 1); return; }
        vec3 basis = cos(phi) * n + sin(phi) * tangent;
        vec3 next = basis / max(u, 1.0e-6);
        if (u >= 1.0)
        {
            // Stop the mesh segment at the horizon, not inside it.
            float fraction = clamp((1.0 - old_u) / max(u - old_u, 1e-6), 0.0, 1.0);
            float horizon_phi = phi - dt + dt * fraction;
            next = cos(horizon_phi) * n + sin(horizon_phi) * tangent;
        }
        if (mesh_hit(center + pos * rs, center + next * rs, color))
        {
            outColor = vec4(color, 1);
            return;
        }
        if (u >= 1.0) { outColor = vec4(0, 0, 0, 1); return; }
        // Analytic tangent is more stable than differencing two distant positions.
        dir = normalize(-du * basis + u * (-sin(phi) * n + cos(phi) * tangent));
        pos = next;
        if (u < escape && du < 0.0)
        {
            outColor = vec4(background(dir), 1);
            return;
        }
    }
    // Unresolved near-critical paths are not assumed to have escaped.
    outColor = vec4(0, 0, 0, 1);
}
