"""Talk to a running mm-server with plain text lines and print both directions."""

import socket
import sys
import time

PORT = int(sys.argv[1])


def connect():
    s = socket.create_connection(("127.0.0.1", PORT))
    s.settimeout(0.4)
    return s


def say(s, who, line):
    print(f"{who} -> {line}", flush=True)
    s.sendall((line + "\n").encode())
    time.sleep(0.15)


def hear(s, who):
    try:
        for line in s.recv(4096).decode().splitlines():
            print(f"{who} <- {line}", flush=True)
    except socket.timeout:
        pass


pc1, pc2 = connect(), connect()
say(pc1, "PC1", "JOIN demo PC1")
hear(pc1, "PC1")
say(pc2, "PC2", "JOIN demo PC2")
hear(pc2, "PC2")
hear(pc1, "PC1")
say(pc1, "PC1", "PING")
hear(pc1, "PC1")
say(pc2, "PC2", "JOIN other PC2")
hear(pc2, "PC2")
say(pc2, "PC2", "LEAVE")
hear(pc1, "PC1")
