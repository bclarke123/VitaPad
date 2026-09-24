#!/usr/bin/env python3
"""Pretends to be a Vita running VitaPad, to test the PC client without a Vita.

It answers discovery and serves animated input that cycles through every
feature, so the client (or a controller tester fed by ViGEm) always sees the
same sequence:

  - each button is held for 0.5 s in turn
  - both sticks move in circles
  - one finger visits the four front touch corners (L1, R1, L3, R3 in ViGEm mode)
  - a finger slides across the rear panel (DS4 touchpad)
  - the "Vita" slowly rocks left/right (gyro + accelerometer)

Usage:
  python3 fake_vita.py                    # normal Vita
  python3 fake_vita.py --drop-every 5     # close the connection every 5 s (tests reconnection)
  python3 fake_vita.py --legacy           # behave like an old Vita app (tests the "outdated" message)
  python3 fake_vita.py --no-discovery     # ignore discovery (tests the saved IP / manual IP paths)
"""

import argparse
import math
import socket
import struct
import threading
import time

PORT = 5000
BUTTONS = [
    ("SELECT", 0x0001), ("START", 0x0008), ("UP", 0x0010), ("RIGHT", 0x0020),
    ("DOWN", 0x0040), ("LEFT", 0x0080), ("L", 0x0100), ("R", 0x0200),
    ("TRIANGLE", 0x1000), ("CIRCLE", 0x2000), ("CROSS", 0x4000), ("SQUARE", 0x8000),
]
CORNERS = [(300, 200), (1600, 200), (300, 900), (1600, 900)]  # L1, R1, L3, R3


def state(t):
    buttons = BUTTONS[int(t / 0.5) % len(BUTTONS)][1]
    lx = int(128 + 127 * math.cos(t * 2))
    ly = int(128 + 127 * math.sin(t * 2))
    rx = int(128 + 127 * math.cos(-t * 2))
    ry = int(128 + 127 * math.sin(-t * 2))
    front = [CORNERS[int(t) % 4]] if int(t * 2) % 2 == 0 else []
    rear = [(int((t % 2) / 2 * 1919), 544)] if int(t / 2) % 2 == 0 else []
    # Rocking +-30 degrees around the Y axis: gravity (in G) and angular velocity (radians/s)
    angle = math.radians(30) * math.sin(t)
    accel = (-math.sin(angle), 0.0, -math.cos(angle))
    gyro = (0.0, math.radians(30) * math.cos(t), 0.0)
    return buttons, lx, ly, rx, ry, front, rear, accel, gyro


def packet_v2(t):
    buttons, lx, ly, rx, ry, front, rear, accel, gyro = state(t)
    def points(pts, base_id):
        data = b"".join(struct.pack("<HHBB", x, y, base_id + i, 0) for i, (x, y) in enumerate(pts))
        return data + b"\0" * (12 - len(data))
    return (struct.pack("<I4BBBH", buttons, lx, ly, rx, ry, len(front), len(rear), 0)
            + points(front, 1) + points(rear, 10)
            + struct.pack("<6fIB3x", *accel, *gyro, int(t * 1e6) & 0xFFFFFFFF, 80))


def packet_legacy(t):
    buttons, lx, ly, rx, ry, front, rear, _, _ = state(t)
    tx, ty = front[0] if front else (0, 0)
    click = (0x01 if front else 0) | ((0x10 if rear[0][0] > 960 else 0x08) if rear else 0)
    return struct.pack("<I4BHHB3x", buttons, lx, ly, rx, ry, tx, ty, click)


def discovery():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORT))
    while True:
        data, addr = s.recvfrom(64)
        if data.startswith(b"VITAPAD_DISCOVER"):
            print(f"Discovery request from {addr[0]}, answering")
            s.sendto(b"VITAPAD_HERE", addr)


def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--legacy", action="store_true", help="act like an old Vita app")
    parser.add_argument("--drop-every", type=float, default=0, help="drop the connection every N seconds")
    parser.add_argument("--no-discovery", action="store_true", help="don't answer discovery")
    args = parser.parse_args()

    if not args.no_discovery:
        threading.Thread(target=discovery, daemon=True).start()
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", PORT))
    server.listen(5)
    print(f"Fake Vita listening on port {PORT}")
    start = time.time()
    while True:
        conn, addr = server.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"Client connected from {addr[0]}")
        connected_at = time.time()
        while True:
            request = recv_exact(conn, 8)
            if request is None:
                print("Client disconnected")
                break
            t = time.time() - start
            if args.legacy or request != b"VPAD2\0\0\0":
                conn.sendall(packet_legacy(t))
            else:
                conn.sendall(packet_v2(t))
            time.sleep(0.002)
            if args.drop_every and time.time() - connected_at > args.drop_every:
                print("Dropping the connection on purpose")
                break
        conn.close()


if __name__ == "__main__":
    main()
