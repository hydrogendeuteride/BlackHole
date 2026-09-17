vec3 sky_view_safe_up(vec3 camLocal, vec3 center)
{
    vec3 up = camLocal - center;
    float len2 = dot(up, up);
    return (len2 > 1e-8) ? (up * inversesqrt(len2)) : vec3(0.0, 1.0, 0.0);
}

float sky_view_camera_radius(vec3 camLocal, vec3 center)
{
    return length(camLocal - center);
}

float sky_view_horizon_mu(vec3 camLocal, vec3 center, float planetRadius)
{
    float r = max(sky_view_camera_radius(camLocal, center), planetRadius + 1.0);
    float q = clamp(planetRadius / r, 0.0, 1.0);
    return -sqrt(max(1.0 - q * q, 0.0));
}

float sky_view_mu_to_v(float mu, float horizonMu)
{
    mu = clamp(mu, -1.0, 1.0);
    horizonMu = clamp(horizonMu, -0.999, 0.999);

    if (mu < horizonMu)
    {
        float t = clamp((mu + 1.0) / max(horizonMu + 1.0, 1e-4), 0.0, 1.0);
        return 0.5 * (1.0 - sqrt(max(1.0 - t, 0.0)));
    }

    float t = clamp((mu - horizonMu) / max(1.0 - horizonMu, 1e-4), 0.0, 1.0);
    return 0.5 + 0.5 * sqrt(t);
}

float sky_view_v_to_mu(float v, float horizonMu)
{
    v = clamp(v, 0.0, 1.0);
    horizonMu = clamp(horizonMu, -0.999, 0.999);

    if (v < 0.5)
    {
        float a = 1.0 - 2.0 * v;
        float t = 1.0 - a * a;
        return mix(-1.0, horizonMu, clamp(t, 0.0, 1.0));
    }

    float t = 2.0 * v - 1.0;
    return mix(horizonMu, 1.0, t * t);
}

void sky_view_basis(vec3 up, vec3 sunDir, out vec3 sunPerp, out vec3 side)
{
    sunDir = normalize(sunDir);
    sunPerp = sunDir - up * dot(sunDir, up);
    float sunPerpLen2 = dot(sunPerp, sunPerp);
    if (sunPerpLen2 < 1e-8)
    {
        vec3 refAxis = mix(vec3(0.0, 1.0, 0.0),
                           vec3(1.0, 0.0, 0.0),
                           step(0.999, abs(up.y)));
        sunPerp = normalize(cross(refAxis, up));
    }
    else
    {
        sunPerp *= inversesqrt(sunPerpLen2);
    }

    side = normalize(cross(up, sunPerp));
}

vec2 sky_view_lut_uv(vec3 camLocal, vec3 center, float planetRadius, vec3 rd, vec3 sunDir)
{
    vec3 up = sky_view_safe_up(camLocal, center);
    rd = normalize(rd);
    sunDir = normalize(sunDir);

    float mu = clamp(dot(rd, up), -1.0, 1.0);
    float nu = clamp(dot(rd, sunDir), -1.0, 1.0);
    float horizonMu = sky_view_horizon_mu(camLocal, center, planetRadius);
    return vec2(nu * 0.5 + 0.5, sky_view_mu_to_v(mu, horizonMu));
}

vec3 sky_view_lut_direction(vec2 uv, vec3 camLocal, vec3 center, float planetRadius, vec3 sunDir)
{
    vec3 up = sky_view_safe_up(camLocal, center);
    sunDir = normalize(sunDir);

    float nu = clamp(uv.x * 2.0 - 1.0, -1.0, 1.0);
    float horizonMu = sky_view_horizon_mu(camLocal, center, planetRadius);
    float mu = sky_view_v_to_mu(uv.y, horizonMu);
    float muS = clamp(dot(sunDir, up), -1.0, 1.0);

    vec3 sunPerp;
    vec3 side;
    sky_view_basis(up, sunDir, sunPerp, side);

    float sinView = sqrt(max(1.0 - mu * mu, 0.0));
    float sinSun = sqrt(max(1.0 - muS * muS, 0.0));
    float x = 0.0;
    if (sinSun > 1e-4)
    {
        x = (nu - mu * muS) / sinSun;
    }
    x = clamp(x, -sinView, sinView);

    float y = sqrt(max(sinView * sinView - x * x, 0.0));
    return normalize(up * mu + sunPerp * x + side * y);
}
