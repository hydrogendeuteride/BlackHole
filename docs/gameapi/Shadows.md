# Shadow System

Control shadow rendering modes, quality, and resolution for directional (sun) lighting.

## Shadow Modes

```cpp
enum class ShadowMode : uint32_t
{
    ClipmapOnly = 0,   // CSM raster shadow maps with PCF
    ClipmapPlusRT = 1, // CSM + ray-traced assist at low N.L angles
    RTOnly = 2         // Pure ray-traced shadows (no shadow maps)
};
```

The enum names still use the historical "Clipmap" labels for API compatibility. The raster path now uses camera-frustum cascaded shadow maps (CSM): each cascade fits a camera depth slice, snaps to the light-space texel grid, and blends near split boundaries.

### Setting Shadow Mode

```cpp
// Raster CSM only (fastest, good for most scenes)
api.set_shadow_mode(GameAPI::ShadowMode::ClipmapOnly);

// Hybrid (CSM + RT assist for grazing-angle shadows)
api.set_shadow_mode(GameAPI::ShadowMode::ClipmapPlusRT);

// Pure ray-traced (best quality, requires RT support)
api.set_shadow_mode(GameAPI::ShadowMode::RTOnly);

// Query current mode
GameAPI::ShadowMode mode = api.get_shadow_mode();
```

## Enable/Disable Shadows

```cpp
api.set_shadows_enabled(true);
bool enabled = api.get_shadows_enabled();
```

## Shadow Map Resolution

Shadow map resolution is configured via `kShadowMapResolution` in `src/core/config.h` (default `2048`).

```cpp
// Runtime query only (set via config constant)
uint32_t resolution = api.get_shadow_map_resolution();
```

**VRAM Impact:**
- Each cascade uses depth format D32F (4 bytes per pixel)
- With 4 cascades:
  - 2048×2048: ~64 MB total
  - 4096×4096: ~256 MB total
  - 8192×8192: ~1 GB total

**Performance Notes:**
- Higher resolution = better quality but higher VRAM usage
- On VRAM-constrained systems, reduce `kTextureBudgetFraction` if using large shadow maps
- Allocation failures can prevent shadows from rendering

**Recommended Values:**
- Consumer GPUs: `2048` or `3072`
- High-end GPUs: `4096`
- Integrated GPUs: `1024` or `2048`

## Cascade Distribution

Directional raster shadows use four CSM cascades by default. Split distances are generated from camera near depth `0.1`, `kShadowCSMFar` (default `800`), and a practical split lambda tuned toward the near field. With the current defaults, cascade 0 covers roughly the first 15 view-space units before blending into cascade 1.

Each cascade fits the camera frustum slice with a stable sphere, expands it by `kShadowCascadeRadiusScale` and `kShadowCascadeRadiusMargin`, then snaps the light-space center to shadow texels. This spends more texels on visible geometry than the older fixed-radius clipmap layout while keeping shimmer low.

## Shadow Quality Settings

### Minimum Visibility

Control the brightness floor for shadowed areas (prevents pitch-black shadows):

```cpp
// 0.0 = fully dark shadows (default)
api.set_shadow_min_visibility(0.0f);

// 0.2 = shadows retain 20% ambient light
api.set_shadow_min_visibility(0.2f);

// Query current value
float minVis = api.get_shadow_min_visibility();
```

Useful for artistic control and ensuring detail remains visible in shadow.

## Hybrid Ray-Traced Settings

When using `ShadowMode::ClipmapPlusRT`, control which CSM cascades use ray-traced assist:

### Cascade Mask

```cpp
// Enable RT assist for cascade 0 only (bitmask: bit 0)
api.set_hybrid_ray_cascade_mask(0b0001);

// Enable RT for cascades 0 and 1 (bits 0-1)
api.set_hybrid_ray_cascade_mask(0b0011);

// Enable RT for all 4 cascades (bits 0-3)
api.set_hybrid_ray_cascade_mask(0b1111);

// Query current mask
uint32_t mask = api.get_hybrid_ray_cascade_mask();
```

**Performance Tip:** Use RT only for the first cascade (closest to camera) to balance quality and cost.

### N·L Threshold

Control when RT assist kicks in based on surface-to-light angle:

```cpp
// Only use RT when N·L < 0.3 (grazing angles)
api.set_hybrid_ray_threshold(0.3f);

// More aggressive (use RT when N·L < 0.5)
api.set_hybrid_ray_threshold(0.5f);

// Query current threshold
float threshold = api.get_hybrid_ray_threshold();
```

Lower thresholds = RT used less often (better performance). Higher thresholds = RT used more (better quality at grazing angles).

## Additional Quality Constants

These are set in `src/core/config.h` and require recompilation:

- `kShadowCascadeCount` - Number of cascades (default `4`)
- `kShadowCSMFar` - Far plane for cascade distribution
- `kShadowCascadeRadiusScale` / `kShadowCascadeRadiusMargin` - Cascade sizing
- `kShadowClipBaseRadius` - Minimum stable radius for the nearest CSM slice
- `kShadowClipPullbackFactor` / `kShadowClipForwardFactor` / `kShadowClipZPadding` - Light frustum depth coverage
- Shader constants in `deferred_lighting*.frag` - PCF radius, split blend width, receiver-plane bias
- `kShadowDepthBiasConstant` / `kShadowDepthBiasSlope` - Punctual shadow-map caster bias

## Complete Example

```cpp
GameAPI::Engine api(&engine);

// Setup shadows
api.set_shadows_enabled(true);
api.set_shadow_mode(GameAPI::ShadowMode::ClipmapPlusRT);

// Use RT assist only for first cascade
api.set_hybrid_ray_cascade_mask(0b0001);
api.set_hybrid_ray_threshold(0.3f);

// Soften shadows slightly
api.set_shadow_min_visibility(0.15f);

// Setup sun
api.set_sunlight_direction(glm::vec3(0.3f, -0.8f, 0.5f));
api.set_sunlight_color(glm::vec3(1.0f, 0.95f, 0.85f), 1.0f);
```

## Troubleshooting

### Shadows Not Appearing

1. Check `api.get_shadows_enabled()` returns `true`
2. Verify sun direction is set: `api.get_sunlight_direction()`
3. Ensure VRAM budget allows shadow map allocation
4. Check console for VMA allocation errors

### Shadow Acne / Peter Panning

CSM uses a small caster bias in `src/render/passes/shadow.cpp` plus receiver-side bias in `deferred_lighting*.frag`. Punctual lights use `kShadowDepthBiasConstant` / `kShadowDepthBiasSlope` from `src/core/config.h`.

- Increase receiver or caster bias to reduce acne.
- Decrease bias if contact shadows detach or look too thin.
- Keep CSM and punctual bias tuned separately; they cover different depth ranges.

### Flickering Shadows

Usually caused by cascade fitting, split transitions, or texel snapping. Solutions:
- Increase shadow map resolution
- Adjust `kShadowCascadeRadiusMargin` for more stable cascades
- Widen the split blend band in `deferred_lighting*.frag` if transitions are visible
- Reduce camera movement speed

### Performance Issues

- Lower shadow map resolution in config
- Use `ShadowMode::ClipmapOnly` (CSM only) instead of hybrid/RT modes
- Reduce hybrid RT cascade mask (e.g., only first cascade)
- Increase hybrid RT threshold to use RT less often

## See Also

- [Lighting](Lighting.md) - Directional light setup
- [Planets](Planets.md) - Sun shadow penumbra settings for planet shadows
- [Ray Tracing](../RayTracing.md) - RT backend details
- [Config](../../src/core/config.h) - Shadow quality constants
