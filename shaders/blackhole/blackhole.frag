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
    vec4 disk_params; // enabled, inner radius (rs), outer radius (rs), brightness
    vec4 disk_style; // pattern contrast, clouds, animation time, height (rs)
    vec4 disk_optics; // inner temperature (K), absorption (1/rs), emission scale
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

bool mesh_hit(vec3 a, vec3 b, out vec3 color, out float fraction)
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
                fraction = clamp(length(hi - a) / max(length(b - a), 1e-7), 0.0, 1.0);
                return true;
            }
        }
        previous = p;
    }
    return false;
}

// Two-sided, opaque emitter in the black hole's equatorial plane.
bool disk_hit(vec3 a, vec3 b, out vec3 hit)
{
    if (blackhole.disk_params.x < 0.5 || blackhole.disk_style.y > 0.5 || abs(b.y - a.y) < 1e-7) return false;
    float t = (blackhole.center_radius.y - a.y) / (b.y - a.y);
    if (t <= 0.0 || t > 1.0) return false;
    hit = mix(a, b, t);
    float r = length(hit.xz - blackhole.center_radius.xz) / blackhole.center_radius.w;
    return r >= blackhole.disk_params.y && r <= blackhole.disk_params.z;
}

float disk_hash(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float disk_noise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(disk_hash(cell), disk_hash(cell + vec3(1, 0, 0)), f.x),
                   mix(disk_hash(cell + vec3(0, 1, 0)), disk_hash(cell + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(disk_hash(cell + vec3(0, 0, 1)), disk_hash(cell + vec3(1, 0, 1)), f.x),
                   mix(disk_hash(cell + vec3(0, 1, 1)), disk_hash(cell + vec3(1, 1, 1)), f.x), f.y), f.z);
}

#include "blackhole/disk.glsl"

vec3 disk_color(vec3 hit)
{
    vec2 p = (hit.xz - blackhole.center_radius.xz) / blackhole.center_radius.w;
    float radius = (length(p) - blackhole.disk_params.y) /
                   max(blackhole.disk_params.z - blackhole.disk_params.y, 0.001);
    float angle = atan(p.y, p.x);
    // Static polar checks keep repeated images recognizable without shear.
    // Avoid screen derivatives here: hits occur in divergent ray-march iterations.
    float checks = smoothstep(-0.2, 0.2, sin(radius * 6.0 * 3.14159265) * cos(angle * 12.0));
    vec3 pattern = mix(vec3(0.12, 0.035, 0.012), vec3(1.0, 0.65, 0.25), checks);
    return pattern * blackhole.disk_params.w;
}

bool scene_hit(vec3 a, vec3 b, out vec3 color)
{
    vec3 hit;
    bool disk = disk_hit(a, b, hit);
    // Only search for meshes in front of the opaque disk crossing.
    float fraction;
    vec3 end = disk ? hit : b;
    bool mesh = mesh_hit(a, end, color, fraction);
    integrate_disk(a, mesh ? mix(a, end, fraction) : end);
    if (mesh) return true;
    if (disk_trans < 0.001) { color = vec3(0); return true; }
    if (disk) { color = disk_color(hit); return true; }
    return false;
}

void main()
{
    vec2 ndc = inUV * 2.0 - 1.0;
    vec3 dir = transpose(mat3(blackhole.view)) * normalize(vec3(ndc / vec2(blackhole.proj[0][0], blackhole.proj[1][1]), -1.0));
    if (blackhole.params.w < 0.5)
    {
        if (blackhole.disk_params.x > 0.5 && blackhole.disk_style.y > 0.5)
        {
            float depth = textureLod(depth_tex, inUV, 0.0).r;
            float distance = length(blackhole.camera.xyz - blackhole.center_radius.xyz) +
                             blackhole.disk_params.z * blackhole.center_radius.w * 2.0;
            if (depth > 0.0)
            {
                float surface_z = -blackhole.proj[3][2] / (depth + blackhole.proj[2][2]);
                distance = surface_z / (mat3(blackhole.view) * dir).z;
            }
            integrate_disk(blackhole.camera.xyz, blackhole.camera.xyz + dir * distance);
            vec3 base = depth > 0.0 ? textureLod(color_tex, inUV, 0.0).rgb : background(dir);
            outColor = disk_result(base);
            return;
        }
        vec3 hit;
        if (disk_hit(blackhole.camera.xyz, blackhole.camera.xyz + dir *
                     (length(blackhole.camera.xyz - blackhole.center_radius.xyz) +
                      blackhole.disk_params.z * blackhole.center_radius.w) * 2.0, hit))
        {
            vec2 uv;
            float gap;
            if (!screen_depth((blackhole.view * vec4(hit, 1.0)).xyz, uv, gap) || gap < 0.0)
            {
                outColor = vec4(disk_color(hit), 1);
                return;
            }
        }
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
    if (r <= 1.001) { outColor = disk_result(vec3(0)); return; }
    vec3 n = pos / r;
    float radial = dot(dir, n);
    vec3 tangent = dir - radial * n;
    float tlen = length(tangent);
    vec3 color;
    if (tlen < 1e-5)
    {
        float distance = radial < 0.0 ? (r - 1.0) * rs : 200.0 * rs;
        if (scene_hit(blackhole.camera.xyz, blackhole.camera.xyz + dir * distance, color)) outColor = disk_result(color);
        else outColor = disk_result(radial < 0.0 ? vec3(0) : background(dir));
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
        if (u <= 0.0) { outColor = disk_result(background(dir)); return; }
        vec3 basis = cos(phi) * n + sin(phi) * tangent;
        vec3 next = basis / max(u, 1.0e-6);
        if (u >= 1.0)
        {
            // Stop the mesh segment at the horizon, not inside it.
            float fraction = clamp((1.0 - old_u) / max(u - old_u, 1e-6), 0.0, 1.0);
            float horizon_phi = phi - dt + dt * fraction;
            next = cos(horizon_phi) * n + sin(horizon_phi) * tangent;
        }
        if (scene_hit(center + pos * rs, center + next * rs, color))
        {
            outColor = disk_result(color);
            return;
        }
        if (u >= 1.0) { outColor = disk_result(vec3(0)); return; }
        // Analytic tangent is more stable than differencing two distant positions.
        dir = normalize(-du * basis + u * (-sin(phi) * n + cos(phi) * tangent));
        pos = next;
        if (u < escape && du < 0.0)
        {
            outColor = disk_result(background(dir));
            return;
        }
    }
    // Unresolved near-critical paths are not assumed to have escaped.
    outColor = disk_result(vec3(0));
}
