#!/usr/bin/env python3
"""Extract first frame from official Isaac sprite sheets and resize to game dimensions."""
from PIL import Image
import numpy as np

SRC = "sprite_sources"
DST = "gfx_clean/resized"

def extract_frame(img, left, top, right, bottom):
    """Extract a rectangular region and crop to tight bounding box."""
    frame = img.crop((left, top, right, bottom))
    arr = np.array(frame)
    alpha = arr[:,:,3]
    rows = np.any(alpha > 10, axis=1)
    cols = np.any(alpha > 10, axis=0)
    if not rows.any():
        return frame
    rmin, rmax = np.where(rows)[0][[0, -1]]
    cmin, cmax = np.where(cols)[0][[0, -1]]
    return frame.crop((cmin, rmin, cmax+1, rmax+1))

def resize_to(img, size, method=Image.LANCZOS):
    """Resize maintaining aspect ratio, center on transparent canvas."""
    w, h = img.size
    tw, th = size, size
    scale = min(tw / w, th / h)
    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
    resized = img.resize((nw, nh), method)
    canvas = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
    canvas.paste(resized, ((tw - nw) // 2, (th - nh) // 2))
    return canvas

def clean_alpha(img, threshold=5):
    """Make near-transparent pixels fully transparent."""
    arr = np.array(img)
    arr[arr[:,:,3] < threshold] = [0, 0, 0, 0]
    return Image.fromarray(arr)

# ===== ENEMY SPRITES =====

# Attack Fly - sheet has multiple fly sprites in a grid pattern
# The sheet is 95x56 with black/red flies on transparent bg
# Individual flies are ~16x16, arranged in rows
img = Image.open(f"{SRC}/attack_fly.png").convert('RGBA')
# The first sprite (top-left area) should be a single black/attack fly
# Let's take first ~20x20 region
frame = extract_frame(img, 0, 0, 30, 28)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_attack_fly.png")
print(f"attack_fly: extracted {frame.size} -> 24x24")

# Pooter - 128x128 with 4x4 grid of frames
# First frame at approx (6,3)-(27,23) 
img = Image.open(f"{SRC}/pooter.png").convert('RGBA')
# Grid is roughly 32x32 cells
frame = extract_frame(img, 0, 0, 32, 32)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_pooter.png")
print(f"pooter: extracted {frame.size} -> 24x24")

# Hopper - 176x61, frames are ~30px wide, 2 rows
# First frame at cols 0-18, row 0-29
img = Image.open(f"{SRC}/hopper.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 29, 30)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_hopper.png")
print(f"hopper: extracted {frame.size} -> 24x24")

# Baby - use the pooter sheet's smaller frame or create from globin
# Actually, "Baby" in Isaac is a crawling pink/flesh enemy
# No direct sprite sheet found - let's extract from mulligan which has similar body style
# For now, use a globin-like extraction but smaller
# Let me check if there's a baby-specific asset we missed
# Baby enemy might be under a different name - let's use a portion of mulligan's sheet
img = Image.open(f"{SRC}/mulligan.png").convert('RGBA')
# Mulligan sheet: first frame at cols 7-25, rows 5-37 (18x32)
# The head/body of mulligan is roughly baby-like
frame = extract_frame(img, 7, 5, 26, 38)
# Baby should look smaller and more basic than mulligan
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_baby.png")
print(f"baby (from mulligan): extracted {frame.size} -> 24x24")

# Globin - 290x247, first frame at cols 1-30, rows 3-29 (29x26)
img = Image.open(f"{SRC}/globin.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 33, 33)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_globin.png")
print(f"globin: extracted {frame.size} -> 24x24")

# Boom Fly - 171x62, it's a full sheet with multiple frames
# The original Binding of Isaac format - frames arranged horizontally
img = Image.open(f"{SRC}/boom_fly.png").convert('RGBA')
# Try to extract first 30x30 region
frame = extract_frame(img, 0, 0, 40, 35)
if frame.size[0] < 5 or frame.size[1] < 5:
    # Fallback to tainted boom fly
    img2 = Image.open(f"{SRC}/tainted_boom_fly.png").convert('RGBA')
    frame = extract_frame(img2, 0, 0, 48, 48)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_boom_fly.png")
print(f"boom_fly: extracted {frame.size} -> 24x24")

# Maw - use Red Maw sheet (32x30, single frame essentially)
img = Image.open(f"{SRC}/red_maw.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 32, 30)
# Save as enemy_maw (they look similar, maw is just darker)
result = resize_to(frame, 24)
result = clean_alpha(result)
# Darken slightly for regular maw
arr = np.array(result)
mask = arr[:,:,3] > 10
arr[mask, 0] = np.clip(arr[mask, 0].astype(int) * 8 // 10, 0, 255).astype(np.uint8)
arr[mask, 1] = np.clip(arr[mask, 1].astype(int) * 7 // 10, 0, 255).astype(np.uint8)
maw_result = Image.fromarray(arr)
maw_result.save(f"{DST}/enemy_maw.png")
print(f"maw (from red_maw, darkened): extracted {frame.size} -> 24x24")

# Mulligan - 812x286, first frame at cols 7-25, rows 5-37
img = Image.open(f"{SRC}/mulligan.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 35, 42)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_mulligan.png")
print(f"mulligan: extracted {frame.size} -> 24x24")

# Host - 96x64, first frame at cols 2-30, rows 20-64
img = Image.open(f"{SRC}/host.png").convert('RGBA')
frame = extract_frame(img, 0, 18, 33, 64)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_host.png")
print(f"host: extracted {frame.size} -> 24x24")

# Red Maw - 32x30, essentially one frame
img = Image.open(f"{SRC}/red_maw.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 32, 30)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_red_maw.png")
print(f"red_maw: extracted {frame.size} -> 24x24")

# Leaper - 176x61, same layout as hopper (6 h-frames, 2 rows)
img = Image.open(f"{SRC}/leaper.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 29, 30)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_leaper.png")
print(f"leaper: extracted {frame.size} -> 24x24")

# Vis - 1415x230, large sheet with many frames
# Vis is a large creature - first substantial frame is at cols 6-165
img = Image.open(f"{SRC}/vis.png").convert('RGBA')
# First "idle" frame should be the head/body portion
frame = extract_frame(img, 0, 0, 50, 50)
if frame.size[0] < 8 or frame.size[1] < 8:
    # Try larger region
    frame = extract_frame(img, 0, 0, 170, 200)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/enemy_vis.png")
print(f"vis: extracted {frame.size} -> 24x24")

# ===== ITEM SPRITES =====

# Blood of the Martyr - 256x96 sprite sheet
img = Image.open(f"{SRC}/blood_martyr.png").convert('RGBA')
# Item sheets typically have the item sprite as first frame, 32x32 grid
frame = extract_frame(img, 0, 0, 34, 34)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_blood_of_martyr.png")
print(f"blood_martyr: extracted {frame.size} -> 24x24")

# Cupid's Arrow - 256x96
img = Image.open(f"{SRC}/cupids_arrow_rebirth.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 34, 34)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_cupids_arrow.png")
print(f"cupids_arrow: extracted {frame.size} -> 24x24")

# Growth Hormones - 262x507
img = Image.open(f"{SRC}/growth_hormones.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 40, 40)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_growth_hormones.png")
print(f"growth_hormones: extracted {frame.size} -> 24x24")

# Jesus Juice - 262x73
img = Image.open(f"{SRC}/jesus_juice.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 40, 40)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_jesus_juice.png")
print(f"jesus_juice: extracted {frame.size} -> 24x24")

# Stigmata - 262x105
img = Image.open(f"{SRC}/stigmata.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 40, 40)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_stigmata.png")
print(f"stigmata: extracted {frame.size} -> 24x24")

# Speed Ball - 260x330
img = Image.open(f"{SRC}/speed_ball.png").convert('RGBA')
frame = extract_frame(img, 0, 0, 40, 40)
result = resize_to(frame, 24)
result = clean_alpha(result)
result.save(f"{DST}/item_speed_ball.png")
print(f"speed_ball: extracted {frame.size} -> 24x24")

print("\n=== Done! ===")
print("Items not found on Spriters Resource (will need high-quality pixel art):")
print("  - item_lunch")
print("  - item_spelunker_hat")
print("  - item_spirit_sword")
print("  - item_wire_coat_hanger")
print("  - item_wire_hanger")
