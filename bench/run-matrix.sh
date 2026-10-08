#!/usr/bin/env bash
# Impairment evaluation for the C client. For each netem profile and
# adaptation setting: start a call between headless mm clients on this
# machine, apply the profile on lo after a clean warm-up, record stats, stop.
#
# Run as root (tc needs it), from Windows:
#   wsl -d Ubuntu -u root -- bash /mnt/e/MiniMeet/bench/run-matrix.sh
# Env: PROFILES="baseline loss5" ADAPT="0 1" DURATION=45 WARMUP=10 PEERS=2 TAG=eval
set -uo pipefail
cd "$(dirname "$0")/.."

PROFILES="${PROFILES:-baseline loss1 loss5 loss10 loss20 burst5 delay100 jitter30 jitter60 rate2m rate1m rate500k rate250k combo}"
ADAPT="${ADAPT:-0 1}"
DURATION="${DURATION:-45}"
WARMUP="${WARMUP:-10}"
PEERS="${PEERS:-2}"
TAG="${TAG:-$(date +%Y%m%d%H%M)}"
PORT="${PORT:-9300}"
NAMES=(botA botB botC botD)

mkdir -p results
make -s all || exit 1
cleanup() { scripts/impair.sh clear >/dev/null; kill "$SERVER" 2>/dev/null; }
trap cleanup EXIT

bin/mm-server "$PORT" > results/server.log 2>&1 &
SERVER=$!
sleep 0.5

for profile in $PROFILES; do
  for adapt in $ADAPT; do
    run="${TAG}_${profile}_adapt${adapt}_p${PEERS}"
    echo "=== $run"
    scripts/impair.sh clear >/dev/null
    flag=""; [ "$adapt" = "0" ] && flag="--no-adapt"
    pids=()
    for ((i = 0; i < PEERS; i++)); do
      name="${NAMES[$i]}"
      bin/mm --server "127.0.0.1:$PORT" --room "b-${profile}-${adapt}" --name "$name" --test --no-stun \
        --headless $flag --duration $((WARMUP + DURATION + PEERS - i)) \
        --csv "results/${run}__${name}.csv" --run "$run" > "results/${run}__${name}.log" 2>&1 &
      pids+=($!)
      sleep 1
    done
    sleep "$WARMUP"
    scripts/impair.sh "$profile" > /dev/null
    wait "${pids[@]}"
    scripts/impair.sh clear > /dev/null
    sleep 1
  done
done
echo "Done. Next: python3 analysis/plot.py results/${TAG}_*.csv"
