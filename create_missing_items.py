#!/usr/bin/env python3
"""
Create missing item sprites for Binding of Isaac 3DS.
Each sprite is 24x24 RGBA pixel art with transparent background.
Style matches existing item sprites in the project.
"""
from PIL import Image, ImageDraw
import os

OUT_DIR = "/home/ubuntu/binding_of_isaac_3ds/gfx_clean/resized"

def new_sprite():
    """Create a blank 24x24 RGBA image."""
    return Image.new("RGBA", (24, 24), (0, 0, 0, 0))

def draw_outline(draw, points, color=(40, 30, 30, 255)):
    """Draw outline around a polygon."""
    for i in range(len(points)):
        draw.line([points[i], points[(i+1) % len(points)]], fill=color, width=1)

# ============================================================
# 1. Wire Coat Hanger - bent wire hanger shape, metallic gray
# ============================================================
def create_wire_coat_hanger():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Hook at top
    draw.arc([8, 2, 16, 10], 0, 180, fill=(180, 180, 190, 255), width=2)
    # Triangular hanger body
    draw.line([(8, 6), (3, 20)], fill=(160, 160, 170, 255), width=2)
    draw.line([(16, 6), (21, 20)], fill=(160, 160, 170, 255), width=2)
    draw.line([(3, 20), (21, 20)], fill=(160, 160, 170, 255), width=2)
    # Hook top
    draw.line([(12, 2), (12, 6)], fill=(180, 180, 190, 255), width=2)
    # Highlight
    draw.point((12, 3), fill=(220, 220, 230, 255))
    draw.point((10, 12), fill=(200, 200, 210, 255))
    return img

# ============================================================
# 2. Lunch - brown paper lunch bag
# ============================================================
def create_lunch():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Bag body (brown)
    draw.rectangle([6, 8, 18, 21], fill=(180, 140, 80, 255))
    # Bag top fold
    draw.rectangle([6, 5, 18, 9], fill=(190, 150, 90, 255))
    # Fold crease
    draw.line([(6, 9), (18, 9)], fill=(140, 100, 50, 255))
    # Bag crinkle lines
    draw.line([(9, 10), (9, 20)], fill=(160, 120, 60, 255))
    draw.line([(15, 10), (15, 20)], fill=(160, 120, 60, 255))
    # Dark outline
    draw.rectangle([6, 5, 18, 21], outline=(100, 70, 30, 255))
    # Highlight
    draw.line([(7, 6), (11, 6)], fill=(210, 180, 120, 255))
    return img

# ============================================================
# 3. Cupid's Arrow - arrow with heart-shaped tip, pink
# ============================================================
def create_cupids_arrow():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Arrow shaft (diagonal)
    draw.line([(4, 20), (20, 4)], fill=(200, 170, 120, 255), width=2)
    # Heart-shaped arrowhead (top right) in pink
    draw.ellipse([17, 2, 21, 6], fill=(220, 80, 100, 255))
    draw.ellipse([19, 2, 23, 6], fill=(220, 80, 100, 255))
    draw.polygon([(17, 5), (23, 5), (20, 9)], fill=(220, 80, 100, 255))
    # Fletching (bottom left) - feather lines
    draw.line([(3, 19), (1, 22)], fill=(200, 200, 220, 255), width=1)
    draw.line([(5, 21), (3, 23)], fill=(200, 200, 220, 255), width=1)
    # Highlight on heart
    draw.point((18, 3), fill=(255, 150, 170, 255))
    return img

# ============================================================
# 4. Spelunker Hat - yellow mining helmet with headlamp
# ============================================================
def create_spelunker_hat():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Hat dome (yellow)
    draw.pieslice([4, 6, 20, 22], 180, 0, fill=(230, 200, 50, 255))
    # Hat brim
    draw.rectangle([3, 14, 21, 17], fill=(210, 180, 40, 255))
    # Headlamp (center front)
    draw.ellipse([9, 10, 15, 16], fill=(240, 240, 200, 255))
    draw.ellipse([10, 11, 14, 15], fill=(255, 255, 220, 255))
    # Lamp glow
    draw.ellipse([11, 12, 13, 14], fill=(255, 255, 255, 255))
    # Light beam suggestion
    draw.line([(12, 10), (12, 6)], fill=(255, 255, 200, 100))
    draw.line([(10, 10), (8, 6)], fill=(255, 255, 200, 60))
    draw.line([(14, 10), (16, 6)], fill=(255, 255, 200, 60))
    # Hat outline
    draw.arc([4, 6, 20, 22], 180, 0, fill=(180, 150, 30, 255), width=1)
    # Hat stripe
    draw.line([(5, 12), (19, 12)], fill=(200, 170, 30, 255))
    return img

