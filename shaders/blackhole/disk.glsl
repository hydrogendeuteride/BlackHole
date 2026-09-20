// Volume model based on the project's original BlackHoleRayTracer shader:
// https://github.com/hydrogendeuteride/BlackHoleRayTracer/blob/master/shader/blackhole.comp
// Smooth surface density, vertical falloff, orbiting turbulence and absorption.
vec3 disk_light = vec3(0.0);
float disk_trans = 1.0;

vec4 disk_result(vec3 behind)
{
    return vec4(disk_light + disk_trans * behind, 1.0);
}

vec4 disk_field(vec3 p, float age)
{
    float r = length(p.xz);
    float angle = atan(p.z, p.x);
    float time = blackhole.disk_style.z;
    float omega = 8.0 * sqrt(0.5 / (r * r * r));
    float anchor = blackhole.disk_params.y * 1.6;
    float drift = 8.0 * sqrt(0.5 / (anchor * anchor * anchor));
    // Only the common rotation uses absolute time. Differential shear has a
    // bounded age, so a long-running scene cannot wind into finer and finer rings.
    float phi = angle - time * drift - age * (omega - drift);
    float radial = 12.0 * log(r / blackhole.disk_params.y);
    vec3 flow = vec3(2.5 * cos(phi), 2.5 * sin(phi), radial);
    float warp = disk_noise(flow * 0.7 + vec3(5.2, 1.3, 0.0)) - 0.5;
    // Narrow radial features and longer azimuthal features form orbiting wisps.
    phi += warp * 0.12;
    vec3 q = vec3(1.8 * cos(phi), 1.8 * sin(phi), radial + warp * 0.2);
    q.z += p.y * 2.0;
    float broad = disk_noise(q);
    vec3 detail_pos = q * 2.7 + vec3(17.1, 9.2, 13.7);
    detail_pos.z += p.y * 6.0 + age * 0.08;
    float detail = disk_noise(detail_pos) * 0.67 + disk_noise(detail_pos * 2.03) * 0.33;
    // Warped noise contours form uneven arcs; a separate mask breaks them
    // into lengths instead of drawing complete concentric rings.
    vec3 strand_pos = vec3(1.2 * cos(phi), 1.2 * sin(phi), radial * 1.65 + warp * 1.5);
    float strand = 1.0 - abs(2.0 * disk_noise(strand_pos) - 1.0);
    float gap = 1.0 - abs(2.0 * disk_noise(strand_pos + vec3(8.3, 2.8, 5.1)) - 1.0);
    float mask = disk_noise(vec3(3.5 * cos(phi), 3.5 * sin(phi), radial * 0.4 + 7.2));
    float bright = smoothstep(0.72, 0.96, strand) * smoothstep(0.35, 0.65, mask);
    float dark = smoothstep(0.8, 0.98, gap) * (1.0 - smoothstep(0.4, 0.7, mask));
    return vec4(broad, detail, bright, dark);
}

vec4 disk_turbulence(vec3 p)
{
    // Staggered 8-second lifetimes. Each field resets with zero weight and
    // zero blend slope, including when time runs backwards.
    float age = mod(blackhole.disk_style.z, 8.0) - 4.0;
    float other = mod(blackhole.disk_style.z + 4.0, 8.0) - 4.0;
    float weight = 0.5 + 0.5 * cos(age * (6.28318530718 / 8.0));
    return mix(disk_field(p, other), disk_field(p, age), weight);
}

float disk_hotspots(vec3 p)
{
    float r = length(p.xz);
    float angle = atan(p.z, p.x);
    float time = blackhole.disk_style.z;
    float inner = blackhole.disk_params.y;
    float hot = 0.0;
    for (int i = 0; i < 2; ++i)
    {
        float slot = float(i);
        float center = inner * (1.3 + slot * 0.55);
        float orbit = time * 8.0 * sqrt(0.5 / (center * center * center));
        float phase = angle - orbit - slot * 2.7 + (r - center) * 1.4;
        float along = atan(sin(phase), cos(phase)) / (0.5 + slot * 0.15);
        float across = (r - center) / (inner * 0.09);
        float life = sin(3.14159265359 * fract(time / (18.0 + slot * 7.0) + 0.3 + slot * 0.37));
        // Smooth birth and decay, stretched along the orbit, with bounded shape.
        hot += exp(-0.5 * (across * across + along * along)) * pow(life, 4.0);
    }
    return hot;
}

float disk_shift(vec3 p, vec3 back_dir)
{
    if (blackhole.disk_effects.x < 0.5 && blackhole.disk_effects.y < 0.5) return 1.0;
    float radius = max(length(p), 1.001);
    float lapse = 1.0 - 1.0 / radius;
    float shift = 1.0;
    if (blackhole.disk_effects.x > 0.5)
    {
        // Static observer at the camera's actual radius, not at infinity.
        float observer = length(blackhole.camera.xyz - blackhole.center_radius.xyz) / blackhole.center_radius.w;
        float observer_lapse = 1.0 - 1.0 / max(observer, 1.001);
        shift *= sqrt(lapse / observer_lapse);
    }
    if (blackhole.disk_effects.y > 0.5)
    {
        vec3 radial = p / radius;
        // Convert coordinate tangent to the local static orthonormal frame.
        // Tracing runs camera -> disk; physical photons travel the other way.
        float dr = dot(back_dir, radial);
        vec3 photon = -normalize(back_dir + radial * dr * (inversesqrt(lapse) - 1.0));
        float orbit_radius = max(length(p.xz), 3.0);
        vec3 azimuth = vec3(-p.z, 0.0, p.x) / max(length(p.xz), 1e-6);
        // Schwarzschild circular orbit: beta = r*Omega/sqrt(1-rs/r).
        // Extend the equatorial velocity through our thin procedural volume.
        float beta = sqrt(0.5 / (orbit_radius - 1.0));
        vec3 velocity = azimuth * (beta * blackhole.disk_effects.w);
        shift *= sqrt(1.0 - beta * beta) / (1.0 - dot(velocity, photon));
    }
    return shift;
}

