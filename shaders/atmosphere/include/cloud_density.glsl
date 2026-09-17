vec2 sample_cloud_weather_grad(vec2 uvE, vec2 uvDx, vec2 uvDy, float scale, vec2 offset)
{
    vec2 uv = uvE * scale + offset;
    return textureGrad(cloudNoiseTex, fract(uv), uvDx * scale, uvDy * scale).rg;
}

float sample_cloud_noise_grad_aniso(vec2 uvE, vec2 uvDx, vec2 uvDy, vec2 scale, vec2 offset)
{
    vec2 uv = uvE * scale + offset;
    return textureGrad(cloudNoiseTex, fract(uv), uvDx * scale, uvDy * scale).r;
}

float sample_cloud_noise_3d_grad(vec3 p, vec3 dpdx, vec3 dpdy, vec3 offset)
{
    return textureGrad(cloudNoiseTex3D, fract(p + offset), dpdx, dpdy).r;
}

float cloud_noise_lod_2d(vec2 dpdx, vec2 dpdy)
{
    vec2 size = vec2(textureSize(cloudNoiseTex, 0));
    float footprint = max(length(dpdx * size), length(dpdy * size));
    return max(log2(max(footprint, 1.0)), 0.0);
}

float cloud_noise_lod_3d(vec3 dpdx, vec3 dpdy)
{
    vec3 size = vec3(textureSize(cloudNoiseTex3D, 0));
    float footprint = max(length(dpdx * size), length(dpdy * size));
    return max(log2(max(footprint, 1.0)), 0.0);
}

vec2 cloud_evolution_orbit(float periodS, float radius, float direction)
{
    if (periodS <= 1e-3) return vec2(0.0);

    float timeS = max(sceneData.timeParams.x, 0.0);
    float phase = 2.0 * PI * (mod(timeS, periodS) / periodS);
    return radius * vec2(cos(phase) - 1.0, direction * sin(phase));
}

vec4 resolve_cloud_evolution_offsets()
{
    vec2 weather = cloud_evolution_orbit(pc.cloud_animation.x, 0.05, 1.0);
    vec2 detail = cloud_evolution_orbit(pc.cloud_animation.y, 0.03, -1.0);
    return vec4(weather, detail);
}

void build_local_cloud_frame(vec3 dir, out vec3 east, out vec3 north)
{
    vec3 refAxis = mix(vec3(0.0, 1.0, 0.0),
                       vec3(1.0, 0.0, 0.0),
                       step(0.999, abs(dir.y)));
    east = normalize(cross(refAxis, dir));
    north = cross(dir, east);
}

vec3 differentiate_normalized(vec3 v, vec3 dv)
{
    float vLen2 = max(dot(v, v), 1e-8);
    float invLen = inversesqrt(vLen2);
    vec3 n = v * invLen;
    return (dv - n * dot(n, dv)) * invLen;
}

void dir_to_equirect_grad(vec3 d,
                          vec3 ddx,
                          vec3 ddy,
                          bool flipV,
                          out vec2 uv,
                          out vec2 uvDx,
                          out vec2 uvDy)
{
    d = normalize(d);
    uv = dir_to_equirect(d, flipV);

    float xz2 = max(dot(d.xz, d.xz), 1e-6);
    float invXZ2 = 1.0 / xz2;
    float invSqrtOneMinusY2 = inversesqrt(max(1.0 - d.y * d.y, 1e-6));

    uvDx.x = (d.x * ddx.z - d.z * ddx.x) * invXZ2 * INV_TWO_PI;
    uvDy.x = (d.x * ddy.z - d.z * ddy.x) * invXZ2 * INV_TWO_PI;
    uvDx.y = -ddx.y * invSqrtOneMinusY2 * INV_PI;
    uvDy.y = -ddy.y * invSqrtOneMinusY2 * INV_PI;

    if (flipV)
    {
        uvDx.y = -uvDx.y;
        uvDy.y = -uvDy.y;
    }
}

#include "atmosphere/include/cloud_profile.glsl"

struct CloudDetailSample
{
    float column;
    float erodeNoise;
    float erodeStrength;
};

float cloud_column(float sliceU, float sliceV)
{
    sliceU = clamp((sliceU - 0.5) * 1.55 + 0.5, 0.0, 1.0);
    sliceV = clamp((sliceV - 0.5) * 1.55 + 0.5, 0.0, 1.0);
    return smoothstep(0.18, 0.82, mix(sliceU, sliceV, 0.45));
}

