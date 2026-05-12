#!/usr/bin/env python3
import argparse
import json
import random
import struct
from pathlib import Path
from typing import Dict, List, Tuple

try:
    from PIL import Image, ImageEnhance
except Exception as exc:  # pragma: no cover
    raise SystemExit(
        "Pillow is required. Install with: pip install pillow\n"
        f"Import error: {exc}"
    )

KIND_ORDER = [
    "lowland",
    "midland",
    "highland",
    "water",
    "mountain",
    "iron",
    "copper",
    "coal",
    "player",
]

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build external runtime texture atlas from local style references."
    )
    parser.add_argument(
        "--dataset-root",
        type=Path,
        default=Path("assets/style-dataset"),
        help="Root directory with per-kind image folders.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("assets/style-dataset/manifest.json"),
        help="Optional manifest mapping kinds to image paths.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/generated/runtime_texture_atlas.bin"),
        help="Output atlas binary for runtime.",
    )
    parser.add_argument("--texture-size", type=int, default=24, help="Texture size in pixels.")
    parser.add_argument("--variants", type=int, default=6, help="Variants per visual kind.")
    parser.add_argument("--seed", type=int, default=20260508, help="RNG seed for deterministic output.")
    return parser.parse_args()


def load_manifest(manifest_path: Path) -> Dict[str, List[Path]]:
    if not manifest_path.exists():
        return {}
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    class_map = data.get("classes", {})
    out: Dict[str, List[Path]] = {}
    for kind, items in class_map.items():
        out[kind] = [Path(p) for p in items]
    return out


def discover_refs(dataset_root: Path, manifest_map: Dict[str, List[Path]]) -> Dict[str, List[Path]]:
    refs: Dict[str, List[Path]] = {}
    for kind in KIND_ORDER:
        candidates: List[Path] = []
        if kind in manifest_map:
            for p in manifest_map[kind]:
                path = p if p.is_absolute() else Path(".") / p
                if path.exists() and path.suffix.lower() in IMAGE_EXTS:
                    candidates.append(path)
        kind_dir = dataset_root / kind
        if kind_dir.exists():
            for p in sorted(kind_dir.rglob("*")):
                if p.is_file() and p.suffix.lower() in IMAGE_EXTS:
                    candidates.append(p)
        refs[kind] = candidates
    return refs


def load_images(paths: List[Path]) -> List[Image.Image]:
    images: List[Image.Image] = []
    for p in paths:
        try:
            img = Image.open(p).convert("RGB")
            images.append(img)
        except Exception:
            continue
    return images


def quilt_texture(images: List[Image.Image], size: int, rng: random.Random) -> Image.Image:
    if not images:
        return procedural_fallback("lowland", size, rng)
    out = Image.new("RGB", (size, size))
    block = max(4, size // 4)
    for y in range(0, size, block):
        for x in range(0, size, block):
            src = rng.choice(images)
            sw, sh = src.size
            cw = min(block + 2, sw)
            ch = min(block + 2, sh)
            sx = rng.randint(0, max(0, sw - cw))
            sy = rng.randint(0, max(0, sh - ch))
            patch = src.crop((sx, sy, sx + cw, sy + ch)).resize((block, block), Image.Resampling.BILINEAR)
            out.paste(patch, (x, y))
    return out


def procedural_fallback(kind: str, size: int, rng: random.Random) -> Image.Image:
    base: Dict[str, Tuple[int, int, int]] = {
        "lowland": (88, 102, 78),
        "midland": (108, 112, 84),
        "highland": (128, 118, 96),
        "water": (36, 72, 124),
        "mountain": (112, 102, 90),
        "iron": (156, 164, 172),
        "copper": (176, 104, 62),
        "coal": (44, 44, 48),
        "player": (216, 206, 176),
    }
    r0, g0, b0 = base.get(kind, (100, 100, 100))
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            n = rng.randint(-14, 14)
            px[x, y] = (
                max(0, min(255, r0 + n)),
                max(0, min(255, g0 + n)),
                max(0, min(255, b0 + n)),
            )
    return img


def stylize(kind: str, img: Image.Image, rng: random.Random) -> Image.Image:
    sat = 0.9 if kind in {"lowland", "midland", "highland", "mountain"} else 1.1
    if kind == "water":
        sat = 1.2
    img = ImageEnhance.Color(img).enhance(sat)
    img = ImageEnhance.Contrast(img).enhance(1.08)
    if kind == "iron":
        img = tint(img, (172, 178, 186), 0.28)
    elif kind == "copper":
        img = tint(img, (194, 122, 74), 0.34)
    elif kind == "coal":
        img = tint(img, (52, 54, 58), 0.42)
    elif kind == "water":
        img = tint(img, (46, 92, 152), 0.30)
    elif kind == "player":
        img = tint(img, (214, 198, 150), 0.45)
    return img


def tint(img: Image.Image, color: Tuple[int, int, int], amount: float) -> Image.Image:
    layer = Image.new("RGB", img.size, color)
    return Image.blend(img, layer, amount)


def image_to_u32_pixels(img: Image.Image) -> List[int]:
    pixels = list(img.convert("RGB").getdata())
    return [r | (g << 8) | (b << 16) for (r, g, b) in pixels]


def write_atlas(
    out_path: Path,
    size: int,
    variants_by_kind: Dict[str, List[Image.Image]],
) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(b"FPTA")
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<I", size))
        f.write(struct.pack("<I", len(KIND_ORDER)))
        for idx, kind in enumerate(KIND_ORDER):
            variants = variants_by_kind[kind]
            f.write(struct.pack("<I", idx))
            f.write(struct.pack("<I", len(variants)))
            for tex in variants:
                u32_pixels = image_to_u32_pixels(tex)
                f.write(struct.pack(f"<{len(u32_pixels)}I", *u32_pixels))


def main() -> None:
    args = parse_args()
    rng = random.Random(args.seed)
    manifest_map = load_manifest(args.manifest)
    refs = discover_refs(args.dataset_root, manifest_map)

    variants_by_kind: Dict[str, List[Image.Image]] = {}
    for kind in KIND_ORDER:
        images = load_images(refs[kind])
        variants: List[Image.Image] = []
        for _ in range(args.variants):
            if images:
                tex = quilt_texture(images, args.texture_size, rng)
            else:
                tex = procedural_fallback(kind, args.texture_size, rng)
            tex = stylize(kind, tex, rng)
            variants.append(tex)
        variants_by_kind[kind] = variants
        print(f"[{kind}] refs={len(images)} variants={len(variants)}")

    write_atlas(args.output, args.texture_size, variants_by_kind)
    print(f"Atlas written: {args.output}")


if __name__ == "__main__":
    main()
