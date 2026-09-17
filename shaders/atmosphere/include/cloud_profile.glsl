#ifndef CLOUD_PROFILE_GLSL
#define CLOUD_PROFILE_GLSL

struct CloudVerticalProfile
{
    float hLocal;
    float span;
    float density;
};

CloudVerticalProfile sample_vertical_profile(float h01, float weatherField, float typeField, float typeBias)
{
    CloudVerticalProfile result;
    result.hLocal = 0.0;
    result.span = 1.0;
    result.density = 0.0;

    typeBias = clamp(typeBias, 0.0, 1.0);
    float cloudType = clamp(typeField + typeBias - 0.5, 0.0, 1.0);
    vec3 typeWeights = max(vec3(0.0),
                           vec3(1.0) - abs(vec3(cloudType) - vec3(0.0, 0.5, 1.0)) * 2.0);
    typeWeights /= max(dot(typeWeights, vec3(1.0)), 1e-3);

    float typeBottom01 = dot(typeWeights, vec3(0.08, 0.04, 0.02));
    float typeTop01 = dot(typeWeights, vec3(0.42, 0.80, 1.00));
    float centerShift = (weatherField - 0.5) * 0.10;
    float spanScale = mix(0.85, 1.08, weatherField);
    float typeCenter = 0.5 * (typeBottom01 + typeTop01) + centerShift;
    float typeHalfSpan = 0.5 * (typeTop01 - typeBottom01) * spanScale;
    float localBottom01 = clamp(typeCenter - typeHalfSpan, 0.0, 1.0);
    float localTop01 = clamp(typeCenter + typeHalfSpan, 0.0, 1.0);
    result.span = max(localTop01 - localBottom01, 1e-3);

    if (h01 < localBottom01 || h01 > localTop01) return result;
    result.hLocal = (h01 - localBottom01) / result.span;

    float stratus = smoothstep(0.0, 0.18, result.hLocal) *
                    (1.0 - smoothstep(0.72, 1.0, result.hLocal));
    float cumulus = smoothstep(0.0, 0.07, result.hLocal) *
                    (1.0 - smoothstep(0.55, 1.0, result.hLocal));
    float towering = smoothstep(0.0, 0.04, result.hLocal) *
                     (1.0 - smoothstep(0.78, 1.0, result.hLocal));
    towering *= mix(0.88, 1.0, smoothstep(0.15, 0.65, result.hLocal));
    result.density = dot(typeWeights, vec3(stratus, cumulus, towering) * vec3(0.85, 1.0, 1.08));
    return result;
}

#endif
