"""
Orbitum — Stable Diffusion Texture Generator
by Sh1t3ad Dev

Generates top-down game tile textures using a local Stable Diffusion model.
RTX 3060 12GB: runs SDXL-Turbo (1 step, ~3s/tile) or SD-2.1 (~20 steps, ~8s/tile).
No account or API key required — models download from public HuggingFace repos.

Usage:
    python sd_texture_gen.py --output-dir assets/generated/sd_tiles [--model sdxl-turbo|sd-2.1] [--seed 42]

Output:
    assets/generated/sd_tiles/{kind}.png  (512x512 each)
    ai-trainer picks these up automatically on next atlas generation.
"""

import argparse
import sys
from pathlib import Path

# ── Prompts ─────────────────────────────────────────────────────────────────

TERRAIN_PROMPTS = {
    "lowland": (
        "seamless top-down orthographic grass ground texture for a factory automation game, "
        "vivid green, micro blades, organic noise, soil patches visible, no shadows, perfectly tiling, "
        "game asset, 512px, Factorio style, ultra sharp pixel detail"
    ),
    "midland": (
        "seamless top-down orthographic warm brown dirt ground texture, factory automation game, "
        "earthy soil, natural grain, small stones, no shadows, perfectly tiling, game asset"
    ),
    "highland": (
        "seamless top-down orthographic dry sandy rocky ground texture, factory game, "
        "pale beige stone, fine gravel, cracked surface, no shadows, perfectly tiling, game asset"
    ),
    "water": (
        "seamless top-down orthographic deep blue water surface, factory game, "
        "calm ripples, dark deep patches, bright shallow edges, no shadows, perfectly tiling, game asset"
    ),
    "mountain": (
        "seamless top-down orthographic dark grey rock texture, factory game, "
        "cracks, mineral veins, rough stone surface, no shadows, perfectly tiling, game asset"
    ),
    "iron": (
        "seamless top-down blue-grey iron ore deposit ground texture, factory automation game, "
        "dense metallic mineral clusters, blue sheen, dark veins, bright sparkles, "
        "no shadows, perfectly tiling, game asset, Factorio ore style"
    ),
    "copper": (
        "seamless top-down vivid orange copper ore deposit ground texture, factory automation game, "
        "orange-rust mineral clusters, oxidation patches, bright highlights, "
        "no shadows, perfectly tiling, game asset, Factorio ore style"
    ),
    "coal": (
        "seamless top-down near-black coal deposit ground texture, factory automation game, "
        "chunky matte dark deposits, carbon seam lines, rare facet glints, "
        "no shadows, perfectly tiling, game asset, Factorio ore style"
    ),
    "player": (
        "top-down view small player character in yellow engineering suit and blue visor helmet, "
        "factory game sprite, centered, transparent-friendly white background, pixel art style"
    ),
}

NEGATIVE = (
    "blurry, out of focus, painterly, watercolor, oil paint, shadows cast sideways, "
    "directional light, 3D render, isometric, perspective, text, watermark, logo, "
    "low quality, jpeg artifacts, oversaturated, noisy"
)

# ── Models ───────────────────────────────────────────────────────────────────

MODELS = {
    "sdxl-turbo": {
        "repo":  "stabilityai/sdxl-turbo",
        "pipe":  "AutoPipelineForText2Image",
        "steps": 1,
        "guidance": 0.0,   # turbo ignores CFG
        "size":  512,
        "note":  "~3s/tile on RTX 3060, no account needed",
    },
    "sd-2.1": {
        "repo":  "stabilityai/stable-diffusion-2-1",
        "pipe":  "StableDiffusionPipeline",
        "steps": 20,
        "guidance": 7.5,
        "size":  768,
        "note":  "~8s/tile on RTX 3060, higher detail, no account needed",
    },
}

DEFAULT_MODEL = "sdxl-turbo"


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Orbitum SD Texture Generator")
    parser.add_argument("--output-dir", default="assets/generated/sd_tiles",
                        help="Where to save generated PNGs")
    parser.add_argument("--model", default=DEFAULT_MODEL, choices=list(MODELS.keys()),
                        help=f"SD model to use (default: {DEFAULT_MODEL})")
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
        print("  pip install -r tools-python/requirements.txt\n")
        sys.exit(1)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cpu":
        print("[WARNING] CUDA not found — running on CPU (very slow). "
              "Make sure NVIDIA drivers and CUDA toolkit are installed.")
    else:
        vram = torch.cuda.get_device_properties(0).total_memory / 1e9
        print(f"[SD] GPU: {torch.cuda.get_device_name(0)}  VRAM: {vram:.1f} GB")

    dtype = torch.float16 if device == "cuda" else torch.float32

    cfg = MODELS[args.model]
    steps = args.steps or cfg["steps"]

    print(f"[SD] Model: {args.model} — {cfg['note']}")
    print(f"[SD] Repo:  {cfg['repo']}")
    print(f"[SD] Steps: {steps}  |  Seed: {args.seed}")

    # ── Load pipeline ─────────────────────────────────────────────────────────
    print("[SD] Loading pipeline (first run downloads model ~2-7 GB)…")
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
    import torch as _torch  # noqa: F811
    for i, kind in enumerate(kinds):
        if kind not in TERRAIN_PROMPTS:
            print(f"[WARN] Unknown kind '{kind}', skipping")
            continue

        prompt = TERRAIN_PROMPTS[kind]
        out_path = out_dir / f"{kind}.png"
        print(f"[SD] [{i+1}/{len(kinds)}] Generating {kind}…", end=" ", flush=True)

        gen = _torch.Generator(device=device).manual_seed(args.seed + i)
        result = pipe(
            prompt=prompt,
            negative_prompt=NEGATIVE if cfg["guidance"] > 0 else None,
            num_inference_steps=steps,
            guidance_scale=cfg["guidance"],
            height=cfg["size"],
            width=cfg["size"],
            generator=gen,
        )
        img = result.images[0]
        img.save(out_path)
        print(f"saved → {out_path}")

    print(f"\n[SD] Done! {len(kinds)} textures saved to: {out_dir}")
    print("[SD] Run ai-trainer to repackage atlas:\n"
          "  dotnet run --project tools-csharp/ai-trainer -- "
          "--variants 16 --output assets/generated/runtime_texture_atlas.bin\n"
          "  Copy-Item assets\\generated\\runtime_texture_atlas.bin build\\runtime\\ -Force")


if __name__ == "__main__":
    main()