# ============================================================
# 5. Speed Ball - syringe with white liquid
# ============================================================
def create_speed_ball():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Syringe barrel (angled)
    draw.rectangle([5, 8, 19, 14], fill=(200, 200, 210, 255))
    # Liquid inside (white/clear)
    draw.rectangle([7, 9, 15, 13], fill=(230, 230, 240, 255))
    # Plunger handle
    draw.rectangle([18, 9, 22, 13], fill=(180, 180, 190, 255))
    draw.line([(18, 9), (18, 13)], fill=(150, 150, 160, 255))
    # Needle
    draw.line([(5, 11), (1, 11)], fill=(180, 180, 190, 255), width=1)
    # Barrel lines
    draw.line([(8, 8), (8, 14)], fill=(170, 170, 180, 255))
    draw.line([(11, 8), (11, 14)], fill=(170, 170, 180, 255))
    draw.line([(14, 8), (14, 14)], fill=(170, 170, 180, 255))
    # Outline
    draw.rectangle([5, 8, 19, 14], outline=(130, 130, 140, 255))
    # Highlight
    draw.line([(6, 9), (16, 9)], fill=(240, 240, 250, 255))
    return img

# ============================================================
# 6. Spirit Sword - glowing blue/white sword
# ============================================================
def create_spirit_sword():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Blade glow (outer)
    draw.polygon([(11, 1), (13, 1), (14, 14), (10, 14)], fill=(150, 180, 255, 100))
    # Blade (silver/blue)
    draw.polygon([(11, 2), (13, 2), (13, 14), (11, 14)], fill=(200, 210, 240, 255))
    # Blade tip
    draw.polygon([(11, 2), (13, 2), (12, 0)], fill=(220, 230, 255, 255))
    # Blade center line
    draw.line([(12, 2), (12, 14)], fill=(230, 240, 255, 255))
    # Guard (golden)
    draw.rectangle([7, 14, 17, 16], fill=(210, 180, 60, 255))
    draw.rectangle([7, 14, 17, 16], outline=(170, 140, 40, 255))
    # Handle (brown)
    draw.rectangle([10, 16, 14, 21], fill=(140, 100, 60, 255))
    draw.line([(12, 17), (12, 20)], fill=(160, 120, 70, 255))
    # Pommel (golden)
    draw.ellipse([10, 20, 14, 23], fill=(210, 180, 60, 255))
    # Sparkle on blade
    draw.point((11, 5), fill=(255, 255, 255, 255))
    draw.point((13, 8), fill=(255, 255, 255, 255))
    return img

# ============================================================
# 7. Blood of the Martyr - crown of thorns (dark brown/red)
# ============================================================
def create_blood_of_martyr():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Crown ring (dark thorny brown)
    draw.ellipse([3, 8, 21, 20], fill=(120, 70, 40, 255))
    draw.ellipse([5, 10, 19, 18], fill=(0, 0, 0, 0))  # hollow center
    # Thorny protrusions
    thorn_color = (100, 60, 30, 255)
    # Top thorns
    draw.polygon([(8, 9), (9, 4), (10, 9)], fill=thorn_color)
    draw.polygon([(14, 9), (15, 4), (16, 9)], fill=thorn_color)
    draw.polygon([(11, 8), (12, 3), (13, 8)], fill=thorn_color)
    # Side thorns
    draw.polygon([(3, 13), (1, 12), (4, 11)], fill=thorn_color)
    draw.polygon([(21, 13), (23, 12), (20, 11)], fill=thorn_color)
    # Blood drops (red)
    blood = (180, 30, 30, 255)
    draw.ellipse([7, 17, 9, 20], fill=blood)
    draw.ellipse([15, 18, 17, 21], fill=blood)
    draw.ellipse([11, 19, 13, 22], fill=blood)
    # Blood on crown
    draw.point((6, 14), fill=blood)
    draw.point((18, 14), fill=blood)
    draw.point((12, 8), fill=blood)
    # Highlight
    draw.point((8, 10), fill=(160, 110, 70, 255))
    draw.point((16, 10), fill=(160, 110, 70, 255))
    return img

# ============================================================
# 8. Stigmata - bloody cross/wound marks on pale flesh
# ============================================================
def create_stigmata():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Pale skin circle (like a palm)
    draw.ellipse([3, 3, 21, 21], fill=(230, 200, 180, 255))
    draw.ellipse([3, 3, 21, 21], outline=(200, 170, 150, 255))
    # Wound cross (dark red gash)
    wound = (160, 30, 30, 255)
    wound_light = (200, 50, 50, 255)
    # Vertical wound
    draw.rectangle([11, 6, 13, 18], fill=wound)
    # Horizontal wound
    draw.rectangle([6, 11, 18, 13], fill=wound)
    # Blood drops emanating
    draw.ellipse([10, 5, 14, 7], fill=wound_light)
    draw.ellipse([10, 17, 14, 19], fill=wound_light)
    draw.ellipse([5, 10, 7, 14], fill=wound_light)
    draw.ellipse([17, 10, 19, 14], fill=wound_light)
    # Center glow
    draw.point((12, 12), fill=(220, 70, 70, 255))
    # Blood drip
    draw.ellipse([11, 19, 13, 22], fill=(140, 20, 20, 255))
    return img

