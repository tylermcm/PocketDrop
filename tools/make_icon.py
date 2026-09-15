"""Renders src/app.ico (gradient tile with a drop arrow) using Pillow."""
import os
from PIL import Image, ImageDraw

S = 1024  # supersampled canvas


def render():
    grad = Image.new("RGB", (S, S))
    px = grad.load()
    a, b = (139, 124, 255), (79, 195, 247)
    for y in range(S):
        for x in range(S):
            t = (x + y) / (2 * S - 2)
            px[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, S - 40, S - 40), radius=230, fill=255)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    w = 92
    white = (255, 255, 255, 255)

    def line(p, q):
        d.line([p, q], fill=white, width=w)
        for c in (p, q):
            d.ellipse((c[0] - w / 2, c[1] - w / 2, c[0] + w / 2, c[1] + w / 2), fill=white)

    cx = S / 2
    line((cx, 250), (cx, 610))
    line((cx - 190, 430), (cx, 620))
    line((cx + 190, 430), (cx, 620))
    line((260, 790), (S - 260, 790))
    return img


if __name__ == "__main__":
    big = render()
    src = os.path.join(os.path.dirname(__file__), "..", "src")
    ico = os.path.join(src, "win", "app.ico")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(ico, sizes=[(s, s) for s in sizes])
    print("wrote", os.path.abspath(ico))
    # macOS icons sit on a transparent canvas with ~10% margin, per Apple's grid.
    mac = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mac.paste(big.resize((824, 824), Image.LANCZOS), (100, 100))
    icns = os.path.join(src, "mac", "app.icns")
    os.makedirs(os.path.dirname(icns), exist_ok=True)
    mac.save(icns)
    print("wrote", os.path.abspath(icns))
