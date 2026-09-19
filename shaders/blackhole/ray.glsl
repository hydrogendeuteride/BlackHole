// Adapted from https://github.com/hydrogendeuteride/BlackHoleRayTracer/blob/master/shader/blackhole.comp
// u = rs/r, u'' = -u + 1.5*u*u.
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
