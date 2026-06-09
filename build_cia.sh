#!/usr/bin/env bash
# =============================================================================
# build_cia.sh -- Standalone CIA build pipeline for Binding of Isaac 3DS.
#
# This is an alternative to `make cia`. It does everything in one self-
# contained script:
#   1. Sanity-check that makerom + bannertool are installed
#   2. (Re)build the .elf via `make`
#   3. Build banner.bnr + icon.icn with bannertool
#   4. Run makerom to produce the final .cia
#
# The RSF (app.rsf) is configured for ORIGINAL 3DS hardware safety:
#   * SystemMode      : 64MB     (NOT 96MB -- 96MB triggers the
#                                 "An error has occurred / SD removed" crash
#                                 on O3DS at boot)
#   * SystemModeExt   : Legacy   (force pre-N3DS execution mode)
#   * CpuSpeed        : 268MHz   (O3DS native)
#   * EnableL2Cache   : false
#   * CanAccessCore2  : false
# =============================================================================
set -euo pipefail

# --- Paths -------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

TARGET="binding_of_isaac_3ds"
ELF="$PROJECT_DIR/$TARGET.elf"
RSF="$PROJECT_DIR/app.rsf"
ROMFS_DIR="$PROJECT_DIR/romfs"

ASSETS_DIR="$PROJECT_DIR/cia_assets"
BANNER_PNG="$ASSETS_DIR/banner.png"
BANNER_WAV="$ASSETS_DIR/audio.wav"
ICON_PNG="$ASSETS_DIR/icon.png"
BANNER_BNR="$ASSETS_DIR/banner.bnr"
ICON_ICN="$ASSETS_DIR/icon.icn"
CIA_OUT="$PROJECT_DIR/$TARGET.cia"

CIA_LONG_TITLE="Binding of Isaac 3DS"
CIA_SHORT_TITLE="Isaac 3DS"
CIA_PUBLISHER="homebrew"

# --- Locate tools ------------------------------------------------------------
# Prefer system-installed tools, fall back to known dev locations.
find_tool() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
    for candidate in \
        "/opt/devkitpro/tools/bin/$name" \
        "/usr/local/bin/$name" \
        "$HOME/cia_tools/$name" \
        "$DEVKITPRO/tools/bin/$name" \
    ; do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

MAKEROM="$(find_tool makerom || true)"
BANNERTOOL="$(find_tool bannertool || true)"

if [[ -z "$MAKEROM" ]]; then
    echo "ERROR: makerom not found." >&2
    echo "  Install from: https://github.com/3DSGuy/Project_CTR/releases" >&2
    exit 1
fi
if [[ -z "$BANNERTOOL" ]]; then
    echo "ERROR: bannertool not found." >&2
    echo "  Install from: https://github.com/carstene1ns/3ds-bannertool/releases" >&2
    echo "  (or the legacy: https://github.com/diasurgical/bannertool/releases)" >&2
    exit 1
fi

echo "=============================================================="
echo " Binding of Isaac 3DS - CIA build pipeline (O3DS-safe)"
echo "=============================================================="
echo " makerom    : $MAKEROM"
echo " bannertool : $BANNERTOOL"
echo " project    : $PROJECT_DIR"
echo "=============================================================="

# --- Sanity check required input files ---------------------------------------
for f in "$RSF" "$BANNER_PNG" "$BANNER_WAV" "$ICON_PNG"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: required input file is missing: $f" >&2
        exit 1
    fi
done
if [[ ! -d "$ROMFS_DIR" ]]; then
    echo "ERROR: romfs directory not found at $ROMFS_DIR" >&2
    exit 1
fi

# --- 1. Build the .elf -------------------------------------------------------
echo "[1/4] Building .elf via make ..."
( cd "$PROJECT_DIR" && make )
if [[ ! -f "$ELF" ]]; then
    echo "ERROR: $ELF was not produced by 'make'." >&2
    exit 1
fi

# --- 2. Build banner.bnr -----------------------------------------------------
echo "[2/4] Building banner.bnr with bannertool ..."
"$BANNERTOOL" makebanner \
    -i "$BANNER_PNG" \
    -a "$BANNER_WAV" \
    -o "$BANNER_BNR" >/dev/null
[[ -f "$BANNER_BNR" ]] || { echo "ERROR: banner.bnr was not produced." >&2; exit 1; }

# --- 3. Build icon.icn (SMDH) ------------------------------------------------
echo "[3/4] Building icon.icn (SMDH) with bannertool ..."
"$BANNERTOOL" makesmdh \
    -s "$CIA_SHORT_TITLE" \
    -l "$CIA_LONG_TITLE" \
    -p "$CIA_PUBLISHER" \
    -i "$ICON_PNG" \
    -o "$ICON_ICN" >/dev/null
[[ -f "$ICON_ICN" ]] || { echo "ERROR: icon.icn was not produced." >&2; exit 1; }

# --- 4. Pack the CIA ---------------------------------------------------------
# NOTE: '-exefslogo' is deliberately NOT passed.  Including it together with a
# RSF that declares 'Logo: Homebrew' produces a CIA whose ExeFS logo region
# trips the HOME Menu loader on launch ("Translation - Page" fault). The
# sm64-port template (proven working on O3DS + N3DS under Luma3DS) omits it
# and we follow that recipe.
echo "[4/4] Packing CIA with makerom (romfs from $ROMFS_DIR) ..."
"$MAKEROM" -f cia \
    -o "$CIA_OUT" \
    -rsf "$RSF" \
    -target t \
    -elf "$ELF" \
    -icon "$ICON_ICN" \
    -banner "$BANNER_BNR" \
    -DAPP_ROMFS="$ROMFS_DIR"

if [[ ! -f "$CIA_OUT" ]]; then
    echo "ERROR: CIA build failed -- $CIA_OUT was not produced." >&2
    exit 1
fi

echo
echo "=============================================================="
echo " SUCCESS - CIA built:"
ls -lh "$CIA_OUT"
echo
echo " Install on a CFW-enabled 3DS with FBI, or load in Citra."
echo " O3DS-safe profile (64MB / Legacy / 268MHz) is active."
echo "=============================================================="
