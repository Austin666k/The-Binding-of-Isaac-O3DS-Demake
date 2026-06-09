#!/usr/bin/env python3
"""Create 12 new enemy sprites for Binding of Isaac 3DS. 24x24 RGBA pixel art."""
from PIL import Image, ImageDraw
import os

OUT = "/home/ubuntu/binding_of_isaac_3ds/gfx_clean/resized"

def ns():
    return Image.new("RGBA", (24, 24), (0, 0, 0, 0))

# 1. Attack Fly - red aggressive fly with angry eyes
def create_attack_fly():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (dark red)
    d.ellipse([7, 8, 17, 20], fill=(180, 50, 50, 255))
    # Wings (translucent)
    d.ellipse([2, 3, 10, 11], fill=(200, 200, 200, 140))
    d.ellipse([14, 3, 22, 11], fill=(200, 200, 200, 140))
    # Angry eyes (yellow)
    d.ellipse([8, 10, 11, 14], fill=(255, 255, 50, 255))
    d.ellipse([13, 10, 16, 14], fill=(255, 255, 50, 255))
    # Pupils
    d.point((10, 12), fill=(20, 20, 20, 255))
    d.point((14, 12), fill=(20, 20, 20, 255))
    # Angry eyebrow lines
    d.line([(8, 10), (11, 11)], fill=(100, 20, 20, 255))
    d.line([(16, 11), (13, 10)], fill=(100, 20, 20, 255))
    return img

# 2. Pooter - purple flying blob that shoots
def create_pooter():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (purple blob)
    d.ellipse([5, 6, 19, 20], fill=(140, 80, 160, 255))
    # Wings (small, bat-like)
    d.polygon([(5, 10), (1, 5), (3, 12)], fill=(120, 60, 140, 200))
    d.polygon([(19, 10), (23, 5), (21, 12)], fill=(120, 60, 140, 200))
    # Eyes (wide, worried)
    d.ellipse([7, 9, 11, 14], fill=(255, 255, 255, 255))
    d.ellipse([13, 9, 17, 14], fill=(255, 255, 255, 255))
    d.ellipse([8, 10, 10, 13], fill=(20, 20, 20, 255))
    d.ellipse([14, 10, 16, 13], fill=(20, 20, 20, 255))
    # Mouth
    d.arc([9, 14, 15, 18], 0, 180, fill=(80, 30, 80, 255))
    # Highlight
    d.point((8, 8), fill=(180, 120, 200, 255))
    return img

# 3. Globin - red fleshy blob that charges
def create_globin():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (red, blobby, larger)
    d.ellipse([3, 5, 21, 22], fill=(180, 50, 40, 255))
    d.ellipse([5, 3, 19, 12], fill=(200, 70, 60, 255))
    # Dark fleshy lines
    d.line([(8, 8), (8, 18)], fill=(140, 30, 25, 255))
    d.line([(16, 8), (16, 18)], fill=(140, 30, 25, 255))
    # Eyes (small, white)
    d.ellipse([8, 8, 11, 11], fill=(255, 255, 255, 255))
    d.ellipse([13, 8, 16, 11], fill=(255, 255, 255, 255))
    d.point((9, 9), fill=(20, 20, 20, 255))
    d.point((14, 9), fill=(20, 20, 20, 255))
    # Dripping blood
    d.ellipse([6, 19, 9, 23], fill=(160, 30, 20, 255))
    d.ellipse([15, 20, 18, 24], fill=(160, 30, 20, 255))
    return img

# 4. Boom Fly - orange/yellow fly that explodes
def create_boom_fly():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (orange, swollen)
    d.ellipse([6, 7, 18, 21], fill=(230, 140, 40, 255))
    # Wings
    d.ellipse([2, 2, 10, 10], fill=(200, 200, 200, 140))
    d.ellipse([14, 2, 22, 10], fill=(200, 200, 200, 140))
    # Glow/fuse effect
    d.ellipse([9, 9, 15, 15], fill=(255, 200, 50, 200))
    # Eyes (X-shaped, dangerous)
    d.line([(8, 10), (10, 12)], fill=(40, 20, 20, 255), width=1)
    d.line([(10, 10), (8, 12)], fill=(40, 20, 20, 255), width=1)
    d.line([(14, 10), (16, 12)], fill=(40, 20, 20, 255), width=1)
    d.line([(16, 10), (14, 12)], fill=(40, 20, 20, 255), width=1)
    # Spark on top
    d.point((12, 5), fill=(255, 255, 200, 255))
    d.point((11, 4), fill=(255, 255, 100, 200))
    d.point((13, 4), fill=(255, 255, 100, 200))
    return img

