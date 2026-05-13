from pathlib import Path
from PIL import Image, ImageDraw


OUT_DIR = Path(r"E:\Factorio-pt'\assets\generated\player")
OUT_DIR.mkdir(parents=True, exist_ok=True)

W, H = 96, 96
FRAMES = 8


def draw_frame(i: int) -> Image.Image:
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = W // 2, H // 2 + 10

    step = (i / FRAMES) * 6.283185307179586
    leg = int(round((__import__("math").sin(step)) * 4))
    arm = int(round((__import__("math").sin(step + 3.14159)) * 3))

    # shadow
    d.ellipse((cx - 14, cy + 18, cx + 14, cy + 26), fill=(0, 0, 0, 80))

    # body
    d.rounded_rectangle((cx - 13, cy - 18, cx + 13, cy + 14), radius=7, fill=(215, 182, 110, 255))
    d.rounded_rectangle((cx - 11, cy - 15, cx + 11, cy + 6), radius=6, fill=(238, 206, 135, 255))

    # helmet
    d.ellipse((cx - 12, cy - 30, cx + 12, cy - 6), fill=(168, 192, 214, 255))
    d.ellipse((cx - 10, cy - 27, cx + 10, cy - 10), fill=(128, 154, 180, 255))
    d.rectangle((cx - 7, cy - 20, cx + 7, cy - 14), fill=(31, 42, 55, 255))

    # arms
    d.rounded_rectangle((cx - 20, cy - 14 + arm, cx - 12, cy + 8 + arm), radius=3, fill=(190, 160, 102, 255))
    d.rounded_rectangle((cx + 12, cy - 14 - arm, cx + 20, cy + 8 - arm), radius=3, fill=(190, 160, 102, 255))

    # legs
    d.rounded_rectangle((cx - 10, cy + 14 + leg, cx - 2, cy + 31 + leg), radius=3, fill=(62, 72, 82, 255))
    d.rounded_rectangle((cx + 2, cy + 14 - leg, cx + 10, cy + 31 - leg), radius=3, fill=(62, 72, 82, 255))
    d.rectangle((cx - 10, cy + 28 + leg, cx - 2, cy + 33 + leg), fill=(35, 35, 40, 255))
    d.rectangle((cx + 2, cy + 28 - leg, cx + 10, cy + 33 - leg), fill=(35, 35, 40, 255))

    # highlight
    d.ellipse((cx - 3, cy - 26, cx + 1, cy - 22), fill=(210, 230, 255, 180))
    return img


def main() -> None:
    for i in range(FRAMES):
        frame = draw_frame(i)
        frame.save(OUT_DIR / f"player_walk_{i:02}.png")
    print(f"Generated {FRAMES} frames at {OUT_DIR}")


if __name__ == "__main__":
    main()
