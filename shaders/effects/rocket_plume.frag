#version 460
#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Set 1: plume inputs
layout(set = 1, binding = 0) uniform sampler2D hdrInput;
layout(set = 1, binding = 1) uniform sampler2D posTex;
layout(set = 1, binding = 3) uniform sampler2D noiseTex;

struct PlumeData
{
    mat4 world_to_plume;

    vec4 shape;       // x length, y nozzleRadius, z expansionAngleRad, w radiusExp
    vec4 emission0;   // rgb coreColor, w intensity
    vec4 emission1;   // rgb plumeColor, w coreStrength
    vec4 params;      // x coreLength, y radialFalloff, z axialFalloff, w softAbsorption
    vec4 noise_shock; // x noiseStrength, y noiseScale, z noiseSpeed, w shockStrength
    vec4 shock_misc;  // x shockFrequency, y shockWidth, z shockDecay
    vec4 sheath;      // x strength, y normalized radius, z normalized width, w unused
};

layout(set = 1, binding = 2, std430) readonly buffer Plumes
{
    PlumeData plumes[];
} plume;

layout(push_constant) uniform PlumePush
{
    ivec4 misc; // x steps, y plumeCount
} pc;

vec3 getCameraLocalPosition()
{
    mat3 rotT = mat3(sceneData.view); // R^T
    mat3 rot  = transpose(rotT);      // R
    vec3 T    = sceneData.view[3].xyz;
    return -rot * T;
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

bool intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float tmin, out float tmax)
{
    tmin = 0.0;
    tmax = 1e30;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(rd[axis]) < 1e-7)
        {
            if (ro[axis] < bmin[axis] || ro[axis] > bmax[axis])
            {
                return false;
            }
            continue;
        }

        float invD = 1.0 / rd[axis];
        float t0 = (bmin[axis] - ro[axis]) * invD;
        float t1 = (bmax[axis] - ro[axis]) * invD;
        tmin = max(tmin, min(t0, t1));
        tmax = min(tmax, max(t0, t1));
        if (tmax < tmin)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    vec3 baseColor = texture(hdrInput, inUV).rgb;

    int plumeCount = clamp(pc.misc.y, 0, plume.plumes.length());
    if (plumeCount <= 0)
    {
        outColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 camLocal = getCameraLocalPosition();

    // Reconstruct a local-space ray for this pixel (Vulkan depth range 0..1).
    vec2 ndc = inUV * 2.0 - 1.0;
    vec3 viewDir = normalize(vec3(ndc.x / sceneData.proj[0][0], ndc.y / sceneData.proj[1][1], -1.0));
    vec3 rdWorld = transpose(mat3(sceneData.view)) * viewDir;

    // Clamp march to geometry distance (gbufferPosition.w == 1 for valid surfaces).
    float tGeom = 1e30;
    vec4 posSample = texture(posTex, inUV);
    if (posSample.w > 0.0)
    {
        float surfT = dot(posSample.xyz - camLocal, rdWorld);
        if (surfT > 0.0)
        {
            tGeom = surfT;
        }
    }

    int steps = clamp(pc.misc.x, 8, 256);

    vec3 add = vec3(0.0);
    float trans = 1.0;
    vec3 scatter = vec3(0.0);
    float time = sceneData.timeParams.x;

    for (int pi = 0; pi < plumeCount; ++pi)
    {
        PlumeData p = plume.plumes[pi];

        float len = max(p.shape.x, 0.001);
        float invLen = 1.0 / len;
        float baseR = max(p.shape.y, 0.0);
        float ang = clamp(p.shape.z, 0.0, 1.55);
        float radiusExp = max(p.shape.w, 0.0);
        float radiusGrowth = tan(ang) * len;

        float noiseStrength = max(p.noise_shock.x, 0.0);
        float edgeNoiseStrength = clamp(noiseStrength, 0.0, 1.5);
        float unitNoiseStrength = clamp(noiseStrength, 0.0, 1.0);
        float maxTurbStrength = clamp(noiseStrength * 2.0, 0.0, 3.0);
        // Bound feathering, edge breakup and XY displacement together.
        float outsideRadius = 1.09 + 0.14 * edgeNoiseStrength +
                              0.1132 * maxTurbStrength;
        float outsideRadiusSq = outsideRadius * outsideRadius;
        float rMax = (baseR + radiusGrowth) * outsideRadius;
        rMax = max(rMax, baseR);

        // Transform ray to plume-local space.
        vec3 ro = (p.world_to_plume * vec4(camLocal, 1.0)).xyz;
        vec3 rd = (p.world_to_plume * vec4(rdWorld, 0.0)).xyz;
        float rdLen = length(rd);
        if (any(isnan(ro)) || any(isinf(ro)) ||
            isnan(rdLen) || isinf(rdLen) || rdLen < 1e-6)
        {
            continue;
        }
        rd /= rdLen;

        // Conservative local bounds (cylinder/box around the cone).
        vec3 bmin = vec3(-rMax, -rMax, 0.0);
        vec3 bmax = vec3( rMax,  rMax, len);

        float t0, t1;
        if (!intersectAABB(ro, rd, bmin, bmax, t0, t1))
        {
            continue;
        }

        // Ray parameters are in plume-local units after normalizing rd.
        t1 = min(t1, tGeom * rdLen);
        float tStart = max(t0, 0.0);
        if (t1 <= tStart)
        {
            continue;
        }

        // Preserve roughly constant sample spacing instead of spending the full
        // axial step budget on short rays crossing the plume sideways.
        float segmentLength = t1 - tStart;
        if (isnan(segmentLength) || isinf(segmentLength))
        {
            continue;
        }
        float marchStepCount = clamp(
            ceil(float(steps) * segmentLength * invLen),
            8.0,
            float(steps));
        float dt = segmentLength / marchStepCount;
        float jitter = hash12(inUV * 1024.0 + vec2(float(pi) * 13.37, float(pi) * 7.17));

        vec3 coreCol = max(p.emission0.rgb, vec3(0.0));
        vec3 plumeCol = max(p.emission1.rgb, vec3(0.0));
        float intensity = max(p.emission0.w, 0.0);
        float coreStrength = max(p.emission1.w, 0.0);

        float coreLen = max(p.params.x, 0.0);
        float radialPow = max(p.params.y, 0.0);
        float axialPow = max(p.params.z, 0.0);
        float absorb = max(p.params.w, 0.0);

        float noiseScale = max(p.noise_shock.y, 0.001);
        float noiseSpeed = p.noise_shock.z;
        float shockStrength = max(p.noise_shock.w, 0.0);
        float shockFreq = max(p.shock_misc.x, 0.0);
        float shockWidth = max(p.shock_misc.y, 0.001);
        float shockDecay = max(p.shock_misc.z, 0.0);

        float sheathStrength = max(p.sheath.x, 0.0);
        float sheathRadius = clamp(p.sheath.y, 0.0, 1.0);
        float sheathWidth = max(p.sheath.z, 0.001);
        float invSheathWidth = 1.0 / sheathWidth;
        float warpUvScale = noiseScale * 0.55;
        float detailZScale = noiseScale * 0.85;
        float detailCrossScale = noiseScale * 2.25;
        float flow = time * noiseSpeed;
        float invCoreLen = (coreLen > 0.0) ? (1.0 / coreLen) : 0.0;
        vec3 hotCoreCol = mix(vec3(1.0), coreCol, 0.85);
        float radiance = intensity * 0.18;
        float shockRadiusRef = max(
            baseR + radiusGrowth * ((radiusExp > 0.0)
                ? pow(0.18, radiusExp) : 0.18), 1e-4);

        for (int si = 0; si < steps; ++si)
        {
            if (float(si) >= marchStepCount)
            {
                break;
            }
            float sampleT = tStart + (float(si) + jitter) * dt;
            vec3 pos = ro + rd * sampleT;

            float zNorm = pos.z * invLen;
            float u = clamp(zNorm, 0.0, 1.0);
            float radiusCurve = (radiusExp > 0.0) ? pow(u, radiusExp) : u;
            float pr = baseR + radiusGrowth * radiusCurve;
            pr = max(pr, 1e-4);

            vec2 q = pos.xy / pr;
            if (dot(q, q) > outsideRadiusSq)
            {
                continue;
            }

            float zFlow = zNorm - flow;

            // Low-frequency cross-section displacement and high-frequency axial detail
            // use independent samples so the whole plume does not pulse as one field.
            vec2 uvWarp = q * warpUvScale +
                          vec2(zFlow * 0.19, -zFlow * 0.13);
            float nWarpX = textureLod(noiseTex, uvWarp + vec2(17.13, 3.71), 0.0).r;
            float nWarpY = textureLod(noiseTex, uvWarp.yx + vec2(5.23, 11.87), 0.0).r;
            // Cartesian cross-section detail has no angular wrap seam and
            // stretches along the flow into filaments instead of broad bands.
            vec2 uvDetail = q * detailCrossScale +
                            vec2(zFlow * detailZScale, -zFlow * detailZScale * 0.73) +
                            vec2(nWarpX, nWarpY) * 0.17;
            float nDetail = textureLod(noiseTex, uvDetail + vec2(23.41, 7.19), 0.0).r;

            float turbStrength = clamp(
                noiseStrength * (0.15 + 0.85 * smoothstep(0.05, 1.0, u)) * 2.0,
                0.0,
                3.0);
            vec2 warpN = vec2(nWarpX, nWarpY) * 2.0 - 1.0;
            float tailTurbulence = smoothstep(0.25, 0.95, u);
            q += warpN * (mix(0.055, 0.08, tailTurbulence) * turbStrength);

            float lowNoise = (nWarpX + nWarpY) - 1.0;
            float detailNoise = nDetail * 2.0 - 1.0;
            float edgeBreakup = (0.012 + 0.128 * tailTurbulence) *
                                edgeNoiseStrength;
            float edgeNoise = mix(lowNoise, detailNoise, 0.7);
            float edgeRadius = 1.0 + edgeNoise * edgeBreakup;
            float edgeFeather = mix(0.025, 0.09, smoothstep(0.15, 1.0, u));

            float rr = length(q);
            float radialCoord = clamp(
                (edgeRadius - rr) / max(edgeRadius, 0.001),
                0.0,
                1.0);
            float boundary = 1.0 - smoothstep(
                edgeRadius - edgeFeather,
                edgeRadius + edgeFeather,
                rr);

            if (boundary > 1e-4)
            {
                float lowMod = max(1.0 + lowNoise * (0.3 * turbStrength), 0.1);
                float detailMod = exp2(detailNoise * (0.9 * turbStrength));
                float detailMix = clamp(turbStrength * 0.55, 0.0, 1.0);

                // Let different parts of the cross-section burn out at different lengths.
                // This removes the flat end cap without making the stable core flicker.
                float tailWarp = (lowNoise * 0.14 + detailNoise * 0.1) *
                                 smoothstep(0.4, 0.95, u) *
                                 unitNoiseStrength;
                float tailCoord = clamp(u + tailWarp, 0.0, 1.1);
                float tailMask = 1.0 - smoothstep(0.82, 1.02, tailCoord);

                float radial = (radialPow > 0.0) ? pow(radialCoord, radialPow) : radialCoord;
                float axialCoord = max(1.0 - tailCoord, 0.0);
                float axial = (axialPow > 0.0) ? pow(axialCoord, axialPow) : axialCoord;
                axial *= tailMask * (1.0 - smoothstep(0.9, 1.0, u));

                // A coherent hot core, a turbulent outer shear layer, and the body of
                // the plume are evaluated separately instead of sharing one noise mask.
                float coreAxial = 0.0;
                if (coreLen > 0.0)
                {
                    coreAxial = 1.0 - smoothstep(coreLen * 0.65, coreLen, pos.z);
                }
                float coreProgress = (coreLen > 0.0)
                    ? clamp(pos.z * invCoreLen, 0.0, 1.0)
                    : 1.0;
                // Anchor the hot jet to the nozzle radius; it tapers while
                // the surrounding plume expands, avoiding a glowing bulb.
                float coreRadius = max(baseR, 0.01) / pr * mix(
                    0.85, 0.18, smoothstep(0.0, 1.0, coreProgress));
                float coreDistance = rr / coreRadius;
                float coreRadial = exp2(
                    -coreDistance * coreDistance * 1.65);
                float coreIgnition = smoothstep(0.0, 0.025, coreProgress);
                float coreDensity = coreAxial * coreRadial * coreIgnition *
                                    mix(1.0, lowMod, 0.2) *
                                    mix(1.0, 0.55, coreProgress);

                float baseDensity = radial * axial * mix(1.0, detailMod, detailMix);
                baseDensity *= mix(1.0, 0.35, coreAxial * coreRadial);

                float sheathDistance =
                    (rr - sheathRadius) * invSheathWidth;
                float sheathBand = exp2(-sheathDistance * sheathDistance * 1.5);
                float sheathGrow = smoothstep(0.03, 0.22, u);
                float sheathDensity = sheathStrength * sheathBand * boundary * axial * sheathGrow * detailMod;
                // Break the continuous outer cone into fading downstream wisps.
                float sheathBreakup = smoothstep(-0.65, 0.5, detailNoise + lowNoise * 0.35);
                sheathDensity *= mix(1.0, sheathBreakup,
                                     0.65 * tailTurbulence * unitNoiseStrength);
                sheathDensity *= 1.0 - 0.45 * tailTurbulence;

                // A filled double cone spreads emission along the cell and
                // narrows at both ends instead of concentrating it in a ball.
                float shockDensity = 0.0;
                float cellContrast = 1.0;
                if (shockStrength > 0.0 && shockFreq > 0.0)
                {
                    float shockPhase = shockFreq * u * (1.15 - 0.15 * u);
                    float disorder = smoothstep(0.12, 0.9, u);
                    shockPhase += lowNoise * noiseStrength *
                                  mix(0.012, 0.075, disorder);
                    float cellIndex = floor(shockPhase);
                    float cell = fract(shockPhase);
                    float cellRandom = hash12(vec2(
                        cellIndex,
                        shockFreq * 0.137 + 4.19));
                    float randomStrength = mix(0.58, 1.16, cellRandom);
                    float cellStrength = mix(1.08, randomStrength, disorder);

                    float cellPeak = mix(0.43, 0.51, cellRandom);
                    float cellShape = clamp(min(
                        (cell - 0.035) / (cellPeak - 0.035),
                        (0.965 - cell) / (0.965 - cellPeak)), 0.0, 1.0);
                    float axialPulse = smoothstep(0.0, 0.24, cellShape) *
                                       mix(0.65, 1.0, cellShape);
                    cellContrast = mix(1.0, 0.45 + 0.55 * axialPulse,
                                       clamp(shockStrength, 0.0, 1.0) *
                                       (1.0 - smoothstep(0.55, 0.9, u)));
                    float radialSpan = (min(sheathRadius, 0.88) * 0.3 +
                                        shockWidth * 0.45) * cellShape;
                    // Shock cells expand more slowly than the outer plume.
                    // Also compensate for the longer luminous path through
                    // downstream cells, which otherwise offsets axial decay.
                    float expansionFade = sqrt(min(shockRadiusRef / pr, 1.0));
                    radialSpan *= expansionFade;
                    // Bound the transverse size relative to the local cell
                    // length so downstream cells retain an elongated profile.
                    float cellLength = len / max(shockFreq * (1.15 - 0.3 * u), 1e-3);
                    radialSpan = min(radialSpan, cellLength * 0.16 * cellShape / pr);
                    float radialDistance = rr / max(radialSpan, 0.02);
                    float radialFill = (1.0 - smoothstep(0.45, 1.0, radialDistance)) *
                                       exp2(-radialDistance * radialDistance * 0.65);

                    float shockStart = smoothstep(0.015, 0.08, u);
                    float shockTail = 1.0 - smoothstep(0.55, 0.92, tailCoord);
                    shockDensity = shockStrength * cellStrength *
                                   axialPulse * radialFill *
                                   exp2(-u * shockDecay) * boundary *
                                   shockStart * shockTail * expansionFade *
                                   mix(1.0, detailMod, 0.18);
                }

                // Preserve color through tonemapping by reserving near-white radiance
                // for the short hot core. The body shifts from the warm ignition color
                // toward plumeColor, while the sheath remains cooler and dimmer.
                vec3 bodyCol = mix(
                    coreCol,
                    plumeCol,
                    smoothstep(0.02, 0.38, u));
                vec3 sheathCol = mix(plumeCol, bodyCol, 0.08);
                vec3 shockCol = mix(
                    hotCoreCol,
                    plumeCol,
                    smoothstep(0.12, 0.78, u));

                vec3 emit = radiance * (
                    bodyCol * (0.38 * baseDensity * cellContrast) +
                    sheathCol * (0.22 * sheathDensity) +
                    hotCoreCol * (0.42 * coreStrength * coreDensity) +
                    shockCol * (0.95 * shockDensity));

                float density = max(
                    baseDensity + 0.65 * sheathDensity +
                    0.35 * coreDensity + 0.15 * shockDensity,
                    0.0);

                if (absorb > 0.0)
                {
                    float alpha = 1.0 - exp(-density * absorb * dt);
                    vec3 source = emit / max(density, 1e-4);
                    scatter += trans * alpha * source;
                    trans *= (1.0 - alpha);
                    if (trans < 0.01)
                    {
                        break;
                    }
                }
                else
                {
                    add += emit * dt;
                }
            }

        }
    }

    vec3 outRgb = add + scatter + trans * baseColor;
    outColor = vec4(outRgb, 1.0);
}
