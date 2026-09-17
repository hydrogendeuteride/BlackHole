#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"
#include "ibl_common.glsl"
#include "lighting_common.glsl"
#include "planet_gbuffer_payload.glsl"
#include "planet_shadow.glsl"
#include "planet_terrain_common.glsl"
#include "planet_ocean_mask_common.glsl"
#include "atmosphere/include/transmittance_lut.glsl"
#define CLOUD_SHADOW_OCEAN
#include "cloud_shadow.glsl"

layout(location = 0) in vec3 inBaseNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in float inSeaRadius;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfacePos;

layout(set = 2, binding = 0) uniform sampler2D transmittanceLut;

struct Vertex
{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
    vec4 tangent;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

layout(push_constant) uniform OceanPush
{
    mat4 render_matrix;
    vec4 body_center_radius;
    vec4 shell_params;
    vec4 atmosphere_center_radius;
    vec4 atmosphere_params;
    vec4 beta_rayleigh;
    vec4 beta_mie;
    vec4 beta_absorption;
    VertexBuffer vertexBuffer;
} pc;

vec3 getCameraLocalPosition()
{
    mat3 rotT = mat3(sceneData.view);
    mat3 rot = transpose(rotT);
    vec3 t = sceneData.view[3].xyz;
    return -rot * t;
}

float sample_wave_height(vec3 unitDir, float radius, float timeSeconds)
{
    const vec3 dirA = normalize(vec3(0.82, 0.0, 0.57));
    const vec3 dirB = normalize(vec3(-0.47, 0.0, 0.88));
    const vec3 dirC = normalize(vec3(0.21, 0.0, -0.98));
    const float wavelengthA = 2500.0;
    const float wavelengthB = 350.0;
    const float wavelengthC = 90.0;
    const float amplitudeA = 0.55;
    const float amplitudeB = 0.18;
    const float amplitudeC = 0.05;
    const float weightA = 0.55;
    const float weightB = 0.30;
    const float weightC = 0.15;
    const float speedA = 18.0;
    const float speedB = 5.0;
    const float speedC = 2.2;

    float phaseA = dot(unitDir, dirA) * radius * (2.0 * PI / wavelengthA) + timeSeconds * speedA * (2.0 * PI / wavelengthA);
    float phaseB = dot(unitDir, dirB) * radius * (2.0 * PI / wavelengthB) + timeSeconds * speedB * (2.0 * PI / wavelengthB);
    float phaseC = dot(unitDir, dirC) * radius * (2.0 * PI / wavelengthC) + timeSeconds * speedC * (2.0 * PI / wavelengthC);

    return weightA * amplitudeA * sin(phaseA) +
           weightB * amplitudeB * sin(phaseB) +
           weightC * amplitudeC * sin(phaseC);
}

vec2 sample_wave_slopes(vec3 unitDir, vec3 east, vec3 north, float radius, float timeSeconds)
{
    const float sampleStepMeters = 24.0;

    vec3 dirE = normalize(unitDir * radius + east * sampleStepMeters);
    vec3 dirN = normalize(unitDir * radius + north * sampleStepMeters);

    float h0 = sample_wave_height(unitDir, radius, timeSeconds);
    float hE = sample_wave_height(dirE, radius, timeSeconds);
    float hN = sample_wave_height(dirN, radius, timeSeconds);

    return vec2((hE - h0) / sampleStepMeters, (hN - h0) / sampleStepMeters);
}

vec3 compute_wave_normal(vec3 unitDir, float radius, float timeSeconds, float coverage)
{
    vec3 anchor = (abs(unitDir.y) < 0.98) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 east = normalize(cross(anchor, unitDir));
    vec3 north = normalize(cross(unitDir, east));
    vec2 slopes = sample_wave_slopes(unitDir, east, north, radius, timeSeconds);

    float slopeScale = mix(0.0, 4.2, coverage);
    vec3 n = unitDir - east * slopes.x * slopeScale - north * slopes.y * slopeScale;
    return normalize(n);
}

vec2 sample_air_mass(vec3 positionLocal, vec3 rayDir, vec3 center, float planetRadius, float atmRadius)
{
    float r = length(positionLocal - center);
    if (planetRadius <= 0.0 || atmRadius <= planetRadius || r <= 0.0)
    {
        return vec2(0.0);
    }

    vec3 up = (positionLocal - center) / r;
    float mu = dot(up, normalize(rayDir));
    return sample_transmittance_air_mass(transmittanceLut, r, mu, planetRadius, atmRadius);
}

vec3 sample_atmosphere_transmittance(vec3 startLocal,
                                     vec3 rayDir,
                                     vec3 cameraLocal,
                                     vec3 center,
                                     float planetRadius,
                                     float atmRadius,
                                     float Hr,
                                     float Hm,
                                     vec3 betaR,
                                     vec3 betaM,
                                     vec3 betaA,
                                     bool segmentToCamera)
{
    if (planetRadius <= 0.0 || atmRadius <= planetRadius)
    {
        return vec3(1.0);
    }

    vec2 airMass = sample_air_mass(startLocal, rayDir, center, planetRadius, atmRadius);
    if (segmentToCamera)
    {
        float cameraRadius = length(cameraLocal - center);
        if (cameraRadius > planetRadius && cameraRadius < atmRadius)
        {
            airMass -= sample_air_mass(cameraLocal, rayDir, center, planetRadius, atmRadius);
        }
    }
    airMass = max(airMass, vec2(0.0));

    vec2 opticalDepth = airMass * vec2(Hr, Hm);
    return exp(-(betaR * opticalDepth.x + betaM * opticalDepth.y + betaA * opticalDepth.x));
}

vec3 evaluate_sky_reflection(vec3 reflectDir,
                             vec3 localUp,
                             vec3 sunDir,
                             vec3 sunColor,
                             vec3 ambientColor,
                             vec3 skyTint,
                             vec3 skyTransmittance,
                             vec3 sunTransmittance,
                             bool atmosphereActive)
{
    float upDot = dot(reflectDir, localUp);
    float skyVisibility = smoothstep(-0.10, 0.03, upDot);
    if (skyVisibility <= 0.0)
    {
        return vec3(0.0);
    }

    float up01 = clamp(upDot * 0.5 + 0.5, 0.0, 1.0);
    float horizon = pow(1.0 - abs(upDot), 1.6);
    float sunAlign = max(dot(reflectDir, sunDir), 0.0);

    vec3 ambientBase = max(ambientColor, vec3(0.02));
    vec3 zenithColor = ambientBase * vec3(0.45, 0.70, 1.25) + sunColor * 0.015;
    vec3 horizonColor = ambientBase * vec3(0.95, 1.00, 1.05) + sunColor * 0.055;
    const float skyReflectance = 0.35;
    const float sunReflectance = 1.15;

    // The ambient sky dome reflects the (Rayleigh-blue) sky, not the neutral
    // scene ambient; without the tint it lays a gray sheen over the water.
    vec3 sky = mix(horizonColor, zenithColor, pow(up01, 0.65)) * skyReflectance * skyTint;

    // Sun-dependent terms must respect sun occlusion/transmittance separately:
    // the composite pass only attenuates the camera segment, never the path
    // toward the sun, so an unattenuated halo shows up as a night-time glint.
    // Keep these lobes tight (half-widths ~10deg / ~3deg / ~6deg): wide
    // powers here paint a pale wash over half the sun-side ocean and kill
    // the water's saturation at grazing angles.
    vec3 horizonWarm = sunColor * pow(sunAlign, 48.0) * (0.30 + 0.22 * horizon);
    vec3 sunHalo = sunColor * (pow(sunAlign, 420.0) * 0.55 + pow(sunAlign, 110.0) * 0.07);
    sky += (horizonWarm + sunHalo) * sunReflectance * sunTransmittance;

    if (atmosphereActive)
    {
        sky *= skyTransmittance;
    }

    return sky * skyVisibility;
}

vec3 evaluate_ocean_specular(vec3 N, vec3 V, vec3 L, float roughness, vec3 F0)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 H = normalize(V + L);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    return (D * G * F / max(4.0 * NdotV * NdotL, 0.001)) * NdotL;
}

