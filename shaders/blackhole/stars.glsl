// HYG J2000 equatorial sky: +Y north, +X RA 0h, +Z RA 6h.
struct Star { vec4 direction; vec4 photometry; };
layout(std430, set = 0, binding = 4) readonly buffer Stars { Star stars[]; };
layout(std430, set = 0, binding = 5) readonly buffer StarCells { uint cells[]; };

vec3 star_color(float bv)
{
    // Ballesteros (2012), https://arxiv.org/abs/1201.1809.
    // B-V estimates a blackbody temperature, not a full stellar spectrum.
    bv = clamp(bv, -0.4, 2.0);
    float temperature = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62));
    vec4 emitted = blackbody_sample(temperature);
    vec4 observed = blackbody_sample(temperature * blackhole.star_view.z);
    // Keep catalogue magnitude as the unshifted flux reference. The LUT's
    // luminance ratio adds spectral brightening without a second g^3 factor.
    return observed.rgb * exp(observed.w - emitted.w);
}

vec3 star_background(vec3 dir)
{
    float angle = blackhole.star_view.x;
    dir = vec3(cos(angle) * dir.x - sin(angle) * dir.z, dir.y,
               sin(angle) * dir.x + cos(angle) * dir.z);
    vec2 uv = vec2(fract(atan(dir.z, dir.x) / 6.28318530718 + 0.5),
                   acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359);
    ivec2 cell = clamp(ivec2(uv * vec2(256, 128)), ivec2(0), ivec2(255, 127));
    uint header = uint(cell.y * 256 + cell.x) * 2u;
    uint offset = cells[header];
    uint count = cells[header + 1u];
    float sigma = radians(blackhole.star_params.z);
    // Approximate a pixel footprint to reduce subpixel scintillation. The
    // lensing Jacobian is not included yet; near-critical arcs need more AA.
    float variance = sigma * sigma + blackhole.star_view.y * blackhole.star_view.y / 12.0;
    vec3 color = vec3(0);
    for (uint i = 0u; i < count; ++i)
    {
        Star star = stars[cells[offset + i]];
        if (star.photometry.x > blackhole.star_params.w) continue;
        if (dot(dir, star.direction.xyz) < 0.99984769516) continue; // 1 degree
        float separation = asin(clamp(length(cross(dir, star.direction.xyz)), 0.0, 1.0));
        float profile = exp(-0.5 * separation * separation / variance);
        float flux = pow(10.0, -0.4 * (star.photometry.x - 3.0));
        // Keep the flux reference small too: shrinking sigma alone would
        // concentrate the old broad footprint into an overexposed core.
        float reference = radians(0.012);
        color += star_color(star.photometry.y) * flux * profile * reference * reference / variance;
    }
    return color * blackhole.star_params.y;
}
