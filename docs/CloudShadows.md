# Cloud shadows

The replacement uses one 256×256 R16_SFLOAT map near the camera's subplanet
point. Cloud Shadows is **off by default** while hardware stability is being
established. The previous near/far implementation is removed. Turning shadows off skips map generation and receiver sampling.

Controls: **View → Engine Debug → PostFX → Planet Clouds**.

- **Cloud Shadows** enables the replacement.
- **Cloud Shadow Strength**: 0–2, default 1.25.
- **Near Shadow Range (km)**: map half extent, 8–2048 km, default 131.072 km.
- **Cloud Shadow Steps**: 4–16 vertical strata, default 8.

The generator samples coverage, weather and cloud shape textures at explicit,
bounded mip levels. It shares wind, overlay rotation, coverage threshold,
weather evolution, density and extinction settings with the visible clouds.
The vertical profile and coverage response match the visible clouds, including
weather-map cloud type and type bias. The 3D column masks use the visible clouds'
metric coordinates, shear, detail scale and evolution, so broad coverage alone
cannot cast shadows through empty columns. Medium-scale erosion also follows
the density remap. When the 3D texture is unavailable, 2D column masks are used.
Each stratum uses at most five texture fetches; micro-detail is omitted.
Map resolution, explicit mip selection and vertical integration still approximate
the visible silhouette, particularly at grazing angles.
Shadows fade at the map edge, so distant
parts of the planet receive no cloud shadow. There is no temporal history.

CPU projection and angle reduction use double precision. The generator works
in planet-radius units rather than subtracting large float world positions.
Shell path lengths use a rationalized difference of square roots, and height
comes from the integration stratum. Grazing columns fade between incidence
cosines 0.05 and 0.15. The loop has a hard limit of 16, optical depth is capped
at 10, texture LODs are clamped, poles have a defined longitude, and invalid
samples return full visibility. Unsupported shell ratios (thickness below
1e-6 or above 0.25 of planet radius; base above 0.25) disable the pass.

The render graph declares texture and parameter-buffer reads and the map write/read dependency.
Maps and descriptor sets belong to frame resources. Texture reload uploads
finish before publishing sampled views; replaced textures and resized cloud
history images are retired after a frame fence rather than destroyed in flight.

Land and ocean share the map. Only direct sunlight terms are attenuated;
night-side receivers and objects above the cloud layer remain unaffected.
Successful compilation or software shader execution does not establish that
the observed hardware blackout is fixed; that needs a run on the affected GPU.
