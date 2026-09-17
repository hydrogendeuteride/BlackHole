#pragma once

#ifdef NDEBUG
inline constexpr bool kUseValidationLayers = false;
#else
inline constexpr bool kUseValidationLayers = true;
#endif

// VMA diagnostics (stats prints + JSON dumps + allocation naming)
// - Default: disabled to avoid noise and I/O at shutdown.
// - Enable at runtime by setting environment variable `VE_VMA_DEBUG=1`.
#include <cstdlib>
#include <cstdint>
inline constexpr bool kEnableVmaDebugByDefault = false;
inline bool vmaDebugEnabled()
{
    const char *env = std::getenv("VE_VMA_DEBUG");
    if (env && *env)
    {
        // Accept 1/true/yes (case-insensitive)
        return (*env == '1') || (*env == 'T') || (*env == 't') || (*env == 'Y') || (*env == 'y');
    }
    return kEnableVmaDebugByDefault;
}

// Fixed logical render resolution for letterboxed viewport.
// Internal rendering and camera aspect will target this size
// even when the window/swapchain size changes.
inline constexpr uint32_t kRenderWidth  = 1920;
inline constexpr uint32_t kRenderHeight = 1080;

// Shadow mapping configuration
inline constexpr int kShadowCascadeCount = 4;
// Maximum shadow distance for CSM in view-space units
inline constexpr float kShadowCSMFar = 800.0f;
// Default shadow map resolution (square) used for stabilization (texel snapping) and image allocation.
// Actual runtime resolution may override this via EngineContext::ShadowSettings.
inline constexpr uint32_t kShadowMapResolution = 2048;
// Extra XY expansion for cascade footprint (safety against FOV/aspect changes)
inline constexpr float kShadowCascadeRadiusScale = 1.1f;
// Additive XY margin in world units beyond the scaled half-size
inline constexpr float kShadowCascadeRadiusMargin = 2.0f;
// CSM fitting configuration.
// Minimum stable radius for the nearest frustum slice fit.
inline constexpr float kShadowClipBaseRadius = 20.0f;
// Pullback and forward range are computed from each fitted cascade footprint.
inline constexpr float kShadowClipPullbackFactor = 1.1f;   // fraction of XY half-size behind center
inline constexpr float kShadowClipForwardFactor  = 1.1f;   // fraction of XY half-size in front of center for zFar
inline constexpr float kShadowClipPullbackMin    = 1.0f;   // lower bound so near levels do not collapse
// Additional Z padding for the orthographic frustum along light direction
inline constexpr float kShadowClipZPadding = 5.0f;

// Raster depth-bias parameters for punctual shadow maps.
inline constexpr float kShadowDepthBiasConstant = 1.0f;
inline constexpr float kShadowDepthBiasSlope    = 1.0f;

// Texture streaming / VRAM budget configuration
// Fraction of total device-local VRAM reserved for streamed textures.
// The remaining budget is left for attachments, swapchain images, meshes, AS, etc.
inline constexpr double kTextureBudgetFraction = 0.7;
// Fallback texture budget in bytes when Vulkan memory properties are unavailable.
inline constexpr size_t kTextureBudgetFallbackBytes = 512ull * 1024ull * 1024ull;
// Minimum texture budget clamp in bytes.
inline constexpr size_t kTextureBudgetMinBytes = 128ull * 1024ull * 1024ull;
