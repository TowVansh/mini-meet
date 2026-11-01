#!/usr/bin/env bash
# One terminal scene for the documentation screenshots: bash scene.sh <name>
cd "$(dirname "$0")/../.." || exit 1
run() { echo "\$ $*"; eval "$*"; echo; }

case "$1" in
  server)
    run "bin/mm-server 9702"
    ;;
  signal)
    run "python3 docs/doc/signal_demo.py 9702"
    ;;
  test)
    run "make -s test"
    ;;
  netem)
    ip netns add docns; ip -n docns link set lo up
    for p in loss5 jitter30 rate1m; do
      run "ip netns exec docns scripts/impair.sh $p lo"
    done
    ip netns del docns
    ;;
  cc)
    run "grep 'cc\]' results/eval_rate1m_adapt1_p2__botA.log | cut -c1-90 | head -12"
    ;;
  nat)
    run "cat results/nat_c_client.log"
    run "tail -n +2 results/nat_c__Laptop.csv | awk -F, '{print \$NF}' | sort | uniq -c"
    ;;
esac
sleep 300
