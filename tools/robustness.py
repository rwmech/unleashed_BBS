#!/usr/bin/env python3
"""
File:        tools/robustness.py
Description: Hostile-input robustness checks for the BBS and its backup
             window. Every check ends by proving the server still answers.
             Run the host build with sanitizers (make SAN=1) to catch memory
             errors, or point it at a bench board running the backup test
             build. Needs the sysop password in system.cfg for the HTTP part.
               - telnet: random bytes, broken IAC sequences, overlong lines,
                 cursor-report floods, format strings in PAGE
               - connection storm beyond the socket budget
               - HTTP: slow headers, oversized headers, parallel clients,
                 bad Content-Length, path games, truncated bodies
               - corrupted zip uploads (random mutations), each answered N
Listing:     COMPLETE FILE
Libraries:   Python 3 standard library only
Usage:       python3 tools/robustness.py [host] [port] [--quick]
"""
import io
import random
import socket
import sys
import threading
import time
import zipfile

import testclient as tc

QUICK = "--quick" in tc.FLAGS
random.seed(6400)


def alive(label):
    c = tc.ansi_login("Probe")
    ok = b"Main" in c.buf
    c.send(b"who\r")
    ok = ok and c.wait_for(b"Who's online", 5)
    c.close()
    return tc.check(f"server alive after {label}", ok)


def raw(data, secs=0.5):
    try:
        s = socket.create_connection((tc.HOST, tc.PORT), timeout=5)
        s.sendall(data)
        time.sleep(secs)
        s.close()
    except OSError:
        pass


# ---------------------------------------------------------------------------
def telnet_checks():
    print("Telnet input")
    ok = True
    for _ in range(4 if QUICK else 10):
        raw(bytes(random.getrandbits(8) for _ in range(32000)), 0.2)
    ok &= alive("random bytes")

    raw(b"\xff\xfa\x1f" + b"A" * 20000, 0.3)                 # subnegotiation never ends
    raw(b"\xff" * 5000, 0.3)