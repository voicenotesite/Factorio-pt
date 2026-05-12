"""
Orbitum — Stable Diffusion Texture Generator (Clean Factorio Style)
by Sh1t3ad Dev
"""

import argparse
import sys
from pathlib import Path

# ── Clean Factorio Style Prompts ─────────────────────────────────────────────
TERRAIN_PROMPTS = {
    "lowland": (
        "seamless tileable top-down orthographic game texture, clean Factorio style, bright olive green grass with patches of light brown dirt, "
        "small scattered stones, subtle grass variation, clean stylized look, high visibility, sharp but not noisy details, "
        "professional game asset, bright natural lighting, perfectly tiling, 4k texture quality"
    ),
    "midland": (
        "seamless tileable top-down orthographic game texture, clean Factorio style, light brown dirt ground with compacted areas, "
        "small rocks and pebbles, subtle tire tracks, bright and clean, high visibility, stylized game art, "
        "professional Factorio-like texture, perfectly tiling"
    ),
    "highland": (
        "seamless tileable top-down orthographic game texture, clean Factorio style, grey-brown rocky ground with small stones and cracks, "
        "bright lighting, high visibility, clean stylized details, professional game asset, perfectly tiling"
    ),
    "water": (
        "seamless tileable top-down orthographic water texture, clean Factorio style, teal blue water with subtle gentle ripples, "
        "bright clean game look, high visibility, stylized, perfectly tiling"
    ),
    "mountain": (
        "seamless tileable top-down orthographic rock texture, clean Factorio style, dark grey rocky surface with cracks and layers, "
        "high visibility, clean stylized details, professional game asset, perfectly tiling"
    ),
    "iron": (
        "seamless tileable top-down orthographic iron ore patch, clean Factorio style, distinct blue-grey ore veins in stone, "
        "clear visible resource, high contrast, bright game lighting, perfectly tiling"
    ),
    "copper": (
        "seamless tileable top-down orthographic copper ore patch, clean Factorio style, bright orange copper deposits in rock, "
        "clear visible resource, high contrast, stylized game texture, perfectly tiling"
    ),
    "coal": (
        "seamless tileable top-down orthographic coal deposit, clean Factorio style, black coal chunks in grey rock, "
        "clear visible resource, high contrast, stylized, perfectly tiling"
    ),
    "stone": (
        "seamless tileable top-down orthographic stone deposit, clean Factorio style, grey rock texture, "
        "high visibility, clean details, perfectly tiling"
    ),
    "player": (
        "top-down orthographic small player character in engineering suit with yellow helmet, "
        "Factorio engineer style, centered, clean game sprite, white background, highly detailed but readable silhouette"
    ),
}

NEGATIVE = (
    "blurry, noisy, messy, dirty, artifacts, low quality, painterly, watercolor, oil paint, photorealistic, realistic photo, "
    "oversaturated, bloom, glow, shadows, dark, too dark, muted, desaturated, cartoon, pixelated, deformed, "
    "seams, bad tiling, text, watermark, low contrast, chaotic, stroke-like patterns, brush strokes"
)

# ── Models ───────────────────────────────────────────────────────────────────
MODELS = {
    "sdxl": {
        "repo": "stabilityai/stable-diffusion-xl-base-1.0",
        "pipe": "AutoPipelineForText2Image",
        "steps": 30,
        "guidance": 7.5,
        "note": "Najlepsza jakość, publiczny bez konta (zalecany)",
    },
    "sdxl-turbo": {
        "repo": "stabilityai/sdxl-turbo",
        "pipe": "AutoPipelineForText2Image",
        "steps": 4,
        "guidance": 0.0,
        "note": "Szybki (~8s/tile), cached",
    },
    "sd-2.1": {
        "repo": "stabilityai/stable-diffusion-2-1",
        "pipe": "StableDiffusionPipeline",
        "steps": 35,
        "guidance": 7.5,
        "note": "WYMAGA KONTA HuggingFace (401 bez logowania)",
    },
}

