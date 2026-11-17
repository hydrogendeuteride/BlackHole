// Adapted from https://github.com/hydrogendeuteride/BlackHoleRayTracer/blob/master/shader/blackhole.comp
// u = rs/r, u'' = -u + 1.5*u*u.
layout(set = 0, binding = 6) uniform sampler3D ray_lut;

float acceleration(float u)
{
    return -u * (1.0 - 1.5 * u);
}

void step_ray(inout float u, inout float du, float dt)
{
    float a = acceleration(u);
    float b = acceleration(u + 0.5 * dt * du);
    float c = acceleration(u + 0.5 * dt * (du + 0.5 * dt * a));
    float d = acceleration(u + dt * (du + 0.5 * dt * b));
    u += dt / 6.0 * (6.0 * du + dt * (a + b + c));
    du += dt / 6.0 * (a + 2.0 * b + 2.0 * c + d);
}

void step_ray_lut(inout float u, inout float du, float dt, float max_step, float max_travel)
{
    float travel = dt * du / u;
    // Preserve integration for states outside the precomputed domain.
    if (u <= 0.0 || u > 1.0 || dt > max_step || abs(travel) > max_travel * 1.00001)
    {
        step_ray(u, du, dt);
        return;
    }
    vec3 size = vec3(textureSize(ray_lut, 0));
    vec3 coord = vec3(u, 0.5 + 0.5 * travel / max_travel, dt * dt / (max_step * max_step));
    vec2 correction = textureLod(ray_lut, (coord * (size - 1.0) + 0.5) / size, 0.0).rg;
    float old_u = u;
    u += dt * du + old_u * dt * dt * correction.x;
    du += old_u * dt * correction.y;
}
