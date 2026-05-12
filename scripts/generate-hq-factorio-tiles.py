#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path
from typing import Dict, List, Tuple

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover
    raise SystemExit(
        "Pillow is required. Install with: pip install pillow\n"
        f"Import error: {exc}"
    )


KINDS = ["dirt", "grass", "sand", "rocky", "iron", "copper", "coal"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate seamless HQ top-down tile textures (32/64) for factory automation games."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/generated/hq-tiles"),
        help="Output directory for generated textures.",
    )
    parser.add_argument(
        "--sizes",
        type=str,
        default="32,64",
        help="Comma-separated texture sizes (e.g. 32,64).",
    )
    parser.add_argument(
        "--variants",
        type=int,
        default=8,
        help="Variant count per kind and size.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=20260508,
        help="Deterministic base seed.",
    )
    return parser.parse_args()


def hash_u32(v: int) -> int:
    v &= 0xFFFFFFFF
    v ^= (v >> 16)
    v = (v * 0x7FEB352D) & 0xFFFFFFFF
    v ^= (v >> 15)
    v = (v * 0x846CA68B) & 0xFFFFFFFF
    v ^= (v >> 16)
    return v & 0xFFFFFFFF


def hash01(x: int, y: int, seed: int, period: int) -> float:
    xx = x % period
    yy = y % period
    h = hash_u32(seed ^ (xx * 0x9E3779B1) ^ (yy * 0x85EBCA77))
    return float(h & 0x00FFFFFF) / 16777216.0