CloudDetailSample sample_detail_3d(vec3 dirW,
                                   vec3 dirWdx,
                                   vec3 dirWdy,
                                   float height,
                                   float heightDx,
                                   float heightDy,
                                   vec2 shearUv,
                                   float detailScale,
                                   float detailErode,
                                   vec2 evolution,
                                   bool shadowSample)
{
    CloudDetailSample result;
    result.erodeNoise = 0.5;
    result.erodeStrength = CLOUD_EROSION_STRENGTH * detailErode;

    float frequencyScale = clamp(detailScale / CLOUD_DETAIL_REFERENCE, 0.125, 8.0);
    float radiusM = max(pc.planet_center_radius.w + height, 1.0);
    vec3 metricP = dirW * radiusM;
    vec3 metricDx = dirWdx * radiusM + dirW * heightDx;
    vec3 metricDy = dirWdy * radiusM + dirW * heightDy;

    float shapeTileM = CLOUD_SHAPE_TILE_M / frequencyScale;
    vec3 shapeP = metricP / shapeTileM + vec3(shearUv.x, 0.0, shearUv.y);
    vec3 evolutionA = vec3(evolution, 0.35 * (evolution.x + evolution.y));
    float sliceU = sample_cloud_noise_3d_grad(shapeP,
                                               metricDx / shapeTileM,
                                               metricDy / shapeTileM,
                                               vec3(0.619, 0.281, 0.113) + evolutionA);

    vec3 metricRot = vec3(metricP.z, metricP.x, -metricP.y);
    vec3 metricRotDx = vec3(metricDx.z, metricDx.x, -metricDx.y);
    vec3 metricRotDy = vec3(metricDy.z, metricDy.x, -metricDy.y);
    float shapeTileB = shapeTileM * 0.79;
    vec3 shapePB = metricRot / shapeTileB + vec3(-shearUv.y, 0.0, shearUv.x);
    vec3 evolutionB = vec3(-evolution.y, -evolution.x, 0.40 * (evolution.x - evolution.y));
    float sliceV = sample_cloud_noise_3d_grad(shapePB,
                                               metricRotDx / shapeTileB,
                                               metricRotDy / shapeTileB,
                                               vec3(0.173, 0.547, 0.763) + evolutionB);

    result.column = cloud_column(sliceU, sliceV);
    if (result.column <= 0.0 || result.erodeStrength <= 1e-3) return result;

    float detailTileM = CLOUD_DETAIL_TILE_M / frequencyScale;
    float microTileM = CLOUD_MICRO_TILE_M / frequencyScale;
    vec3 mediumDx = metricDx / detailTileM;
    vec3 mediumDy = metricDy / detailTileM;
    float mediumLod = cloud_noise_lod_3d(mediumDx, mediumDy);
    float detailWeight = 1.0 - smoothstep(CLOUD_DETAIL_LOD_FADE_START,
                                          CLOUD_DETAIL_LOD_FADE_END,
                                          mediumLod);
    result.erodeStrength *= detailWeight;
    if (result.erodeStrength <= 1e-3) return result;

    float medium = sample_cloud_noise_3d_grad(metricP / detailTileM,
                                               mediumDx,
                                               mediumDy,
                                               vec3(0.377, 0.113, 0.531) + evolutionA * 1.35);
    vec3 microP = vec3(metricP.y, -metricP.z, metricP.x) / microTileM;
    vec3 microDx = vec3(metricDx.y, -metricDx.z, metricDx.x) / microTileM;
    vec3 microDy = vec3(metricDy.y, -metricDy.z, metricDy.x) / microTileM;
    float microLod = cloud_noise_lod_3d(microDx, microDy);
    float microWeight = 1.0 - smoothstep(CLOUD_MICRO_LOD_FADE_START,
                                         CLOUD_MICRO_LOD_FADE_END,
                                         microLod);
    float micro = medium;
    if (!shadowSample && microWeight > 1e-3)
    {
        micro = sample_cloud_noise_3d_grad(microP,
                                           microDx,
                                           microDy,
                                           vec3(0.731, 0.419, 0.193) + evolutionB * 1.75);
    }
    result.erodeNoise = mix(medium, micro, 0.35 * microWeight);
    return result;
}

