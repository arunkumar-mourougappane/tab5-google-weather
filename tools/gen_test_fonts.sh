#!/usr/bin/env bash
# Regenerates the hero-digit test fonts consumed by examples/font_size_test
# (see platformio.ini's [env:font-test]). Not part of the firmware build
# itself - run this by hand whenever the size ladder below changes.
#
# lv_font_conv (via npx, no global install needed) can't read the .woff2
# files in docs/mockups/assets/fonts/ directly - it wants ttf/otf - so this
# first converts to a temp .ttf with fonttools, then runs lv_font_conv per
# size. Requires: `pip3 install fonttools brotli` (brotli is what makes
# fonttools able to decode woff2) and `npx` (ships with node/npm).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

SRC_WOFF2="docs/mockups/assets/fonts/archivo-variable.woff2"
OUT_DIR="src/fonts_test"
TMP_TTF="$(mktemp -t archivo-variable.XXXXXX.ttf)"
trap 'rm -f "$TMP_TTF"' EXIT

# Same subset the hero temperature readout in docs/mockups/dashboard.html
# actually needs (docs/rendering.md's "narrow per-purpose fonts" point) -
# this is a legibility test for that one element, not a general Latin font.
SYMBOLS="0123456789°"

# Ladder spans LVGL's largest built-in bitmap font (48px, the current
# ceiling per docs/rendering.md) up through the mockup's 108px hero digit
# and one step past it, so the on-device comparison brackets the open
# question instead of just confirming one candidate size.
SIZES=(48 64 80 96 108 128)

mkdir -p "$OUT_DIR"

python3 -c "
from fontTools.ttLib import TTFont
f = TTFont('$SRC_WOFF2')
f.flavor = None
f.save('$TMP_TTF')
"

for size in "${SIZES[@]}"; do
  name="font_archivo_${size}"
  echo "generating ${name} (${size}px)..."
  npx --yes lv_font_conv \
    --font "$TMP_TTF" \
    --size "$size" \
    --bpp 4 \
    --format lvgl \
    --symbols "$SYMBOLS" \
    --lv-font-name "$name" \
    -o "$OUT_DIR/${name}.c"
done

echo "done. Regenerate include/fonts_test.h by hand if the size ladder changed."
