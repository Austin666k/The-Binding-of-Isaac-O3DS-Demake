#!/bin/bash
# Rebuild UI items atlas with updated heart sprites

echo "=== Rebuilding UI Items Atlas ==="

# Set devkitPro environment
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=${DEVKITPRO}/devkitARM
export PATH=${DEVKITPRO}/tools/bin:${DEVKITARM}/bin:$PATH

cd "$(dirname "$0")"

# Find tex3ds
TEX3DS=$(which tex3ds)
if [ -z "$TEX3DS" ]; then
    echo "Error: tex3ds not found in PATH"
    echo "Looking for tex3ds..."
    TEX3DS=$(find /opt/devkitpro -name tex3ds 2>/dev/null | head -1)
    if [ -z "$TEX3DS" ]; then
        echo "Error: tex3ds not installed"
        echo "Install with: sudo dkp-pacman -S tex3ds"
        exit 1
    fi
fi

echo "Using tex3ds: $TEX3DS"
echo ""

# Build the atlas
echo "Building ui_items.t3x..."
$TEX3DS -i gfx/ui_items.t3s -o romfs/ui_items.t3x

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ UI items atlas rebuilt successfully!"
    echo "  Output: romfs/ui_items.t3x"
    echo ""
    echo "Heart sprites now include:"
    echo "  - Red hearts (full, half, empty)"
    echo "  - Soul hearts (full, half)"
    echo "  - Black hearts (full)"
else
    echo ""
    echo "✗ Failed to rebuild atlas"
    exit 1
fi