DEFAULT_MODEL = "sdxl"
DEFAULT_SIZE = 1024

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Orbitum SD Texture Generator — Clean Factorio Style")
    parser.add_argument("--output-dir", default="assets/generated/sd_tiles_factorio_clean",
                        help="Where to save generated PNGs")
    parser.add_argument("--model", default=DEFAULT_MODEL, choices=list(MODELS.keys()),
                        help="sdxl (domyślny, najlepsza jakość) | sdxl-turbo (szybki) | sd-2.1 (wymaga konta HF)")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, choices=[512, 768, 1024, 1536, 2048],
                        help="Texture resolution (default: 1024)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for reproducibility")
    parser.add_argument("--steps", type=int, default=None,
                        help="Override inference steps")
    parser.add_argument("--kinds", nargs="*", default=None,
                        help="Only generate these kinds (default: all)")
    args = parser.parse_args()

    # ── Check deps ────────────────────────────────────────────────────────────
    try:
        import torch
        from diffusers import (
            StableDiffusionPipeline,
            AutoPipelineForText2Image,
            DPMSolverMultistepScheduler,
        )
    except ImportError:
        print("\n[ERROR] Missing dependencies. Install with:")
        print("  pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128")
        print("  pip install diffusers transformers accelerate")
        sys.exit(1)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cpu":
        print("[WARNING] Running on CPU — very slow!")
    else:
        vram = torch.cuda.get_device_properties(0).total_memory / 1e9
        print(f"[SD] GPU: {torch.cuda.get_device_name(0)} | VRAM: {vram:.1f} GB")

    dtype = torch.float16 if device == "cuda" else torch.float32
    cfg = MODELS[args.model]
    steps = args.steps or cfg["steps"]

    print(f"[SD] Model: {args.model} — {cfg['note']}")
    print(f"[SD] Resolution: {args.size}x{args.size} (Clean Factorio Style)")
    print(f"[SD] Steps: {steps} | Seed: {args.seed}")

    # ── Load pipeline ─────────────────────────────────────────────────────────
    print("[SD] Loading pipeline...")
    if cfg["pipe"] == "AutoPipelineForText2Image":
        pipe = AutoPipelineForText2Image.from_pretrained(
            cfg["repo"], torch_dtype=dtype, variant="fp16" if device == "cuda" else None
        ).to(device)
    else:
        pipe = StableDiffusionPipeline.from_pretrained(
            cfg["repo"], torch_dtype=dtype, use_safetensors=True
        ).to(device)
        pipe.scheduler = DPMSolverMultistepScheduler.from_config(pipe.scheduler.config)

    pipe.set_progress_bar_config(disable=True)

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    kinds = args.kinds or list(TERRAIN_PROMPTS.keys())

    # ── Generate ──────────────────────────────────────────────────────────────
    import torch as _torch
    for i, kind in enumerate(kinds):
        if kind not in TERRAIN_PROMPTS:
            print(f"[WARN] Unknown kind '{kind}', skipping")
            continue

        prompt = TERRAIN_PROMPTS[kind]
        out_path = out_dir / f"{kind}_{args.size}.png"

        print(f"[SD] [{i+1}/{len(kinds)}] Generating {kind} @ {args.size}px...", end=" ", flush=True)

        gen = _torch.Generator(device=device).manual_seed(args.seed + i)

        result = pipe(
            prompt=prompt,
            negative_prompt=NEGATIVE,
            num_inference_steps=steps,
            guidance_scale=cfg["guidance"],
            height=args.size,
            width=args.size,
            generator=gen,
        )

        img = result.images[0]
        img.save(out_path)
        print(f"saved → {out_path}")

    print(f"\n[SD] Done! Clean Factorio-style textures saved to: {out_dir}")
    print("[SD] Pack atlas:")
    print("  dotnet run --project tools-csharp/ai-trainer -- --variants 16 --output assets/generated/runtime_texture_atlas.bin")
    print("  Copy-Item assets\\generated\\runtime_texture_atlas.bin build\\runtime\\ -Force")


if __name__ == "__main__":
    main()


import argparse
import sys
from pathlib import Path

