#!/usr/bin/env python3
import argparse
import math
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


def u32(v):
    return v & 0xFFFFFFFF


def hash3(x, y, z, seed):
    h = u32(seed ^ 0x9E3779B9)
    h = u32((h ^ u32(x * 0x85EBCA6B)) * 0xC2B2AE35)
    h = u32((h ^ u32(y * 0x27D4EB2F)) * 0x165667B1)
    h = u32((h ^ u32(z * 0xD3A2646C)) * 0x9E3779B9)
    return u32(h ^ (h >> 16))


def rand01(h):
    return (h & 0x00FFFFFF) / float(0x01000000)


def fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def lerp(a, b, t):
    return a + (b - a) * t


def value_noise(x, y, z, period, seed):
    x0 = math.floor(x)
    y0 = math.floor(y)
    z0 = math.floor(z)
    xf = x - x0
    yf = y - y0
    zf = z - z0

    def val(ix, iy, iz):
        return rand01(hash3(ix % period, iy % period, iz % period, seed)) * 2.0 - 1.0

    n000 = val(x0, y0, z0)
    n100 = val(x0 + 1, y0, z0)
    n010 = val(x0, y0 + 1, z0)
    n110 = val(x0 + 1, y0 + 1, z0)
    n001 = val(x0, y0, z0 + 1)
    n101 = val(x0 + 1, y0, z0 + 1)
    n011 = val(x0, y0 + 1, z0 + 1)
    n111 = val(x0 + 1, y0 + 1, z0 + 1)

    u = fade(xf)
    v = fade(yf)
    w = fade(zf)
    nx00 = lerp(n000, n100, u)
    nx10 = lerp(n010, n110, u)
    nx01 = lerp(n001, n101, u)
    nx11 = lerp(n011, n111, u)
    return lerp(lerp(nx00, nx10, v), lerp(nx01, nx11, v), w)


def fbm(u, v, w, seed, octaves):
    amp = 0.5
    total = 0.0
    norm = 0.0
    for freq in octaves:
        total += amp * value_noise(u * freq, v * freq, w * freq, freq, seed + freq * 31)
        norm += amp
        amp *= 0.5
    return total / max(norm, 1e-6)


def feature(ix, iy, iz, period, seed):
    x = ix % period
    y = iy % period
    z = iz % period
    return (
        x + rand01(hash3(x, y, z, seed)),
        y + rand01(hash3(x, y, z, seed + 17)),
        z + rand01(hash3(x, y, z, seed + 37)),
    )


def worley_f1(u, v, w, period, seed):
    x = u * period
    y = v * period
    z = w * period
    ix = math.floor(x)
    iy = math.floor(y)
    iz = math.floor(z)
    best = 1e9
    for dz in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                cx = ix + dx
                cy = iy + dy
                cz = iz + dz
                fx, fy, fz = feature(cx, cy, cz, period, seed)
                fx += math.floor(cx / period) * period
                fy += math.floor(cy / period) * period
                fz += math.floor(cz / period) * period
                best = min(best, (fx - x) ** 2 + (fy - y) ** 2 + (fz - z) ** 2)
    return min(1.0, math.sqrt(best) / 1.35)


def worley(u, v, w, seed):
    return 0.6 * worley_f1(u, v, w, 4, seed) + 0.4 * worley_f1(u, v, w, 8, seed + 53)


def clamp01(v):
    return min(1.0, max(0.0, v))


def smoothstep(a, b, x):
    t = clamp01((x - a) / (b - a))
    return t * t * (3.0 - 2.0 * t)


