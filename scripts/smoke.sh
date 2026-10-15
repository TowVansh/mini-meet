#!/usr/bin/env bash
# Quick local check: server + two headless test clients for N seconds.
#   bash scripts/smoke.sh [seconds] [extra mm flags...]
cd "$(dirname "$0")/.."
SECS="${1:-12}"; shift || true
LOG=/tmp/mm-smoke; mkdir -p $LOG
bin/mm-server 9101 > $LOG/server.log 2>&1 &
SRV=$!
sleep 0.5
bin/mm --server 127.0.0.1:9101 --room smoke --name alice --test --no-stun --headless --duration $((SECS + 2)) "$@" > $LOG/alice.log 2>&1 &
sleep 1
bin/mm --server 127.0.0.1:9101 --room smoke --name bob --test --no-stun --headless --duration "$SECS" "$@" > $LOG/bob.log 2>&1
sleep 2
kill $SRV 2>/dev/null
grep -hE "CONNECTED|crypto|cc\]" $LOG/alice.log $LOG/bob.log
echo "--- alice last stats"
grep "stats\]" $LOG/alice.log | tail -2 | cut -c1-210