CloudDetailSample sample_detail_2d(vec2 uvE,
                                   vec2 uvDx,
                                   vec2 uvDy,
                                   float hLocal,
                                   float hLocalDx,
                                   float hLocalDy,
                                   vec2 shearUv,
                                   float detailScale,
                                   float detailErode,
                                   vec2 evolution)
{
    CloudDetailSample result;
    result.erodeNoise = 0.5;
    result.erodeStrength = CLOUD_EROSION_STRENGTH * detailErode;
    float sliceU = sample_cloud_noise_grad_aniso(vec2(uvE.x + shearUv.x, hLocal),
                                                   vec2(uvDx.x, hLocalDx),
                                                   vec2(uvDy.x, hLocalDy),
                                                   vec2(detailScale, CLOUD_SLICE_HEIGHT_FREQ),
                                                   vec2(0.619, 0.281) + evolution);
    float sliceV = sample_cloud_noise_grad_aniso(vec2(uvE.y + shearUv.y, hLocal),
                                                   vec2(uvDx.y, hLocalDx),
                                                   vec2(uvDy.y, hLocalDy),
                                                   vec2(detailScale * 0.85, CLOUD_SLICE_HEIGHT_FREQ * 1.13),
                                                   vec2(0.113, 0.763) - evolution.yx);
    result.column = cloud_column(sliceU, sliceV);
    if (result.column <= 0.0 || result.erodeStrength <= 1e-3) return result;

    vec2 erodeScale = vec2(detailScale, CLOUD_SLICE_HEIGHT_FREQ) * CLOUD_EROSION_FREQ;
    vec2 erodeDx = vec2(uvDx.x, hLocalDx) * erodeScale;
    vec2 erodeDy = vec2(uvDy.x, hLocalDy) * erodeScale;
    float erodeLod = cloud_noise_lod_2d(erodeDx, erodeDy);
    result.erodeStrength *= 1.0 - smoothstep(CLOUD_DETAIL_LOD_FADE_START,
                                             CLOUD_DETAIL_LOD_FADE_END,
                                             erodeLod);
    if (result.erodeStrength <= 1e-3) return result;
    result.erodeNoise = sample_cloud_noise_grad_aniso(vec2(uvE.x + shearUv.x, hLocal),
                                                       vec2(uvDx.x, hLocalDx),
                                                       vec2(uvDy.x, hLocalDy),
                                                       erodeScale,
                                                       vec2(0.377, 0.531) + evolution * 1.35);
    return result;
}