# ── Ultra Realistic QHD Prompts ─────────────────────────────────────────────
TERRAIN_PROMPTS = {
    "lowland": (
        "seamless tileable top-down orthographic 4K texture, ultra realistic lush green grass with patches of brown earth, "
        "small stones, dry grass tufts, subtle terrain variation, photorealistic, Factorio style game asset, "
        "natural daylight, high visibility, highly detailed, realistic materials, sharp micro details, "
        "perfectly tiling, no seams, clean game texture"
    ),
    "midland": (
        "seamless tileable top-down orthographic 4K texture, ultra realistic dry brown dirt and compacted earth, "
        "small rocks, pebbles, tire tracks, photorealistic industrial ground, Factorio style, "
        "natural daylight, high visibility, highly detailed surface, perfectly tiling"
    ),
    "highland": (
        "seamless tileable top-down orthographic 4K texture, ultra realistic rocky terrain with grey-brown stones and gravel, "
        "cracked earth, small boulders, photorealistic, Factorio style, natural daylight, "
        "high visibility, sharp detailed rocks, perfectly tiling"
    ),
    "water": (
        "seamless tileable top-down orthographic 4K water texture, ultra realistic dark teal-blue water with subtle ripples and caustics, "
        "photorealistic, Factorio style, natural lighting, high detail, perfectly tiling"
    ),
    "mountain": (
        "seamless tileable top-down orthographic 4K texture, ultra realistic dark grey rocky mountain surface, "
        "sharp cracked stones, layered mineral strata, photorealistic rock detail, Factorio style, "
        "perfectly tiling, high visibility"
    ),
    "iron": (
        "seamless tileable top-down orthographic 4K iron ore deposit, ultra realistic blue-grey metallic iron ore veins "
        "in dark rock, shiny ore chunks, photorealistic mineral texture, Factorio style, high contrast, perfectly tiling"
    ),
    "copper": (
        "seamless tileable top-down orthographic 4K copper ore deposit, ultra realistic bright orange and turquoise copper ore "
        "in brown rock, malachite oxidation, photorealistic, Factorio style, high visibility, perfectly tiling"
    ),
    "coal": (
        "seamless tileable top-down orthographic 4K coal deposit, ultra realistic black shiny coal chunks "
        "embedded in dark grey rock, photorealistic, Factorio style, high contrast, perfectly tiling"
    ),
    "stone": (
        "seamless tileable top-down orthographic 4K stone deposit, ultra realistic grey rock with quartz and mineral flecks, "
        "photorealistic, Factorio style, high visibility, perfectly tiling"
    ),
    "sand": (
        "seamless tileable top-down orthographic 4K sand texture, ultra realistic light beige desert sand with small dunes and pebbles, "
        "photorealistic, Factorio style, perfectly tiling"
    ),
    "player": (
        "top-down orthographic small player character in engineering suit with yellow helmet, "
        "Factorio engineer style, centered, clean game sprite, white background, highly detailed but readable silhouette"
    ),
}

NEGATIVE = (
    "cartoon, stylized, pixel art, low detail, blurry, painting, illustration, oversaturated, neon, bloom, glow, "
    "artifacts, seams, low resolution, deformed, ugly, too dark, muted colors, desaturated, shadows, directional light, "
    "isometric, perspective, text, watermark, logo"
)

# ── Models ───────────────────────────────────────────────────────────────────
MODELS = {
    "sdxl-turbo": {
        "repo": "stabilityai/sdxl-turbo",
        "pipe": "AutoPipelineForText2Image",
        "steps": 4,        # 4 steps = lepsza jakość niż 1, nadal szybko (~8s/tile)
        "guidance": 0.0,
        "note": "~8-12s/tile @ 1024px, cached, bez konta",
    },
    "sdxl": {
        "repo": "stabilityai/stable-diffusion-xl-base-1.0",
        "pipe": "AutoPipelineForText2Image",
        "steps": 30,
        "guidance": 7.5,
        "note": "Pełny SDXL, najlepsza jakość, ~30s/tile, publiczny bez konta",
    },
    "sd-2.1": {
        "repo": "stabilityai/stable-diffusion-2-1",
        "pipe": "StableDiffusionPipeline",
        "steps": 30,
        "guidance": 7.5,
        "note": "WYMAGA KONTA HuggingFace (401 bez logowania)",
    },
}

