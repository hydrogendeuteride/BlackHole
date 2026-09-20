# HYG star background

`hyg_v41.csv` contains 25,791 stars from **HYG v4.1**, compiled by David Nash / Astronomy Nexus.
Source: https://github.com/astronexus/HYG-Database/tree/c7f7f883fe678cc7680169a50ccd7dcc49b060ce/hyg

The original data and this extracted catalog are licensed under
**Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**:
https://creativecommons.org/licenses/by-sa/4.0/

Changes: retain stars with apparent visual magnitude <= 7.5, exclude the Sun,
retain only HYG ID, RA in hours, declination in degrees, visual magnitude and
B-V color index. Missing B-V values use 0.65. Values are at epoch/equinox J2000.
Reproduce with `py tools/import_stars.py` (network required only for importing).
The reduced CSV is intentionally included as a runtime asset.

The renderer uses measured directions and relative flux `10^(-0.4*magnitude)`.
It treats the catalog as infinitely distant, as seen from Earth; it does not
reconstruct the sky as seen from a different location near a real black hole.
Proper motion, parallax, dust, variability and diffuse Milky Way light are omitted.
B-V is mapped to an illustrative RGB palette with equal luminance normalization.
Star size is a Gaussian display footprint, not a measured stellar diameter.
Brightness is an artistic exposure multiplier. A spherical cell index avoids
scanning the full catalog per ray and covers seams and poles.

`Catalog stars` switches between this sky and the existing city background.
`Lensing` compares straight and bent rays using the same catalog shader.
The existing city IBL still lights the test BMW; stars currently replace only
the visible distant background. Pixel filtering is approximate and does not yet
include the lensing Jacobian, so tightly bent arcs can still shimmer in motion.
