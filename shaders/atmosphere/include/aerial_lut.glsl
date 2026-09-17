// Aerial perspective froxel LUT stored as a 3D texture:
// AERIAL_LUT_RES x AERIAL_LUT_RES view directions, AERIAL_LUT_SLICES depth slices.
// Each column integrates over the ray's atmosphere span [tBegin, tBegin + tSpan]
// remapped with a squared distribution: t(w) = tBegin + tSpan * w * w.
// Requires ray_sphere_intersect (constants.glsl) to be included first.

const float AERIAL_LUT_RES = 64.0;
const float AERIAL_LUT_SLICES = 32.0;

bool aerial_lut_span(vec3 camLocal,
                     vec3 rd,
                     vec3 center,
                     float planetRadius,
                     float atmRadius,
                     out float tBegin,
                     out float tSpan)
{
    tBegin = 0.0;
    tSpan = 0.0;

    float tAtm0;
    float tAtm1;
    if (!ray_sphere_intersect(camLocal, rd, center, atmRadius, tAtm0, tAtm1) || tAtm1 <= 0.0)
    {
        return false;
    }

    tBegin = max(tAtm0, 0.0);
    float tEnd = tAtm1;

    float tPlanet0;
    float tPlanet1;
    if (ray_sphere_intersect(camLocal, rd, center, planetRadius, tPlanet0, tPlanet1))
    {
        float tGround = (tPlanet0 > 0.0) ? tPlanet0 : tPlanet1;
        if (tGround > 0.0)
        {
            tEnd = min(tEnd, tGround);
        }
    }

    tSpan = tEnd - tBegin;
    return tSpan > 0.0;
}

float aerial_lut_slice_t(float tBegin, float tSpan, float w)
{
    return tBegin + tSpan * w * w;
}

float aerial_lut_w(float t, float tBegin, float tSpan)
{
    return sqrt(clamp((t - tBegin) / max(tSpan, 1.0e-3), 0.0, 1.0));
}

vec4 aerial_lut_sample(sampler3D volume, vec2 uv, float w)
{
    float sliceF = clamp(w, 0.0, 1.0) * AERIAL_LUT_SLICES - 0.5;
    float slice0 = floor(sliceF);
    float f = clamp(sliceF - slice0, 0.0, 1.0);
    float slice1 = min(slice0 + 1.0, AERIAL_LUT_SLICES - 1.0);

    vec2 texel = clamp(uv * AERIAL_LUT_RES, vec2(0.5), vec2(AERIAL_LUT_RES - 0.5)) / AERIAL_LUT_RES;
    float invSlices = 1.0 / AERIAL_LUT_SLICES;

    vec4 hi = textureLod(volume, vec3(texel, (slice1 + 0.5) * invSlices), 0.0);
    vec4 lo = (slice0 < 0.0)
        ? vec4(0.0)
        : textureLod(volume, vec3(texel, (slice0 + 0.5) * invSlices), 0.0);
    return mix(lo, hi, f);
}
