layout(set = 0, binding = 7) uniform sampler2D blackbody_lut;

vec4 blackbody_sample(float temperature)
{
    float index = log(clamp(temperature, 500.0, 1000000.0) / 500.0) * (511.0 / log(2000.0));
    int lo = min(int(index), 510);
    // Preserve float32 values and the original interpolation on all GPUs,
    // including devices without linear filtering for this texture format.
    return mix(texelFetch(blackbody_lut, ivec2(lo, 0), 0),
               texelFetch(blackbody_lut, ivec2(lo + 1, 0), 0), index - float(lo));
}
