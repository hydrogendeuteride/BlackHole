# CIE colour matching data

`CIE_xyz_1931_2deg.csv` is the unmodified 1 nm, 360–830 nm CIE 1931
2 degree observer table from the International Commission on Illumination.

CIE 2019, *Colour-matching functions of CIE 1931 standard colorimetric observer*,
International Commission on Illumination (CIE), Vienna, AT.
DOI: https://doi.org/10.25039/CIE.DS.xvudnb9b

- Source: https://cie.co.at/datatable/cie-1931-colour-matching-functions-2-degree-observer
- Download: https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv
- Metadata: https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv_metadata.json
- License: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
- SHA-256: `fa663e3535a7e0763a745993a1f0a192eb0275ac46ad2d1befd7626841e713c1`

Run `py tools/generate_blackbody.py` to integrate Planck spectra and regenerate
`shaders/blackhole/blackbody.glsl`. That derived lookup table is also CC BY-SA 4.0.
The adaptation converts XYZ to linear sRGB, normalizes chromaticity to unit
luminance, and stores log luminance separately. The table is intentionally
tracked so ordinary builds need neither network access nor regeneration.