# 5. Host - skull-like creature in a shell
def create_host():
    img = ns(); d = ImageDraw.Draw(img)
    # Shell/base (gray dome)
    d.pieslice([3, 8, 21, 24], 180, 0, fill=(140, 140, 150, 255))
    d.rectangle([3, 16, 21, 22], fill=(130, 130, 140, 255))
    # Shell lines
    d.arc([3, 8, 21, 24], 180, 0, fill=(100, 100, 110, 255))
    d.line([(7, 12), (7, 20)], fill=(110, 110, 120, 255))
    d.line([(17, 12), (17, 20)], fill=(110, 110, 120, 255))
    # Skull peeking out
    d.ellipse([7, 4, 17, 14], fill=(230, 220, 200, 255))
    # Eye sockets (dark)
    d.ellipse([8, 6, 11, 10], fill=(30, 10, 10, 255))
    d.ellipse([13, 6, 16, 10], fill=(30, 10, 10, 255))
    # Red eyes
    d.point((9, 8), fill=(255, 50, 50, 255))
    d.point((14, 8), fill=(255, 50, 50, 255))
    # Nose hole
    d.point((12, 10), fill=(80, 60, 50, 255))
    # Teeth
    d.line([(9, 12), (15, 12)], fill=(200, 200, 190, 255))
    d.point((10, 13), fill=(200, 200, 190, 255))
    d.point((14, 13), fill=(200, 200, 190, 255))
    return img

# 6. Mulligan - flesh-colored humanoid with flies
def create_mulligan():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (pinkish flesh)
    d.ellipse([6, 6, 18, 22], fill=(200, 160, 140, 255))
    # Head bump
    d.ellipse([8, 2, 16, 12], fill=(210, 170, 150, 255))
    # Vacant eyes
    d.ellipse([9, 5, 12, 9], fill=(40, 40, 40, 255))
    d.ellipse([13, 5, 16, 9], fill=(40, 40, 40, 255))
    # Mouth (open, drooling)
    d.ellipse([10, 9, 14, 13], fill=(80, 30, 30, 255))
    # Small flies orbiting (2 dots)
    d.ellipse([2, 1, 5, 4], fill=(60, 60, 60, 255))
    d.ellipse([19, 2, 22, 5], fill=(60, 60, 60, 255))
    # Fly wings
    d.point((2, 1), fill=(180, 180, 180, 200))
    d.point((21, 2), fill=(180, 180, 180, 200))
    # Flesh drips
    d.point((8, 20), fill=(180, 130, 110, 255))
    d.point((16, 21), fill=(180, 130, 110, 255))
    return img

# 7. Red Maw - stationary red turret mouth
def create_red_maw():
    img = ns(); d = ImageDraw.Draw(img)
    # Base (dark red lump)
    d.ellipse([4, 8, 20, 22], fill=(160, 40, 40, 255))
    # Mouth (large, open)
    d.ellipse([6, 4, 18, 16], fill=(120, 20, 20, 255))
    # Inner mouth (dark)
    d.ellipse([8, 6, 16, 14], fill=(60, 10, 10, 255))
    # Teeth (top)
    d.polygon([(8, 6), (10, 8), (9, 6)], fill=(220, 200, 180, 255))
    d.polygon([(12, 5), (14, 8), (11, 8)], fill=(220, 200, 180, 255))
    d.polygon([(15, 6), (16, 8), (14, 6)], fill=(220, 200, 180, 255))
    # Teeth (bottom)
    d.polygon([(9, 14), (11, 12), (10, 14)], fill=(220, 200, 180, 255))
    d.polygon([(13, 14), (15, 12), (14, 14)], fill=(220, 200, 180, 255))
    # Highlight
    d.point((7, 10), fill=(200, 80, 70, 255))
    return img

# 8. Leaper - frog-like creature
def create_leaper():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (green-brown, frog-like)
    d.ellipse([4, 8, 20, 22], fill=(120, 140, 80, 255))
    # Head (slightly smaller)
    d.ellipse([6, 3, 18, 14], fill=(130, 150, 90, 255))
    # Big eyes (bulging, on top)
    d.ellipse([5, 1, 11, 7], fill=(200, 210, 160, 255))
    d.ellipse([13, 1, 19, 7], fill=(200, 210, 160, 255))
    # Pupils
    d.ellipse([7, 2, 10, 6], fill=(40, 40, 20, 255))
    d.ellipse([14, 2, 17, 6], fill=(40, 40, 20, 255))
    # Mouth line
    d.arc([8, 8, 16, 14], 0, 180, fill=(80, 100, 50, 255), width=1)
    # Legs (tucked for jumping)
    d.ellipse([2, 16, 7, 22], fill=(110, 130, 70, 255))
    d.ellipse([17, 16, 22, 22], fill=(110, 130, 70, 255))
    # Belly lighter
    d.ellipse([8, 14, 16, 20], fill=(160, 180, 120, 255))
    return img

