#!/usr/bin/env bash
# Rebuild the deck and render slide images for a visual check (WSL/Linux).
#   bash docs/slides/qa.sh <pptx-skill-dir> <out-dir> [dpi] [slides...]
# LibreOffice runs on Windows (soffice.exe); pdftoppm here.
set -e
SKILL="$1"; OUT="$2"; DPI="${3:-60}"; shift 3 || true
cd "$(dirname "$0")/../.."
PPTX_SKILL="$SKILL" NODE_PATH=~/deckgen/node_modules node docs/slides/build_deck.js
python3 "$SKILL/scripts/office/validate.py" Mini-Meet-Slides.pptx | tail -1
mkdir -p "$OUT"
WINOUT=$(wslpath -m "$OUT")
"/mnt/c/Program Files/LibreOffice/program/soffice.exe" -env:UserInstallation=file:///C:/Users/Towering/AppData/Local/Temp/lo-profile \
  --headless --convert-to pdf --outdir "$WINOUT" "$(wslpath -m Mini-Meet-Slides.pptx)" > /dev/null 2>&1
cd "$OUT"
rm -f slide-*.jpg
if [ $# -gt 0 ]; then
  for s in "$@"; do pdftoppm -jpeg -r "$DPI" -f "$s" -l "$s" Mini-Meet-Slides.pdf slide; done
else
  pdftoppm -jpeg -r "$DPI" Mini-Meet-Slides.pdf slide
fi
ls -1 "$OUT"/slide-*.jpg
