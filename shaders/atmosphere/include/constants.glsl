const float PI = 3.14159265359;
const float INV_TWO_PI = 0.15915494309189535;
const float INV_PI = 0.3183098861837907;

const int FLAG_ATMOSPHERE = 1;
const int FLAG_CLOUDS = 2;
const int FLAG_CLOUD_FLIP_V = 4;
const int FLAG_CLOUD_NOISE_3D = 8;
const int FLAG_JITTER_BLUE_NOISE = 16;
const uint MISC_FLAGS_MASK = 0xFFu;
const uint MISC_NOISE_BLEND_SHIFT = 8u;
const uint MISC_DETAIL_ERODE_SHIFT = 16u;
const uint MISC_JITTER_FRAME_SHIFT = 24u;

const float CLOUD_AMBIENT_SCALE = 0.16;
const float CLOUD_PHASE_G = 0.80;
const float CLOUD_PHASE_G_BACK = -0.15;
const float CLOUD_PHASE_LOBE_MIX = 0.50;
// Multi-scattering octaves (Wrenninge): per-octave scatter weight, extinction scale, phase-g scale.
const vec3 CLOUD_MS_SCATTER = vec3(1.0, 0.50, 0.25);
const vec3 CLOUD_MS_EXTINCT = vec3(1.0, 0.50, 0.25);
const vec3 CLOUD_MS_PHASE_G = vec3(1.0, 0.50, 0.25);
const float CLOUD_POWDER_MIN = 0.35;
const float CLOUD_SCATTER_SCALE = 2.0;
const float CLOUD_SUN_WHITEN = 0.65;
const float CLOUD_AMBIENT_WHITEN = 0.85;
const float CLOUD_AMBIENT_MIN_LUMA = 0.22;
const float CLOUD_SLICE_HEIGHT_FREQ = 1.65;
const float CLOUD_EROSION_FREQ = 4.0;
const float CLOUD_EROSION_STRENGTH = 0.45;
const float CLOUD_SHEAR_MIN = 0.018;
const float CLOUD_SHEAR_MAX = 0.080;
const float CLOUD_DETAIL_REFERENCE = 12.0;
const float CLOUD_SHAPE_TILE_M = 480000.0;
const float CLOUD_DETAIL_TILE_M = 120000.0;
const float CLOUD_MICRO_TILE_M = 40000.0;
const float CLOUD_DETAIL_LOD_FADE_START = 1.5;
const float CLOUD_DETAIL_LOD_FADE_END = 4.0;
const float CLOUD_MICRO_LOD_FADE_START = 0.0;
const float CLOUD_MICRO_LOD_FADE_END = 2.0;
const int CLOUD_LIGHT_STEPS = 2;
const float CLOUD_LIGHT_NEAR_SPLIT = 0.25;
const float CLOUD_LOCAL_SHADOW_MIN = 0.68;
const float CLOUD_TERRAIN_FAST_TOLERANCE_FRAC = 0.02;
const float CLOUD_TERRAIN_FAST_TOLERANCE_MIN_M = 25.0;
const float CLOUD_TERRAIN_FAST_GRAZE_MIN_COS = 0.20;
float luminance(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 rotate_y_sc(vec3 v, vec2 sc)
{
    float s = sc.x;
    float c = sc.y;
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vec2 dir_to_equirect(vec3 d, bool flipV)
{
    d = normalize(d);
    float u = atan(d.z, d.x) * INV_TWO_PI + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) * INV_PI;
    if (flipV) v = 1.0 - v;
    return vec2(fract(u), clamp(v, 0.001, 0.999));
}

bool ray_sphere_intersect(vec3 ro, vec3 rd, vec3 center, float radius, out float t0, out float t1)
{
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return false;
    h = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
}