# 9. Hopper - small bouncing creature
def create_hopper():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (round, pinkish)
    d.ellipse([5, 6, 19, 22], fill=(200, 140, 130, 255))
    # Legs (stubby, wide)
    d.ellipse([3, 17, 9, 23], fill=(180, 120, 110, 255))
    d.ellipse([15, 17, 21, 23], fill=(180, 120, 110, 255))
    # Face
    d.ellipse([7, 5, 17, 16], fill=(210, 150, 140, 255))
    # Big eyes
    d.ellipse([7, 6, 12, 12], fill=(255, 255, 255, 255))
    d.ellipse([12, 6, 17, 12], fill=(255, 255, 255, 255))
    d.ellipse([9, 8, 11, 11], fill=(20, 20, 20, 255))
    d.ellipse([13, 8, 15, 11], fill=(20, 20, 20, 255))
    # Open mouth (surprised)
    d.ellipse([10, 12, 14, 16], fill=(100, 40, 40, 255))
    return img

# 10. Maw - walking mouth creature
def create_maw():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (dark flesh)
    d.ellipse([4, 4, 20, 22], fill=(170, 100, 90, 255))
    # Huge mouth (dominates body)
    d.ellipse([5, 8, 19, 20], fill=(100, 30, 30, 255))
    d.ellipse([7, 10, 17, 18], fill=(50, 10, 10, 255))
    # Teeth top row
    for tx in range(7, 17, 3):
        d.polygon([(tx, 10), (tx+1, 12), (tx+2, 10)], fill=(220, 210, 190, 255))
    # Teeth bottom
    for tx in range(8, 16, 3):
        d.polygon([(tx, 18), (tx+1, 16), (tx+2, 18)], fill=(220, 210, 190, 255))
    # Small eyes
    d.ellipse([7, 4, 10, 8], fill=(255, 200, 50, 255))
    d.ellipse([14, 4, 17, 8], fill=(255, 200, 50, 255))
    d.point((8, 6), fill=(20, 20, 20, 255))
    d.point((15, 6), fill=(20, 20, 20, 255))
    return img

# 11. Vis - floating eyeball that shoots double
def create_vis():
    img = ns(); d = ImageDraw.Draw(img)
    # Body (gray, floating mass)
    d.ellipse([3, 5, 21, 22], fill=(130, 130, 150, 255))
    # Large central eye
    d.ellipse([6, 6, 18, 18], fill=(255, 255, 240, 255))
    d.ellipse([8, 8, 16, 16], fill=(180, 50, 50, 255))
    d.ellipse([10, 10, 14, 14], fill=(20, 20, 20, 255))
    # Eye highlight
    d.point((11, 10), fill=(255, 255, 255, 255))
    # Veiny lines around eye
    d.line([(6, 10), (3, 8)], fill=(180, 60, 60, 200))
    d.line([(18, 10), (21, 8)], fill=(180, 60, 60, 200))
    d.line([(12, 6), (12, 3)], fill=(180, 60, 60, 200))
    # Dripping flesh underneath
    d.ellipse([7, 18, 10, 23], fill=(120, 120, 140, 255))
    d.ellipse([14, 19, 17, 24], fill=(120, 120, 140, 255))
    return img

# 12. Baby - small crawling creature
def create_baby():
    img = ns(); d = ImageDraw.Draw(img)
    # Tiny body (pinkish, round)
    d.ellipse([7, 8, 17, 20], fill=(230, 190, 180, 255))
    # Head (large relative to body, baby-like)
    d.ellipse([6, 2, 18, 14], fill=(240, 200, 190, 255))
    # Big innocent eyes
    d.ellipse([7, 4, 12, 10], fill=(255, 255, 255, 255))
    d.ellipse([12, 4, 17, 10], fill=(255, 255, 255, 255))
    d.ellipse([9, 5, 11, 9], fill=(60, 60, 120, 255))
    d.ellipse([13, 5, 15, 9], fill=(60, 60, 120, 255))
    # Tear drop (like Isaac)
    d.ellipse([8, 10, 10, 12], fill=(100, 150, 220, 255))
    # Small mouth
    d.arc([10, 10, 14, 13], 0, 180, fill=(160, 100, 90, 255))
    # Stubby arms
    d.ellipse([4, 12, 8, 16], fill=(220, 180, 170, 255))
    d.ellipse([16, 12, 20, 16], fill=(220, 180, 170, 255))
    return img

enemies = {
    "enemy_attack_fly": create_attack_fly,
    "enemy_pooter": create_pooter,
    "enemy_globin": create_globin,
    "enemy_boom_fly": create_boom_fly,
    "enemy_host": create_host,
    "enemy_mulligan": create_mulligan,
    "enemy_red_maw": create_red_maw,
    "enemy_leaper": create_leaper,
    "enemy_hopper": create_hopper,
    "enemy_maw": create_maw,
    "enemy_vis": create_vis,
    "enemy_baby": create_baby,
}

for name, creator in enemies.items():
    img = creator()
    path = os.path.join(OUT, f"{name}.png")
    img.save(path, "PNG")
    print(f"✅ {path}")

print(f"\nCreated {len(enemies)} enemy sprites.")