def smooth(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def periodic_value_noise(x: float, y: float, period: int, seed: int) -> float:
    x0 = math.floor(x)
    y0 = math.floor(y)
    x1 = x0 + 1
    y1 = y0 + 1
    tx = smooth(x - x0)
    ty = smooth(y - y0)
    a = hash01(int(x0), int(y0), seed, period)
    b = hash01(int(x1), int(y0), seed, period)
    c = hash01(int(x0), int(y1), seed, period)
    d = hash01(int(x1), int(y1), seed, period)
    ab = a + (b - a) * tx
    cd = c + (d - c) * tx
    return ab + (cd - ab) * ty


def periodic_fbm(x: float, y: float, period: int, seed: int, octaves: int, lac: float = 2.0, gain: float = 0.5) -> float:
    total = 0.0
    amp = 0.5
    freq = 1.0
    norm = 0.0
    for i in range(octaves):
        per = max(1, int(round(period * freq)))
        total += periodic_value_noise(x * freq, y * freq, per, seed ^ (i * 0x9E3779B9)) * amp
        norm += amp
        amp *= gain
        freq *= lac
    return total / norm if norm > 0.0 else 0.0


def clamp8(v: float) -> int:
    return max(0, min(255, int(round(v))))


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def lerp3(c0: Tuple[float, float, float], c1: Tuple[float, float, float], t: float) -> Tuple[float, float, float]:
    return (
        lerp(c0[0], c1[0], t),
        lerp(c0[1], c1[1], t),
        lerp(c0[2], c1[2], t),
    )


def add3(c: Tuple[float, float, float], d: Tuple[float, float, float]) -> Tuple[float, float, float]:
    return c[0] + d[0], c[1] + d[1], c[2] + d[2]


def mul3(c: Tuple[float, float, float], m: float) -> Tuple[float, float, float]:
    return c[0] * m, c[1] * m, c[2] * m


def to_rgb(c: Tuple[float, float, float]) -> Tuple[int, int, int]:
    return clamp8(c[0]), clamp8(c[1]), clamp8(c[2])


def smoothstep(a: float, b: float, x: float) -> float:
    if a == b:
        return 1.0 if x >= b else 0.0
    t = (x - a) / (b - a)
    return smooth(t)


def terrain_color(kind: str, x: int, y: int, size: int, seed: int) -> Tuple[int, int, int]:
    macro = periodic_fbm(x * 0.11, y * 0.11, size, seed ^ 0xA11, 4)
    micro = periodic_fbm(x * 0.49, y * 0.49, size, seed ^ 0xA22, 3)
    grain = periodic_fbm(x * 0.95, y * 0.95, size, seed ^ 0xA33, 2)
    spots = hash01(x, y, seed ^ 0xA44, size)

    if kind == "dirt":
        base = lerp3((92, 64, 42), (142, 98, 62), macro)
        layers = periodic_fbm(x * 0.14, y * 0.26, size, seed ^ 0xA55, 3)
        c = add3(base, ((layers - 0.5) * 18.0, (layers - 0.5) * 10.0, (layers - 0.5) * 6.0))
        c = add3(c, ((grain - 0.5) * 12.0, (micro - 0.5) * 10.0, (micro - 0.5) * 9.0))
        if spots > 0.965:
            c = add3(c, (18, 14, 10))  # micro stones
        return to_rgb(c)

    if kind == "grass":
        base = lerp3((66, 110, 62), (102, 142, 86), macro)
        tuft = periodic_fbm(x * 0.30, y * 0.30, size, seed ^ 0xA66, 4)
        c = add3(base, ((tuft - 0.5) * 8.0, (tuft - 0.5) * 16.0, (tuft - 0.5) * 7.0))
        c = add3(c, ((micro - 0.5) * 5.0, (grain - 0.5) * 8.0, (micro - 0.5) * 4.0))
        if spots > 0.972:
            c = add3(c, (14, 18, 10))  # tiny blades highlight
        return to_rgb(c)

    if kind == "sand":
        base = lerp3((198, 176, 126), (228, 208, 152), macro)
        c = add3(base, ((grain - 0.5) * 10.0, (micro - 0.5) * 10.0, (grain - 0.5) * 8.0))
        if spots > 0.968:
            c = add3(c, (-18, -16, -12))  # pebble
        return to_rgb(c)

    # rocky
    base = lerp3((92, 86, 80), (134, 124, 112), macro)
    crack = periodic_fbm(x * 0.36, y * 0.36, size, seed ^ 0xA77, 4)
    veins = periodic_fbm(x * 0.20 + 11.0, y * 0.20 - 7.0, size, seed ^ 0xA88, 4)
    c = add3(base, ((crack - 0.5) * 18.0, (crack - 0.5) * 16.0, (crack - 0.5) * 14.0))
    if veins > 0.73:
        c = add3(c, (12, 10, 8))
    if spots > 0.972:
        c = add3(c, (-16, -14, -12))
    return to_rgb(c)


def ore_color(kind: str, x: int, y: int, size: int, seed: int) -> Tuple[int, int, int]:
    field = periodic_fbm(x * 0.15, y * 0.15, size, seed ^ 0xB11, 4)
    lumps = periodic_fbm(x * 0.42, y * 0.42, size, seed ^ 0xB22, 4)
    grit = periodic_fbm(x * 0.95, y * 0.95, size, seed ^ 0xB33, 2)
    spark = hash01(x, y, seed ^ 0xB44, size)
    cluster = smoothstep(0.50, 0.84, field * 0.72 + lumps * 0.58)

    if kind == "iron":
        base = lerp3((82, 96, 110), (118, 134, 148), field)
        ore = lerp3((120, 138, 156), (170, 188, 202), lumps)
        c = lerp3(base, ore, cluster)
        c = add3(c, ((grit - 0.5) * 10.0, (grit - 0.5) * 11.0, (grit - 0.5) * 13.0))
        if spark > 0.985:
            c = add3(c, (28, 32, 36))
        return to_rgb(c)

    if kind == "copper":
        base = lerp3((106, 64, 40), (144, 86, 54), field)
        ore = lerp3((162, 98, 60), (206, 132, 84), lumps)
        c = lerp3(base, ore, cluster)
        c = add3(c, ((grit - 0.5) * 9.0, (grit - 0.5) * 7.0, (grit - 0.5) * 5.0))
        if spark > 0.986:
            c = add3(c, (24, 15, 8))
        return to_rgb(c)

    # coal
    base = lerp3((24, 26, 30), (42, 46, 52), field)
    ore = lerp3((38, 40, 46), (64, 68, 74), lumps)
    c = lerp3(base, ore, cluster)
    c = add3(c, ((grit - 0.5) * 7.0, (grit - 0.5) * 7.0, (grit - 0.5) * 7.0))
    if spark > 0.989:
        c = add3(c, (16, 16, 16))
    return to_rgb(c)


def generate_tile(kind: str, size: int, seed: int) -> Image.Image:
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            if kind in {"dirt", "grass", "sand", "rocky"}:
                px[x, y] = terrain_color(kind, x, y, size, seed)
            else:
                px[x, y] = ore_color(kind, x, y, size, seed)
    return img


def write_preview_sheet(size_dir: Path, size: int, variants: int) -> None:
    cols = variants
    rows = len(KINDS)
    sheet = Image.new("RGB", (cols * size, rows * size))
    for ry, kind in enumerate(KINDS):
        for cx in range(cols):
            tile_path = size_dir / kind / f"{kind}_v{cx + 1:02d}_{size}.png"
            if not tile_path.exists():
                continue
            tile = Image.open(tile_path).convert("RGB")
            sheet.paste(tile, (cx * size, ry * size))
    sheet.save(size_dir / f"preview_sheet_{size}.png")


def main() -> None:
    args = parse_args()
    sizes: List[int] = []
    for s in args.sizes.split(","):
        s = s.strip()
        if not s:
            continue
        sizes.append(max(8, int(s)))
    if not sizes:
        raise SystemExit("No sizes specified.")

    args.output.mkdir(parents=True, exist_ok=True)
    metadata: Dict[str, Dict[str, List[str]]] = {}

    for size in sizes:
        size_dir = args.output / str(size)
        size_dir.mkdir(parents=True, exist_ok=True)
        metadata[str(size)] = {}
        for kind_idx, kind in enumerate(KINDS):
            kind_dir = size_dir / kind
            kind_dir.mkdir(parents=True, exist_ok=True)
            names: List[str] = []
            for v in range(args.variants):
                seed = args.seed ^ (size * 1009) ^ (kind_idx * 131) ^ (v * 8191)
                img = generate_tile(kind, size, seed)
                name = f"{kind}_v{v + 1:02d}_{size}.png"
                img.save(kind_dir / name)
                names.append(str((Path(str(size)) / kind / name).as_posix()))
            metadata[str(size)][kind] = names
            print(f"[{size}px] {kind}: {len(names)} variants")

        write_preview_sheet(size_dir, size, args.variants)
        print(f"[{size}px] preview sheet: {size_dir / f'preview_sheet_{size}.png'}")

    meta_path = args.output / "manifest.json"
    meta_path.write_text(
        json.dumps(
            {
                "generator": "generate-hq-factorio-tiles.py",
                "styles": "top-down orthographic, no directional lighting, seamless tile textures",
                "sizes": sizes,
                "variants": args.variants,
                "kinds": KINDS,
                "files": metadata,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    print(f"Manifest written: {meta_path}")


if __name__ == "__main__":
    main()

