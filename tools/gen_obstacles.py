#!/usr/bin/env python3
"""Generate poop (3 damage states) + spikes obstacle sprites (16x16),
matching the game's chunky pixel-art style (like env_rock.png).
Output: gfx_clean/resized/env_poop_1..3.png, env_spikes.png
"""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "gfx_clean", "resized")

BROWN_D = (86, 55, 25, 255)    # dark outline
BROWN_M = (125, 84, 40, 255)   # mid tone
BROWN_L = (158, 111, 58, 255)  # highlight
SHINE   = (196, 148, 88, 255)


def poop(stage):
    """stage 1 = intact, 2 = damaged, 3 = nearly destroyed"""
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if stage == 1:
        # classic three-coil swirl
        d.ellipse((2, 9, 13, 15), fill=BROWN_M, outline=BROWN_D)
        d.ellipse((3, 5, 12, 11), fill=BROWN_M, outline=BROWN_D)
        d.ellipse((5, 2, 10, 7), fill=BROWN_L, outline=BROWN_D)
        d.point((6, 3), fill=SHINE)
        d.point((7, 3), fill=SHINE)
        d.point((5, 7), fill=BROWN_L)
        d.point((10, 11), fill=BROWN_L)
    elif stage == 2:
        # top coil smashed off, chips missing
        d.ellipse((2, 9, 13, 15), fill=BROWN_M, outline=BROWN_D)
        d.ellipse((3, 6, 12, 12), fill=BROWN_M, outline=BROWN_D)
        d.point((7, 6), fill=(0, 0, 0, 0))
        d.point((5, 7), fill=(0, 0, 0, 0))
        d.point((4, 10), fill=BROWN_L)
        # crumbs
        d.point((1, 14), fill=BROWN_D)
        d.point((14, 13), fill=BROWN_D)
    else:
        # flattened smear + crumbs
        d.ellipse((2, 11, 13, 15), fill=BROWN_M, outline=BROWN_D)
        d.point((4, 10), fill=BROWN_D)
        d.point((10, 10), fill=BROWN_D)
        d.point((1, 13), fill=BROWN_D)
        d.point((14, 14), fill=BROWN_D)
        d.point((7, 9), fill=BROWN_D)
    return im


def spikes():
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    GREY_D = (60, 60, 68, 255)
    GREY_M = (120, 120, 132, 255)
    GREY_L = (190, 190, 200, 255)
    # base plate
    d.rectangle((0, 13, 15, 15), fill=GREY_D)
    # four spikes
    for sx in (1, 5, 9, 13):
        d.polygon([(sx, 13), (sx + 3, 13), (sx + 1, 3)], fill=GREY_M, outline=GREY_D)
        d.line([(sx + 1, 12), (sx + 1, 5)], fill=GREY_L)
    return im


if __name__ == "__main__":
    for s in (1, 2, 3):
        p = os.path.join(OUT, f"env_poop_{s}.png")
        poop(s).save(p)
        print("wrote", p)
    sp = os.path.join(OUT, "env_spikes.png")
    spikes().save(sp)
    print("wrote", sp)