void main()
{
    float coverage = sample_planet_ocean_coverage(inUV);
    // Derivatives are undefined after discard in this quad; take them first.
    float footprint = length(fwidth(inWorldPos));
    if (!planet_ocean_visible(coverage))
    {
        discard;
    }

    float strength = planet_ocean_strength();
    float baseRoughness = clamp(materialData.extra[3].w, 0.04, 1.0);

    // The analytic waves (90m-2.5km wavelengths) go sub-pixel long before
    // orbit distances; point-sampling them there turns the sharp specular
    // lobes into speckle noise that FXAA/tonemap average into a dim smear.
    // Fade the wave normal out with pixel footprint and widen the lobe so
    // the far view converges to a smooth sphere with a broad stable glint.
    float farBlend = smoothstep(60.0, 2500.0, footprint);
    float waveFade = 1.0 - farBlend;

    vec3 baseNormal = normalize(inBaseNormal);
    vec3 N = compute_wave_normal(baseNormal, inSeaRadius, sceneData.timeParams.x, coverage * waveFade);
    vec3 cameraLocal = getCameraLocalPosition();
    vec3 V = normalize(cameraLocal - inWorldPos);
    vec3 Lsun = -sceneData.sunlightDirection.xyz;

    float sunVis = 1.0;
    if (sceneData.rtParams.y > 0.0)
    {
        sunVis = planet_analytic_shadow_visibility(inWorldPos + N * 0.01, Lsun);
        sunVis = max(sunVis, clamp(sceneData.shadowTuning.x, 0.0, 1.0));
    }

    const vec3 F0 = vec3(0.02);
    // Far-glint knobs (the wave normal is faded out there, so the smooth
    // sphere normal cannot alias and the lobe shape is a free choice):
    // SIZE is the lobe roughness from orbit — bigger = wider sun-dot.
    // GAIN * size^2 holds the peak brightness while SIZE changes, since the
    // GGX peak falls off as 1/size^2. Current gain keeps the peak at the
    // size=0.055, boost=2.0 level. If the grazing-angle streak starts
    // washing out the low-altitude view again, lower SIZE first.
    const float FAR_GLINT_SIZE = 0.085;
    const float FAR_GLINT_GAIN = 800.0;
    float lobe0 = mix(baseRoughness, max(baseRoughness * 0.75, FAR_GLINT_SIZE), farBlend);
    float lobe1 = clamp(baseRoughness * 0.18, 0.012, 0.06);
    vec3 directSpec = evaluate_ocean_specular(N, V, Lsun, lobe0, F0) *
                      mix(1.0, FAR_GLINT_GAIN * lobe0 * lobe0, farBlend);
    // The sharp lobe is pure aliasing at distance; fade it with the waves.
    directSpec += evaluate_ocean_specular(N, V, Lsun, lobe1, F0) * (1.75 * strength * waveFade);

    vec3 directTransmittance = vec3(1.0);
    bool atmosphereActive = (pc.shell_params.y > 0.5) &&
                            (pc.atmosphere_center_radius.w > 0.0) &&
                            (pc.atmosphere_params.x > pc.atmosphere_center_radius.w) &&
                            (pc.atmosphere_params.w > 0.0);
    if (atmosphereActive)
    {
        vec3 betaR = max(pc.beta_rayleigh.rgb, vec3(0.0));
        vec3 betaM = max(pc.beta_mie.rgb, vec3(0.0));
        vec3 betaA = max(pc.beta_absorption.rgb, vec3(0.0));
        if (any(greaterThan(betaR + betaM + betaA, vec3(0.0))))
        {
            vec3 center = pc.atmosphere_center_radius.xyz;
            float planetRadius = pc.atmosphere_center_radius.w;
            float atmRadius = pc.atmosphere_params.x;
            float Hr = max(pc.atmosphere_params.y, 1.0);
            float Hm = max(pc.atmosphere_params.z, 1.0);
            directTransmittance = sample_atmosphere_transmittance(
                inWorldPos,
                Lsun,
                cameraLocal,
                center,
                planetRadius,
                atmRadius,
                Hr,
                Hm,
                betaR,
                betaM,
                betaA,
                false);
        }
    }

    // Apply the same cloud column as terrain to sunlight, including the glint
    // and upwelling. The ambient sky reflection remains visible in cloud shade.
    directTransmittance *= cloud_shadow_visibility(inWorldPos);

    // The atmosphere composite attenuates the camera segment after ocean writes surface position.
    // Keep ocean shading to incoming light paths to avoid double-tinting grazing water.
    // Sky reflection uses local sky radiance, then the composite pass attenuates the camera segment.
    vec3 direct = directSpec * sceneData.sunlightColor.rgb * sceneData.sunlightColor.a * sunVis *
                  directTransmittance;

    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    float sunFacing = dot(baseNormal, Lsun);
    // Wide ramp: soften the day-vs-twilight brightness step on the water so
    // the surface terms don't drop off at the same spot as the inscatter.
    float nightReflectionFactor = mix(0.04, 1.0, smoothstep(-0.15, 0.35, sunFacing));
    vec3 localUp = atmosphereActive
        ? normalize(inWorldPos - pc.atmosphere_center_radius.xyz)
        : normalize(baseNormal);
    // Sun terms in the sky reflection get the sun-path transmittance (zero
    // when the sun ray hits the planet) plus a horizon cut for airless bodies.
    vec3 sunReflTrans = directTransmittance * smoothstep(-0.02, 0.03, dot(localUp, Lsun));
    vec3 skyTint = vec3(1.0);
    if (atmosphereActive)
    {
        vec3 tintBeta = max(pc.beta_rayleigh.rgb, vec3(0.0));
        float tintMax = max(tintBeta.r, max(tintBeta.g, tintBeta.b));
        if (tintMax > 0.0)
        {
            skyTint = mix(vec3(1.0), tintBeta / tintMax, 0.45);
        }
    }
    vec3 skyColor = evaluate_sky_reflection(
        R,
        localUp,
        Lsun,
        sceneData.sunlightColor.rgb * sceneData.sunlightColor.a,
        sceneData.ambientColor.rgb,
        skyTint,
        vec3(1.0),
        sunReflTrans,
        false);
    vec3 fresnel = fresnelSchlick(NdotV, F0);
    vec3 skyReflection = skyColor * fresnel * nightReflectionFactor;

    // Upwelling (sub-surface scattered) sunlight. This deep ocean albedo is
    // the PRIMARY source of the ocean's color: single-scatter inscatter can
    // only produce a washed-out gray-blue, so the water body itself has to
    // carry the saturation (same approach as typical game earth renders).
    // Hue stays oceanic (B > G > R) but it needs enough luminance to
    // win against the gray-blue inscatter laid on top; too dark and the
    // ocean reads as slate-gray instead of blue.
    vec3 waterAlbedo = vec3(0.006, 0.020, 0.065);
    float NdotLup = max(dot(N, Lsun), 0.0);
    vec3 upwelling = waterAlbedo * (NdotLup / PI) *
                     sceneData.sunlightColor.rgb * sceneData.sunlightColor.a *
                     sunVis * directTransmittance * (vec3(1.0) - fresnel);
    // Faint sky-diffuse fill so twilight water fades out smoothly.
    upwelling += waterAlbedo * sceneData.ambientColor.rgb * 0.50 *
                 nightReflectionFactor * (vec3(1.0) - fresnel);

    vec3 color = direct + skyReflection + upwelling;
    outColor = vec4(color, 1.0);
    outSurfacePos = vec4(inWorldPos, encode_planet_gbuffer_pos_w(1.0, 1.0, 1.0));
}
