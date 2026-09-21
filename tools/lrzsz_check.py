#!/usr/bin/env python3
"""Drive a real file transfer with lrzsz, an implementation nobody here wrote.

Why this exists
---------------
Every transfer bug on this board so far has been invisible to the project's
own test client, because that client was written alongside the board and
agreed with it. Twice it agreed with something wrong:

  - It sent raw bytes and doubled 0xFF, which is not what a telnet client
    does. A real terminal pads a bare CR with NUL under NVT rules, so every
    upload from SyncTERM arrived corrupt while every test passed.
  - It then honoured the board's request for RFC 856 binary mode. A terminal
    that declines keeps padding for ever, which the board did not handle,
    and again every test passed.

So the protocol end of this is `sz` from lrzsz, the reference XMODEM and
YMODEM sender. If the board and lrzsz disagree, one of them is wrong, and it
is almost certainly not the one that has been shipping since 1986.

The login and the walk into a section are borrowed from testclient, because
they are not what is under test, and rewriting them badly is how the first
attempt at this ended up typing "files 5" into a registration form.

Usage: BBS_SD_DIR=<card> lrzsz_check.py <port> [--refuse-binary] [--ymodem]

--refuse-binary plays the terminal that will not do telnet BINARY, which is
the case that broke on real hardware. Run it both ways: the board has to
cope with either answer.
"""
import os, pty, select, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testclient as tc


def bridge(sock, argv, seconds=120):
    """Run an lrzsz process with the socket as its line.

    Telnet sits in the middle, so bytes from the tool are escaped on the way
    out and unescaped on the way in. That layer is the board's problem too,
    which is exactly why it belongs here rather than being assumed away.
    """
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(argv[0], argv)
    sock.setblocking(False)
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd, sock], [], [], 0.2)
        if fd in r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if not d:
                break
            sock.sendall(tc._escape(d))
        if sock in r:
            try:
                raw = sock.recv(8192)
            except BlockingIOError:
                raw = b""
            except OSError:
                break
            if raw == b"":
                break
            if raw:
                os.write(fd, tc._unescape(raw))
        if os.waitpid(pid, os.WNOHANG)[0]:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    sock.setblocking(True)


def main():
    port = int(sys.argv[1])
    refuse = "--refuse-binary" in sys.argv
    ymodem = "--ymodem" in sys.argv

    tc.HOST = "127.0.0.1"
    tc.PORT = port
    tc._binary["refuse"] = refuse
    tc._binary["on"] = False

    sd = os.environ.get("BBS_SD_DIR", "")
    drop = os.path.join(sd, "pub", "drop")
    name = "REFTEST.BIN"
    landed = os.path.join(drop, ".pending", name)
    if os.path.exists(landed):
        os.remove(landed)

    # Full of the byte NVT pads, and of the byte telnet escapes.
    payload = (bytes([0x0D]) * 6 + bytes(range(256)) +
               bytes([0x0D, 0x0A]) * 20 + bytes([0xFF]) * 40) * 3
    src = os.path.join("/tmp", name)
    with open(src, "wb") as f:
        f.write(payload)

    s = tc.ansi_login("Reference")
    if not tc.enter_area(s, 5, b"Drop Box"):
        print("could not open the drop box")
        return 1

    s.buf.clear()
    s.send(b"u")
    if not s.wait_for(b"Upload", 6):
        print("no upload prompt")
        return 1
    s.buf.clear()
    if ymodem:
        s.send(b"\r")
        want = b"Start your YMODEM send"
        argv = ["sz", "--ymodem", "-q", src]
    else:
        s.send(name.encode() + b"\r")
        want = b"Start your XMODEM send"
        argv = ["sz", "--xmodem", "-q", src]
    if not s.wait_for(want, 8):
        print("board did not offer the transfer:", tc.plain(s.buf)[-200:])
        return 1

    # --delay N: sit still for N seconds before starting the transfer, the
    # way a person does while picking a file in their terminal's dialog.
    # The board polls 'C' every three seconds while waiting, and a receiver
    # that falls back from CRC to checksum after a few polls has changed
    # protocol behind a sender that already committed to CRC.
    delay = 0
    for i, a in enumerate(sys.argv):
        if a == "--delay" and i + 1 < len(sys.argv):
            delay = int(sys.argv[i + 1])
    if delay:
        print("waiting %d s before starting, like somebody choosing a file" % delay)
        end = time.time() + delay
        while time.time() < end:
            s.pump(0.5)
        s.buf.clear()

    print("protocol: %s   telnet binary agreed by client: %s"
          % ("YMODEM" if ymodem else "XMODEM", not refuse))
    bridge(s.s, argv)
    time.sleep(2.0)

    ok = os.path.exists(landed)
    print("file landed:", ok)
    if ok:
        with open(landed, "rb") as f:
            got = f.read()
        exact = got[:len(payload)] == payload
        print("bytes match: %s  (%d received, %d sent)" % (exact, len(got), len(payload)))
        ok = exact
        os.remove(landed)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
