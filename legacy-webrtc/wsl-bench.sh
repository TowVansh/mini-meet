#!/usr/bin/env bash
# Run the impairment matrix from Windows in one go (tc needs root):
#   wsl -d Ubuntu -u root -- bash /mnt/e/MiniMeet/scripts/wsl-bench.sh
# Accepts the same env vars as bench/run-matrix.js (PROFILES, ADAPT, DURATION, PEERS, TAG).
set -euo pipefail
cd "$(dirname "$0")/.."
# Chromium was downloaded by the normal user during npm install.
if [ -z "${PUPPETEER_CACHE_DIR:-}" ]; then
  for d in /home/*/.cache/puppeteer; do [ -d "$d" ] && export PUPPETEER_CACHE_DIR="$d"; done
fi
exec node bench/run-matrix.js
