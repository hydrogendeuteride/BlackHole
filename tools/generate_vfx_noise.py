#!/usr/bin/env python3
from __future__ import annotations

import math
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VFX = ROOT / "assets" / "vfx"
SIZE = 512


def fade(t: float) -> float:
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def hash2(x: int, y: int, seed: int) -> int:
    h = (x * 0x8DA6B343) ^ (y * 0xD8163841) ^ (seed * 0xCB1AB31F)
    h ^= (h >> 13)
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= (h >> 16)
    return h


def grad(x: int, y: int, seed: int) -> tuple[float, float]:
    a = (hash2(x, y, seed) & 0xFFFF) / 65536.0 * math.tau
    return math.cos(a), math.sin(a)


def perlin(x: float, y: float, period: int, seed: int) -> float:
    x0 = math.floor(x)
    y0 = math.floor(y)
    xf = x - x0
    yf = y - y0
    x1 = x0 + 1
    y1 = y0 + 1

    gx00, gy00 = grad(x0 % period, y0 % period, seed)
    gx10, gy10 = grad(x1 % period, y0 % period, seed)
    gx01, gy01 = grad(x0 % period, y1 % period, seed)
    gx11, gy11 = grad(x1 % period, y1 % period, seed)

    n00 = gx00 * xf + gy00 * yf
    n10 = gx10 * (xf - 1.0) + gy10 * yf
    n01 = gx01 * xf + gy01 * (yf - 1.0)
    n11 = gx11 * (xf - 1.0) + gy11 * (yf - 1.0)

    u = fade(xf)
    v = fade(yf)
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v)


def fbm(u: float, v: float, seed: int, freqs: list[int], rough: bool) -> float:
    total = 0.0
    amp = 1.0
    norm = 0.0
    for i, freq in enumerate(freqs):
        n = perlin(u * freq, v * freq, freq, seed + i * 101)
        if rough and i > 1:
            n = 1.0 - abs(n * 2.0)
        total += n * amp
        norm += amp
        amp *= 0.52
    return total / max(norm, 1e-6)


def make_channel(seed: int, kind: str) -> bytes:
    vals: list[float] = []
    if kind == "perlin":
        freqs = [4, 8, 16, 32, 64]
        rough = False
    else:
        freqs = [6, 12, 24, 48, 96]
        rough = True

    for y in range(SIZE):
        v = y / SIZE
        for x in range(SIZE):
            u = x / SIZE
            warp_x = fbm(u, v, seed + 701, [3, 6, 12], False) * 0.11
            warp_y = fbm(u, v, seed + 977, [3, 6, 12], False) * 0.11
            n = fbm((u + warp_x) % 1.0, (v + warp_y) % 1.0, seed, freqs, rough)
            vals.append(n)

    lo = min(vals)
    hi = max(vals)
    scale = 255.0 / max(hi - lo, 1e-6)
    return bytes(max(0, min(255, round((v - lo) * scale))) for v in vals)


def interleave_rg(r: bytes, g: bytes) -> bytes:
    out = bytearray(len(r) * 2)
    out[0::2] = r
    out[1::2] = g
    return bytes(out)


def run(args: list[str]) -> None:
    print(" ".join(str(a) for a in args))
    subprocess.run(args, check=True)


def ktx_tool() -> str:
    found = shutil.which("ktx") or shutil.which("ktx.exe")
    if found:
        return found
    fallback = Path("C:/Program Files/KTX-Software/bin/ktx.exe")
    if fallback.exists():
        return str(fallback)
    raise RuntimeError("ktx executable not found")


def make_raw_ktx(ktx: str, name: str, raw: bytes, fmt: str) -> None:
    raw_path = VFX / f".tmp_{name}.raw"
    out_path = VFX / f"{name}.ktx2"
    raw_path.write_bytes(raw)
    try:
        run([
            ktx, "create",
            "--format", fmt,
            "--raw",
            "--width", str(SIZE),
            "--height", str(SIZE),
            "--assign-tf", "linear",
            "--input-swizzle", "rg01",
            str(raw_path),
            str(out_path),
        ])
    finally:
        raw_path.unlink(missing_ok=True)


def make_bc(ktx: str, name: str, raw: bytes, fmt: str, target: str) -> None:
    raw_path = VFX / f".tmp_{name}.raw"
    basis_path = VFX / f".tmp_{name}_basis.ktx2"
    out_path = VFX / f"{name}.ktx2"
    raw_path.write_bytes(raw)
    try:
        channels = "2" if fmt == "R8G8_UNORM" else "1"
        run([
            ktx, "create",
            "--format", fmt,
            "--raw",
            "--width", str(SIZE),
            "--height", str(SIZE),
            "--assign-tf", "linear",
            "--encode", "basis-lz",
            "--input-swizzle", "rg01" if channels == "2" else "r001",
            str(raw_path),
            str(basis_path),
        ])
        run([ktx, "transcode", "--target", target, str(basis_path), str(out_path)])
    finally:
        raw_path.unlink(missing_ok=True)
        basis_path.unlink(missing_ok=True)


def restore_stbn(ktx: str) -> None:
    zip_path = VFX / "STBN.zip"
    stbn_dir = VFX / "STBN"
    if not zip_path.exists():
        print("STBN.zip not found; skipping STBN volume")
        return

    stbn_dir.mkdir(parents=True, exist_ok=True)
    prefix = "STBN/stbn_scalar_2Dx1Dx1D_128x128x64x1_"
    slices: list[Path] = []
    with zipfile.ZipFile(zip_path) as zf:
        for i in range(64):
            member = f"{prefix}{i}.png"
            dst = stbn_dir / Path(member).name
            dst.write_bytes(zf.read(member))
            slices.append(dst)

    top_slice = VFX / "stbn_scalar_2Dx1Dx1D_128x128x64x1_6.png"
    top_slice.write_bytes(slices[6].read_bytes())

    out_path = VFX / "stbn_scalar_128x128x64_r8.ktx2"
    run([
        ktx, "create",
        "--format", "R8_UNORM",
        "--depth", "64",
        "--assign-tf", "linear",
        "--assign-texcoord-origin", "top-left-front",
        *[str(p) for p in slices],
        str(out_path),
    ])


def main() -> None:
    VFX.mkdir(parents=True, exist_ok=True)
    ktx = ktx_tool()

    for kind, seed in [("simplex", 1207), ("perlin", 3407)]:
        r = make_channel(seed, kind)
        g = make_channel(seed + 4099, kind)
        make_raw_ktx(ktx, kind, interleave_rg(r, g), "R8G8_UNORM")
        make_bc(ktx, f"{kind}_bc4", r, "R8_UNORM", "bc4")

    restore_stbn(ktx)


if __name__ == "__main__":
    main()
