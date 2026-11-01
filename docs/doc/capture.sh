#!/usr/bin/env bash
# Capture real program output for the project documentation (run as root in WSL).
# Writes docs/doc/out/*.txt; build_doc.py turns them into the document.
cd "$(dirname "$0")/../.."
OUT=docs/doc/out; mkdir -p "$OUT"
make -s all test > /dev/null

{ echo '$ make test'; bin/test; } > "$OUT/test.txt" 2>&1

# signalling: real server, driven with plain text like telnet
bin/mm-server 9701 > /tmp/doc-srv.log 2>&1 &
S=$!
sleep 0.5
python3 - > "$OUT/signal.txt" <<'EOF'
import socket, time
def conn():
    s = socket.create_connection(("127.0.0.1", 9701)); s.settimeout(0.4); return s
def say(s, who, line):
    print(f"{who} -> {line}"); s.sendall((line + "\n").encode()); time.sleep(0.15)
def hear(s, who):
    try:
        for l in s.recv(4096).decode().splitlines(): print(f"{who} <- {l}")
    except socket.timeout:
        pass
a, b = conn(), conn()
say(a, "PC1", "JOIN demo PC1"); hear(a, "PC1")
say(b, "PC2", "JOIN demo PC2"); hear(b, "PC2")
hear(a, "PC1")
pc1 = None
print("PC1 -> PING"); a.sendall(b"PING\n"); time.sleep(0.15); hear(a, "PC1")
say(b, "PC2", "JOIN bad!room PC2"); hear(b, "PC2")
say(b, "PC2", "LEAVE"); hear(a, "PC1")
EOF
kill $S; { echo '$ mm-server 9701'; cut -c10- /tmp/doc-srv.log; } > "$OUT/server.txt"

# impairment: the real tc commands on a throwaway namespace
ip netns add docns; ip -n docns link set lo up
{
  for p in loss5 jitter30 rate1m; do
    echo "\$ scripts/impair.sh $p"; ip netns exec docns scripts/impair.sh "$p" lo
  done
} > "$OUT/netem.txt" 2>&1
ip netns del docns

# controller reacting during the recorded 1 Mbit/s run
{ echo '$ grep "cc]" results/eval_rate1m_adapt1_p2__botA.log'; grep "cc\]" results/eval_rate1m_adapt1_p2__botA.log | cut -c10-110 | head -12; } > "$OUT/cc.txt"
cat "$OUT"/signal.txt
