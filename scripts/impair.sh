#!/usr/bin/env bash
# Apply a named tc netem impairment profile to an interface (default: lo).
#
#   sudo scripts/impair.sh loss5          # 5 % random loss on lo
#   sudo scripts/impair.sh rate1m eth0    # 1 Mbit/s cap on eth0
#   sudo scripts/impair.sh clear
#   scripts/impair.sh list
#
# Note: on lo every packet crosses the qdisc once, so delay/jitter apply per
# direction and the round trip sees roughly double the configured delay.
set -euo pipefail

PROFILE="${1:-list}"
DEV="${2:-lo}"

profile_args() {
  case "$1" in
    baseline)    echo "" ;;
    loss1)       echo "loss 1%" ;;
    loss5)       echo "loss 5%" ;;
    loss10)      echo "loss 10%" ;;
    loss20)      echo "loss 20%" ;;
    burst5)      echo "loss gemodel 1% 30% 70% 0.1%" ;;   # bursty (Gilbert-Elliott)
    delay100)    echo "delay 100ms" ;;
    jitter30)    echo "delay 100ms 30ms distribution normal" ;;
    jitter60)    echo "delay 100ms 60ms distribution normal" ;;
    rate2m)      echo "rate 2mbit" ;;
    rate1m)      echo "rate 1mbit" ;;
    rate500k)    echo "rate 500kbit" ;;
    rate250k)    echo "rate 250kbit" ;;
    combo)       echo "delay 80ms 20ms distribution normal loss 3% rate 1mbit" ;;
    *)           return 1 ;;
  esac
}

PROFILES="baseline loss1 loss5 loss10 loss20 burst5 delay100 jitter30 jitter60 rate2m rate1m rate500k rate250k combo"

case "$PROFILE" in
  list)
    echo "$PROFILES"
    exit 0 ;;
  clear)
    tc qdisc del dev "$DEV" root 2>/dev/null || true
    echo "cleared $DEV"
    exit 0 ;;
esac

if ! ARGS="$(profile_args "$PROFILE")"; then
  echo "unknown profile '$PROFILE'. Profiles: $PROFILES" >&2
  exit 1
fi

tc qdisc del dev "$DEV" root 2>/dev/null || true
if [ -n "$ARGS" ]; then
  # shellcheck disable=SC2086
  tc qdisc add dev "$DEV" root netem $ARGS
fi
echo "$DEV: ${ARGS:-no impairment}"
tc qdisc show dev "$DEV"