void sample_disk(vec3 p, vec3 back_dir, float ds)
{
    float inner = blackhole.disk_params.y;
    float outer = blackhole.disk_params.z;
    float r = length(p.xz);
    if (r <= inner || r >= outer) return;
    float height = blackhole.disk_style.w * mix(0.9, 1.25, smoothstep(inner, outer, r));
    height = max(height, 0.001);
    // Skip empty volume before evaluating the noise layers.
    if (abs(p.y) > height * 4.0) return;
    float vertical = p.y / height;
    float contrast = blackhole.disk_style.x;
    // Use relative height for noise as well, so thickness does not change the
    // column's pattern or integrated optical depth.
    vec3 noise_pos = vec3(p.x, vertical * 0.08, p.z);
    vec4 gas = contrast > 0.0 ? disk_turbulence(noise_pos) : vec4(0.5, 0.5, 0.0, 0.0);
    float clumps = gas.x;
    float detail = gas.y;
    // A phenomenological surface-density law, not a solved alpha-disk model.
    // Sigma has a reference column of 1 rs at the inner radius. Normalize the
    // vertical Gaussian so changing H preserves Sigma and thus optical depth.
    float surface = pow(inner / r, 0.6);
    surface *= smoothstep(inner, inner * 1.05, r) * (1.0 - smoothstep(outer * 0.8, outer, r));
    float density = surface * (0.39894228 / height) * exp(-0.5 * vertical * vertical);
    density *= 1.0 - smoothstep(3.0, 4.0, abs(vertical));
    // Preserve a dense continuous body, with stronger structure in its skin.
    float structure = contrast * mix(0.25, 1.0, smoothstep(0.75, 2.0, abs(vertical)));
    float cloud = clamp(0.7 * clumps + 0.3 * detail, 0.0, 1.0);
    density *= mix(1.0, mix(0.45, 1.55, cloud), structure);
    density *= 1.0 + structure * (0.65 * gas.z - 0.78 * gas.w);
    if (density * height < 0.00001) return;

    // Original thin-disk approximation: T(r) = T_inner * (r_inner/r)^(3/4).
    // Small local temperature variations affect both chromaticity and radiance.
    float temperature = blackhole.disk_optics.x * pow(inner / r, 0.75);
    temperature *= mix(1.0, mix(0.94, 1.06, clumps), contrast);
    float hot = contrast > 0.0 ? disk_hotspots(p) : 0.0;
    temperature *= 1.0 + contrast * (0.035 * gas.z - 0.035 * gas.w + 0.09 * hot);
    float shift = disk_shift(p, back_dir);
    vec4 thermal = blackbody_sample(temperature * shift);
    // B_nu(g*T) already includes g^3 B_(nu/g)(T). Do not boost it twice.
    // Disabling brightness is an explanatory color-only view, not full transfer.
    if (blackhole.disk_effects.z < 0.5) thermal.w = blackbody_sample(temperature).w;
    float reference = blackbody_sample(blackhole.disk_optics.x).w;
    // Normalize once to the chosen inner temperature. Preserve relative visible
    // radiance across the disk; emission is a separate scene-exposure scale.
    vec3 source = thermal.rgb * exp(thermal.w - reference) * blackhole.disk_optics.z;
    float trans = exp(-blackhole.disk_optics.y * density * ds);
    disk_light += disk_trans * (1.0 - trans) * source;
    disk_trans *= trans;
}

void integrate_disk(vec3 a, vec3 b)
{
    if (blackhole.disk_params.x < 0.5 || blackhole.disk_style.y < 0.5) return;
    a = (a - blackhole.center_radius.xyz) / blackhole.center_radius.w;
    b = (b - blackhole.center_radius.xyz) / blackhole.center_radius.w;
    vec3 delta = b - a;
    float distance = length(delta);
    if (distance < 1e-7) return;
    vec3 dir = delta / distance;
    float height = blackhole.disk_style.w * 1.25 * 4.0;
    float bound = length(vec2(blackhole.disk_params.z, height));
    float along = dot(a, dir);
    float discriminant = along * along - dot(a, a) + bound * bound;
    if (discriminant <= 0.0) return;
    float near = max(0.0, -along - sqrt(discriminant));
    float far = min(distance, -along + sqrt(discriminant));
    // Clip every ray segment to the volume, including long straight rays and
    // segments crossing the entire thin disk between geodesic steps.
    if (abs(dir.y) < 1e-7)
    {
        if (abs(a.y) > height) return;
    }
    else
    {
        vec2 slab = (vec2(-height, height) - a.y) / dir.y;
        near = max(near, min(slab.x, slab.y));
        far = min(far, max(slab.x, slab.y));
    }
    if (far <= near) return;
    int steps = clamp(int(ceil((far - near) / min(0.06, blackhole.disk_style.w))), 1, 1024);
    float ds = (far - near) / float(steps);
    for (int i = 0; i < steps && disk_trans > 0.001; ++i)
        sample_disk(a + dir * (near + (float(i) + 0.5) * ds), dir, ds);
}