# ============================================================
# 9. Wire Hanger - simpler wire hanger (variant of coat hanger)
# ============================================================
def create_wire_hanger():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Hook at top (more pronounced)
    draw.arc([9, 1, 15, 7], 0, 360, fill=(170, 170, 180, 255), width=2)
    # Hanger shoulders (wider, flatter)
    draw.line([(9, 5), (2, 16)], fill=(150, 150, 165, 255), width=2)
    draw.line([(15, 5), (22, 16)], fill=(150, 150, 165, 255), width=2)
    # Bottom bar
    draw.line([(2, 16), (22, 16)], fill=(150, 150, 165, 255), width=2)
    # Blood stains (this is Isaac after all - it's stuck in Isaac's head)
    draw.ellipse([10, 14, 14, 18], fill=(180, 40, 40, 255))
    draw.ellipse([8, 16, 11, 19], fill=(160, 30, 30, 200))
    # Highlight
    draw.point((12, 2), fill=(210, 210, 220, 255))
    draw.point((6, 10), fill=(190, 190, 200, 255))
    return img

# ============================================================
# 10. Growth Hormones - purple syringe
# ============================================================
def create_growth_hormones():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Syringe barrel (angled diagonal)
    # Draw at slight angle for visual interest
    draw.rectangle([4, 9, 18, 15], fill=(200, 200, 210, 255))
    # Purple liquid inside
    draw.rectangle([6, 10, 14, 14], fill=(160, 80, 180, 255))
    # Liquid shine
    draw.line([(7, 11), (12, 11)], fill=(180, 100, 200, 255))
    # Plunger
    draw.rectangle([17, 10, 22, 14], fill=(180, 180, 190, 255))
    draw.line([(17, 10), (17, 14)], fill=(140, 140, 150, 255))
    # Needle
    draw.line([(4, 12), (1, 12)], fill=(180, 180, 190, 255), width=1)
    draw.point((0, 12), fill=(200, 200, 210, 255))
    # Barrel markings
    draw.line([(7, 9), (7, 15)], fill=(170, 170, 180, 255))
    draw.line([(10, 9), (10, 15)], fill=(170, 170, 180, 255))
    draw.line([(13, 9), (13, 15)], fill=(170, 170, 180, 255))
    # Outline
    draw.rectangle([4, 9, 18, 15], outline=(130, 130, 140, 255))
    # "Growth" hint - small + symbol
    draw.line([(11, 3), (11, 7)], fill=(120, 200, 120, 255), width=1)
    draw.line([(9, 5), (13, 5)], fill=(120, 200, 120, 255), width=1)
    return img

# ============================================================
# 11. Jesus Juice - purple juice box
# ============================================================
def create_jesus_juice():
    img = new_sprite()
    draw = ImageDraw.Draw(img)
    # Juice box body (purple)
    draw.rectangle([5, 6, 19, 21], fill=(140, 60, 160, 255))
    # Top of box (slightly lighter, folded)
    draw.polygon([(5, 6), (12, 3), (19, 6)], fill=(160, 80, 180, 255))
    # Label area (lighter purple)
    draw.rectangle([7, 9, 17, 18], fill=(170, 90, 190, 255))
    # Cross on label (golden)
    draw.line([(12, 10), (12, 17)], fill=(220, 200, 80, 255), width=1)
    draw.line([(9, 13), (15, 13)], fill=(220, 200, 80, 255), width=1)
    # Straw (coming out of top)
    draw.line([(14, 6), (16, 1)], fill=(200, 200, 200, 255), width=1)
    draw.line([(16, 1), (18, 1)], fill=(200, 200, 200, 255), width=1)
    # Box outline
    draw.rectangle([5, 6, 19, 21], outline=(100, 40, 120, 255))
    # Highlight
    draw.line([(6, 7), (6, 20)], fill=(180, 100, 200, 255))
    # Shine
    draw.point((7, 8), fill=(200, 130, 220, 255))
    return img

# ============================================================
# Create all sprites
# ============================================================
items = {
    "item_wire_coat_hanger": create_wire_coat_hanger,
    "item_lunch": create_lunch,
    "item_cupids_arrow": create_cupids_arrow,
    "item_spelunker_hat": create_spelunker_hat,
    "item_speed_ball": create_speed_ball,
    "item_spirit_sword": create_spirit_sword,
    "item_blood_of_martyr": create_blood_of_martyr,
    "item_stigmata": create_stigmata,
    "item_wire_hanger": create_wire_hanger,
    "item_growth_hormones": create_growth_hormones,
    "item_jesus_juice": create_jesus_juice,
}

os.makedirs(OUT_DIR, exist_ok=True)

for name, creator in items.items():
    img = creator()
    path = os.path.join(OUT_DIR, f"{name}.png")
    img.save(path, "PNG")
    print(f"✅ Created {path} ({img.size[0]}x{img.size[1]})")

print(f"\nDone! Created {len(items)} item sprites.")