def png_gray(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        row = y * width
        raw.extend(pixels[row:row + width])

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    path.write_bytes(b"".join([
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
        chunk(b"IEND", b""),
    ]))


def png_weather_rg(path, width, height, red, green):
    # KTX imports gray/alpha PNG channels into R8G8 as R/G.
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        row = y * width
        for x in range(width):
            i = row + x
            raw.extend((red[i], green[i]))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    path.write_bytes(b"".join([
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 4, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
        chunk(b"IEND", b""),
    ]))


def make_weather(size, seed):
    coverage_vals = []
    type_vals = []
    for y in range(size):
        v = y / size
        for x in range(size):
            u = x / size
            n = fbm(u, v, 0.0, seed, (2, 4, 8, 16, 32)) * 0.7 + 0.5
            coverage_vals.append(smoothstep(0.12, 0.92, n))

            # Independent, lower-frequency field: 0 = stratus, 0.5 = cumulus,
            # 1 = towering. Keep it away from exact zero so R8 legacy maps can
            # be detected in the shader by their missing green channel.
            type_low = fbm(u, v, 0.0, seed + 7919, (2, 4, 8)) * 0.5 + 0.5
            type_mid = fbm(u, v, 0.0, seed + 104729, (4, 8)) * 0.5 + 0.5
            cloud_type = smoothstep(0.12, 0.88, 0.82 * type_low + 0.18 * type_mid)
            type_vals.append(0.05 + 0.90 * cloud_type)

    mn = min(coverage_vals)
    mx = max(coverage_vals)
    inv = 1.0 / max(mx - mn, 1e-6)
    coverage = bytes(int(clamp01((value - mn) * inv) * 255.0 + 0.5) for value in coverage_vals)
    cloud_type = bytes(int(clamp01(value) * 255.0 + 0.5) for value in type_vals)
    return coverage, cloud_type


def make_volume(size, seed):
    pixels = bytearray(size * size * size)
    i = 0
    for z in range(size):
        w = z / size
        for y in range(size):
            v = y / size
            for x in range(size):
                u = x / size
                p = fbm(u, v, w, seed, (4, 8, 16)) * 0.72 + 0.5
                cell = worley(u, v, w, seed + 101)
                perlin_worley = clamp01((p - cell * 0.34) / max(1.0 - cell * 0.34, 1e-4))
                detail = smoothstep(0.08, 0.95, 0.65 * perlin_worley + 0.35 * (1.0 - cell))
                pixels[i] = int(detail * 255.0 + 0.5)
                i += 1
    return bytes(pixels)


def run(cmd):
    print(" ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True)


def create_ktx_2d(png_path, ktx_path):
    ktx_path.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="cloud-weather-") as temp_dir:
        encoded_path = Path(temp_dir) / "weather_uastc.ktx2"
        run([
            "ktx", "create",
            "--format", "R8G8_UNORM",
            "--encode", "uastc",
            "--uastc-quality", "2",
            "--assign-tf", "linear",
            "--generate-mipmap",
            "--mipmap-filter", "box",
            "--mipmap-wrap", "wrap",
            str(png_path),
            str(encoded_path),
        ])
        run(["ktx", "transcode", "--target", "bc5", str(encoded_path), str(ktx_path)])


def downsample_volume_box(volume, size):
    next_size = max(1, size // 2)
    result = bytearray(next_size * next_size * next_size)
    dst = 0
    for z in range(next_size):
        for y in range(next_size):
            for x in range(next_size):
                total = 0
                for dz in (0, 1):
                    sz = (z * 2 + dz) % size
                    for dy in (0, 1):
                        sy = (y * 2 + dy) % size
                        for dx in (0, 1):
                            sx = (x * 2 + dx) % size
                            total += volume[(sz * size + sy) * size + sx]
                result[dst] = (total + 4) // 8
                dst += 1
    return bytes(result), next_size


def volume_mip_chain(volume, size):
    levels = [(volume, size)]
    while size > 1:
        volume, size = downsample_volume_box(volume, size)
        levels.append((volume, size))
    return levels


def create_ktx_3d(volume, size, ktx_path):
    ktx_path.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="cloud-volume-slices-") as temp_dir:
        temp = Path(temp_dir)
        slices = []
        levels = volume_mip_chain(volume, size)
        for level, (level_volume, level_size) in enumerate(levels):
            slice_len = level_size * level_size
            for z in range(level_size):
                path = temp / f"level_{level:02d}_slice_{z:03d}.png"
                png_gray(path,
                         level_size,
                         level_size,
                         level_volume[z * slice_len:(z + 1) * slice_len])
                slices.append(str(path))

        run([
            "ktx", "create",
            "--format", "R8_UNORM",
            "--depth", str(size),
            "--levels", str(len(levels)),
            "--assign-tf", "linear",
            "--assign-texcoord-origin", "top-left-front",
            *slices,
            str(ktx_path),
        ])


def main():
    ap = argparse.ArgumentParser(description="Generate tileable cloud weather and 3D noise KTX2 assets.")
    ap.add_argument("--out-dir", default="assets/vfx")
    ap.add_argument("--weather-size", type=int, default=512)
    ap.add_argument("--volume-size", type=int, default=64)
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--weather-only", action="store_true")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    weather_stem = f"cloud_weather_perlin_{args.weather_size}"
    volume_stem = f"cloud_perlin_worley_{args.volume_size}_r8"
    weather_png = out_dir / f"{weather_stem}.png"
    weather_ktx = out_dir / f"{weather_stem}.ktx2"
    volume_ktx = out_dir / f"{volume_stem}.ktx2"

    coverage, cloud_type = make_weather(args.weather_size, args.seed)
    png_weather_rg(weather_png, args.weather_size, args.weather_size, coverage, cloud_type)
    create_ktx_2d(weather_png, weather_ktx)

    if args.weather_only:
        return

    volume = make_volume(args.volume_size, args.seed + 19)
    create_ktx_3d(volume, args.volume_size, volume_ktx)


if __name__ == "__main__":
    main()
