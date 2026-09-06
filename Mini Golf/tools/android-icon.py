#!/usr/bin/env python3
"""Make the Android launcher icons from one piece of the game's own artwork.

    tools/android-icon.py <image>            # e.g. the 55x55 icon decoded from the game's files

Writes three files next to the manifest, which tools/android-build.sh packages if they are there:

    android/icon.png              the icon Android 7 and older use, on its own
    android/icon-foreground.png   the two layers of an adaptive icon, for Android 8 and newer
    android/icon-background.png

None of them is in the repository: they are the game's artwork, like the icon the Switch build
carries, and the person building supplies it (../.gitignore).

**Why two layers.** Since Android 8 a launcher cuts every icon to a shape of its own choosing —
a circle here, a squircle there — and an app that hands over a plain square is left as a square
among circles. An adaptive icon hands over a 108dp foreground and background instead and lets
the launcher cut what it likes. Only the middle 72dp is certain to survive.

**Why the artwork is drawn at exactly that 72dp.** A circle is at its widest across the middle,
which is where this artwork keeps its logo, so a picture drawn to the circle's full diameter
keeps every letter and loses only its four corners, which are scenery. Drawing it smaller — to
fit *inside* the circle — leaves a square floating in the middle with a visible edge.

The background is the same artwork blown up and blurred, so that whatever shape is cut, and
however far a launcher parallaxes the layers apart, colour reaches the edge instead of a hole.

Needs Pillow. This is not part of the build: run it once when you supply the artwork.
"""
import sys
from pathlib import Path

try:
    from PIL import Image, ImageFilter
except ImportError:
    sys.exit("android-icon.py: needs Pillow (pip install pillow)")

CANVAS = 432  # 108dp at xxxhdpi, the size an adaptive icon's layers are
SAFE = 288    # the 72dp of it a launcher's mask is guaranteed not to cut away
LEGACY = 192  # a plain square icon, for Android 7 and older

def sharpened(image, size):
    """Scale to `size`, putting back the edge the scaling took off.

    The artwork is small — the game's own icon is 55x55 — so this is mostly enlargement, and
    Lanczos alone leaves it looking soft on a screen with four times the density of an iPod's.
    """
    grown = image.resize((size, size), Image.LANCZOS)
    return grown.filter(ImageFilter.UnsharpMask(radius=2, percent=110, threshold=2))

def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    source = Image.open(argv[1]).convert("RGB")
    if source.width != source.height:
        # Pad rather than stretch, using the corner, which on this artwork is its background.
        side = max(source.size)
        square = Image.new("RGB", (side, side), source.getpixel((0, 0)))
        square.paste(source, ((side - source.width) // 2, (side - source.height) // 2))
        source = square

    out = Path(__file__).resolve().parent.parent / "android"
    out.mkdir(exist_ok=True)

    sharpened(source, LEGACY).save(out / "icon.png", optimize=True)
    source.resize((CANVAS, CANVAS), Image.LANCZOS).filter(
        ImageFilter.GaussianBlur(14)).save(out / "icon-background.png", optimize=True)
    foreground = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    foreground.paste(sharpened(source, SAFE), ((CANVAS - SAFE) // 2, (CANVAS - SAFE) // 2))
    foreground.save(out / "icon-foreground.png", optimize=True)

    print(f"{argv[1]} ({source.width}x{source.height}) ->")
    for name in ("icon.png", "icon-foreground.png", "icon-background.png"):
        print(f"    android/{name}")

if __name__ == "__main__":
    main(sys.argv)