DEFAULT_MODEL = "sdxl-turbo"
DEFAULT_SIZE = 1024

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Orbitum SD Texture Generator — QHD Ultra Realistic")
    parser.add_argument("--output-dir", default="assets/generated/sd_tiles_qhd_realistic",
                        help="Where to save generated PNGs")
    parser.add_argument("--model", default=DEFAULT_MODEL, choices=list(MODELS.keys()),
                        help="sdxl-turbo (szybki, cached) | sdxl (najlepsza jakość) | sd-2.1 (wymaga konta HF)")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, choices=[512, 768, 1024, 1536, 2048],
                        help="Texture resolution (default: 1024)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for reproducibility")
    parser.add_argument("--steps", type=int, default=None,
                        help="Override inference steps")
    parser.add_argument("--kinds", nargs="*", default=None,
                        help="Only generate these kinds (default: all)")
    args = parser.parse_args()

    # ── Check deps ────────────────────────────────────────────────────────────
    try:
        import torch
        from diffusers import (
            StableDiffusionPipeline,
            AutoPipelineForText2Image,
            DPMSolverMultistepScheduler,
        )
    except ImportError:
        print("\n[ERROR] Missing dependencies. Install with:")
        print("  pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128")
        print("  pip install diffusers transformers accelerate")
        sys.exit(1)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cpu":
        print("[WARNING] Running on CPU — very slow!")
    else:
        vram = torch.cuda.get_device_properties(0).total_memory / 1e9
        print(f"[SD] GPU: {torch.cuda.get_device_name(0)} | VRAM: {vram:.1f} GB")

    dtype = torch.float16 if device == "cuda" else torch.float32
    cfg = MODELS[args.model]
    steps = args.steps or cfg["steps"]

    print(f"[SD] Model: {args.model} — {cfg['note']}")
    print(f"[SD] Resolution: {args.size}x{args.size}")
    print(f"[SD] Steps: {steps} | Seed: {args.seed}")

    # ── Load pipeline ─────────────────────────────────────────────────────────
    print("[SD] Loading pipeline (first run will download model)...")
    if cfg["pipe"] == "AutoPipelineForText2Image":
        pipe = AutoPipelineForText2Image.from_pretrained(
            cfg["repo"], torch_dtype=dtype, variant="fp16" if device == "cuda" else None
        ).to(device)
    else:
        pipe = StableDiffusionPipeline.from_pretrained(
            cfg["repo"], torch_dtype=dtype, use_safetensors=True
        ).to(device)
        pipe.scheduler = DPMSolverMultistepScheduler.from_config(pipe.scheduler.config)

    pipe.set_progress_bar_config(disable=True)

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    kinds = args.kinds or list(TERRAIN_PROMPTS.keys())

    # ── Generate ──────────────────────────────────────────────────────────────
    import torch as _torch
    for i, kind in enumerate(kinds):
        if kind not in TERRAIN_PROMPTS:
            print(f"[WARN] Unknown kind '{kind}', skipping")
            continue

        prompt = TERRAIN_PROMPTS[kind]
        out_path = out_dir / f"{kind}_{args.size}.png"

        print(f"[SD] [{i+1}/{len(kinds)}] Generating {kind} @ {args.size}px...", end=" ", flush=True)

        gen = _torch.Generator(device=device).manual_seed(args.seed + i)

        result = pipe(
            prompt=prompt,
            negative_prompt=NEGATIVE,
            num_inference_steps=steps,
            guidance_scale=cfg["guidance"],
            height=args.size,
            width=args.size,
            generator=gen,
        )

        img = result.images[0]
        img.save(out_path)
        print(f"saved → {out_path}")

    print(f"\n[SD] Done! {len(kinds)} textures saved to: {out_dir}")
    print("[SD] Pack atlas:")
    print("  dotnet run --project tools-csharp/ai-trainer -- --variants 16 --output assets/generated/runtime_texture_atlas.bin")
    print("  Copy-Item assets\\generated\\runtime_texture_atlas.bin build\\runtime\\ -Force")


if __name__ == "__main__":
    main()
