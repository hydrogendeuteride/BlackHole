# Rocket Plumes

The game API exposes dynamically growing analytic rocket plume slots. The underlying renderer raymarches a procedural plume in plume-local space.

## Relevant Types

- `GameAPI::RocketPlumeSettings`
- `GameAPI::Engine::set_rocket_plumes_enabled(...)`
- `GameAPI::Engine::get_rocket_plume(...)`
- `GameAPI::Engine::set_rocket_plume(...)`
- `GameAPI::Engine::get_rocket_plume_count()`
- `GameAPI::Engine::set_rocket_plume_noise_texture_path(...)`

See `src/core/game_api.h`.

## Coordinate Model

`worldToPlume` defines the plume's local frame:

- `+Z` is the exhaust direction
- `z = 0` is the nozzle exit plane
- the matrix is expressed in world space
- the matrix remains double precision until floating-origin compensation is applied by the renderer

## Main Controls

- shape: `length`, `nozzleRadius`, `expansionAngleRad`, `radiusExp`
- emission: `intensity`, `coreColor`, `plumeColor`, `coreLength`, `coreStrength`
- falloff: `radialFalloff`, `axialFalloff`
- noise: `noiseStrength`, `noiseScale`, `noiseSpeed`
- outer shear layer: `sheathStrength`, `sheathRadius`, `sheathWidth`
- shock diamonds: `shockStrength`, `shockFrequency`, `shockWidth`, `shockDecay`
- absorption: `softAbsorption`

## Typical Flow

1. enable the plume system
2. choose slot indices (setting a new index grows the list automatically)
3. fill one or more `RocketPlumeSettings`
4. update those settings every frame as the nozzle transform changes

The initial 16 slots are not a limit. `get_rocket_plume_count()` returns the
current slot count, including disabled slots; new intermediate slots are disabled.
Disable unused slots when the active engine count decreases. Actual capacity is
bounded by memory and GPU storage-buffer limits; rendering cost grows with active plumes.

```cpp
api.set_rocket_plumes_enabled(true);

GameAPI::RocketPlumeSettings plume{};
plume.enabled = true;
plume.length = 12.0f;
plume.nozzleRadius = 0.12f;
plume.intensity = 10.0f;

api.set_rocket_plume(0, plume);
```

## Noise Texture

The plume system also has a shared noise texture path:

```cpp
api.set_rocket_plume_noise_texture_path("vfx/noise/plume_noise.ktx2");
```

That path is asset-relative under the normal `assets/` rules.

## Rocket Plume Lab

Use the renderer controls or a project-specific debug panel to tune the effect.
Full-throttle presets and throttle scaling belong to the consuming application;
the renderer owns only plume rendering and its reusable parameters.

For isolated previews, the corresponding background API is
`set_plain_background(enabled, linear_rgb)`. Query it with
`get_plain_background(color)`; the return value reports whether the override is
enabled.

## Related Docs

- [../GameAPI.md](../GameAPI.md)
- [Volumetrics.md](Volumetrics.md)
