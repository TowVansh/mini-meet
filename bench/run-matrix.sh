#!/usr/bin/env bash
# Impairment evaluation for the C client.
#
# Two Linux network namespaces joined by a veth pair act as two separate
# hosts:   [mmA: 10.10.0.1  vA] <----veth----> [vB  10.10.0.2 :mmB]
# netem runs on BOTH veth ends, so each direction of the call is impaired
# separately, like a real access link (a 1 Mbit/s profile = 1 Mbit/s each way).
# mm-server and botA run in mmA, botB in mmB.
#
# For each netem profile and adaptation setting: start the call, keep it
# clean for WARMUP seconds, apply the profile, record DURATION seconds.
#
# Run as root (namespaces and tc need it), from Windows:
#   wsl -d Ubuntu -u root -- bash /mnt/e/MiniMeet/bench/run-matrix.sh
# Env: PROFILES="baseline loss5" ADAPT="0 1" DURATION=45 WARMUP=10 TAG=eval
set -uo pipefail
cd "$(dirname "$0")/.."

PROFILES="${PROFILES:-baseline loss1 loss5 loss10 loss20 burst5 delay100 jitter30 jitter60 rate2m rate1m rate500k rate250k combo}"
ADAPT="${ADAPT:-0 1}"
DURATION="${DURATION:-45}"
WARMUP="${WARMUP:-10}"
TAG="${TAG:-$(date +%Y%m%d%H%M)}"
PORT=9300

mkdir -p results
make -s all || exit 1

setup_ns() {
  ip netns del mmA 2>/dev/null; ip netns del mmB 2>/dev/null
  ip netns add mmA && ip netns add mmB
  ip link add vA type veth peer name vB
  ip link set vA netns mmA && ip link set vB netns mmB
  ip -n mmA addr add 10.10.0.1/24 dev vA && ip -n mmB addr add 10.10.0.2/24 dev vB
  for ns in mmA mmB; do ip -n $ns link set lo up; done
  ip -n mmA link set vA up && ip -n mmB link set vB up
  # default routes so each client finds its "LAN" address as host candidate
  ip -n mmA route add default via 10.10.0.2 && ip -n mmB route add default via 10.10.0.1
}
impair() {   # apply profile to both directions
  ip netns exec mmA scripts/impair.sh "$1" vA > /dev/null
  ip netns exec mmB scripts/impair.sh "$1" vB > /dev/null
}
cleanup() { kill "$SERVER" 2>/dev/null; ip netns del mmA 2>/dev/null; ip netns del mmB 2>/dev/null; }
trap cleanup EXIT

setup_ns || { echo "namespace setup failed"; exit 1; }
ip netns exec mmA bin/mm-server "$PORT" > results/server.log 2>&1 &
SERVER=$!
sleep 0.5

for profile in $PROFILES; do
  for adapt in $ADAPT; do
    run="${TAG}_${profile}_adapt${adapt}_p2"
    echo "=== $run"
    impair clear
    flag=""; [ "$adapt" = "0" ] && flag="--no-adapt"
    common="--room b-${profile}-${adapt} --test --no-stun --headless $flag --run $run"
    ip netns exec mmA bin/mm --server 127.0.0.1:$PORT --name botA $common --duration $((WARMUP + DURATION + 2)) \
      --csv "results/${run}__botA.csv" > "results/${run}__botA.log" 2>&1 &
    a=$!
    sleep 1
    ip netns exec mmB bin/mm --server 10.10.0.1:$PORT --name botB $common --duration $((WARMUP + DURATION + 1)) \
      --csv "results/${run}__botB.csv" > "results/${run}__botB.log" 2>&1 &
    b=$!
    sleep "$WARMUP"
    impair "$profile"
    wait $a $b
    impair clear
    sleep 1
  done
done
echo "Done. Next: python3 analysis/plot.py results/${TAG}_*.csv"