float cloud_density(vec3 dir,
                    vec3 dirDx,
                    vec3 dirDy,
                    float height,
                    float heightDx,
                    float heightDy,
                    bool cloudsActive,
                    bool flipV,
                    vec2 overlayRotSC,
                    vec2 windHeading,
                    vec2 windSC,
                    vec4 evolutionOffsets,
                    bool shadowSample)
{
    float baseHeightM = max(pc.cloud_layer.x, 0.0);
    float thicknessM = max(pc.cloud_layer.y, 0.0);
    float densityScale = max(pc.cloud_layer.z, 0.0);
    float coverage = clamp(pc.cloud_layer.w, 0.0, 0.999);

    if (thicknessM <= 0.0 || densityScale <= 0.0) return 0.0;
    if (height < baseHeightM || height > baseHeightM + thicknessM) return 0.0;

    float h01 = (height - baseHeightM) / max(thicknessM, 1e-3);

    vec3 dirW = dir;
    bool windActive = cloudsActive;
    if (windActive)
    {
        vec3 east;
        vec3 north;
        build_local_cloud_frame(dir, east, north);
        vec3 windT = east * windHeading.x + north * windHeading.y;

        float s = windSC.x;
        float c = windSC.y;
        dirW = dir * c + windT * s;
    }

    vec3 dirWdx = dirDx;
    vec3 dirWdy = dirDy;
    dirW = rotate_y_sc(dirW, overlayRotSC);
    dirWdx = rotate_y_sc(dirWdx, overlayRotSC);
    dirWdy = rotate_y_sc(dirWdy, overlayRotSC);

    vec2 uvE;
    vec2 uvDx;
    vec2 uvDy;
    dir_to_equirect_grad(dirW, dirWdx, dirWdy, flipV, uvE, uvDx, uvDy);
    float localCov = clamp(textureGrad(cloudOverlayTex, uvE, uvDx, uvDy).r, 0.0, 1.0);

    uint miscPacked = uint(pc.misc.w);
    float weatherBlend = float((miscPacked >> MISC_NOISE_BLEND_SHIFT) & 0xFFu) * (1.0 / 255.0);
    // Bound this sample's coverage before fetching weather or detail noise.
    // Leave a small margin for rounding at the coverage threshold.
    float maxWeatherCov = mix(localCov, mix(localCov, 1.0, weatherBlend), 0.50);
    if (maxWeatherCov + 1e-6 < coverage) return 0.0;

    float detailErode = float((miscPacked >> MISC_DETAIL_ERODE_SHIFT) & 0xFFu) * (1.0 / 255.0);
    bool useNoise3D = (int(miscPacked & MISC_FLAGS_MASK) & FLAG_CLOUD_NOISE_3D) != 0;

    float lowScale = max(pc.cloud_params.x, 0.001);
    float detailScale = max(pc.cloud_params.y, 0.001);
    vec2 weatherSample = sample_cloud_weather_grad(uvE,
                                                   uvDx,
                                                   uvDy,
                                                   lowScale,
                                                   vec2(0.173, 0.547) + evolutionOffsets.xy);
    float weatherField = localCov;
    if (weatherBlend > 1e-4)
    {
        float weatherNoise = weatherSample.r;
        weatherNoise = clamp((weatherNoise - 0.5) * 1.25 + 0.5, 0.0, 1.0);
        weatherField = clamp(mix(localCov, weatherNoise, weatherBlend), 0.0, 1.0);
    }

    float weatherCov = mix(localCov, weatherField, 0.50);
    if (weatherCov <= coverage) return 0.0;

    float typeField = weatherSample.g;
    if (typeField <= (1.0 / 255.0))
    {
        // Legacy R8 weather maps have no type channel. Re-sample R through a
        // rotated, lower-frequency domain so cloud type does not collapse to
        // one global profile.
        typeField = sample_cloud_noise_grad_aniso(uvE.yx,
                                                   uvDx.yx,
                                                   uvDy.yx,
                                                   vec2(lowScale * 0.5, lowScale),
                                                   vec2(0.731, 0.193) - evolutionOffsets.yx);
    }

    CloudVerticalProfile vertical = sample_vertical_profile(h01, weatherField, typeField, pc.terrain_params.w);
    float hLocal = vertical.hLocal;
    float localSpan = vertical.span;
    float profile = vertical.density;
    if (profile <= 0.0) return 0.0;

    float cov = max(0.0, weatherCov - coverage) / max(1.0 - coverage, 1e-3);
    cov = pow(cov, mix(1.45, 0.75, weatherField));
    if (cov <= 0.0) return 0.0;

    float hLocalDx = heightDx / max(thicknessM * localSpan, 1e-3);
    float hLocalDy = heightDy / max(thicknessM * localSpan, 1e-3);

    vec2 shearDir = windHeading;
    float shearLen2 = dot(shearDir, shearDir);
    if (shearLen2 < 1e-6)
    {
        shearDir = normalize(vec2(0.8660254, 0.5));
    }
    else
    {
        shearDir *= inversesqrt(shearLen2);
    }

    float shearAmount = mix(CLOUD_SHEAR_MIN, CLOUD_SHEAR_MAX, weatherField);
    vec2 shearUv = shearDir * ((hLocal - 0.5) * shearAmount);

    CloudDetailSample detail = useNoise3D
        ? sample_detail_3d(dirW,
                           dirWdx,
                           dirWdy,
                           height,
                           heightDx,
                           heightDy,
                           shearUv,
                           detailScale,
                           detailErode,
                           evolutionOffsets.zw,
                           shadowSample)
        : sample_detail_2d(uvE,
                           uvDx,
                           uvDy,
                           hLocal,
                           hLocalDx,
                           hLocalDy,
                           shearUv,
                           detailScale,
                           detailErode,
                           evolutionOffsets.zw);
    if (detail.column <= 0.0) return 0.0;

    float base = cov * profile * detail.column;
    if (base <= 0.0) return 0.0;

    if (detail.erodeStrength > 1e-3)
    {
        // Remap-based edge erosion: the high-frequency noise raises the density floor, so
        // low-density edges get carved while dense cores stay solid. Wispy carve near the
        // layer bottom, billowy near the top.
        float erode = mix(detail.erodeNoise, 1.0 - detail.erodeNoise, clamp(hLocal * 3.0, 0.0, 1.0));
        float thresh = erode * detail.erodeStrength;
        base = clamp((base - thresh) / max(1.0 - thresh, 1e-3), 0.0, 1.0);
        if (base <= 0.0) return 0.0;
    }

    return base * densityScale;
}
