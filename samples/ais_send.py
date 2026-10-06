#!/usr/bin/env python3
"""Send synthetic AIS !AIVDM sentences over UDP for ais_maneuver228 testing.

No third-party packages required. Generates type 1/2/3 position reports and
type 5 static/voyage reports (single or two-fragment) with valid XOR and
six-bit armored payloads.

Examples:
  ./samples/ais_send.py --mmsi 477000001 --lat 22.123456 --lon 114.012345 \
      --sog 12.3 --cog 45
  ./samples/ais_send.py --mmsi 477000001 --name "HARBOR STAR"
  ./samples/ais_send.py --scenario closing
"""
import argparse
import math
import socket
import sys
import time


def checksum(body: str) -> str:
    x = 0
    for ch in body:
        x ^= ord(ch)
    return f"!{body}*{x:02X}"


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value: int, length: int) -> None:
        for i in range(length - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def text(self, value: str, chars: int) -> None:
        for k in range(chars):
            v = 0
            if k < len(value):
                ch = ord(value[k])
                if 65 <= ch <= 90:
                    v = ch - 64
                elif 48 <= ch <= 57:
                    v = ch - 48 + 27
                elif ch == 32:
                    v = 32
            self.put(v, 6)


def armor(bits):
    fill = (6 - len(bits) % 6) % 6
    bits = bits + [0] * fill
    out = []
    for i in range(0, len(bits), 6):
        v = 0
        for b in bits[i:i + 6]:
            v = (v << 1) | b
        c = v + 48
        if c > 87:
            c += 8
        out.append(chr(c))
    return "".join(out), fill


def encode_position(mmsi, msg_type, lat, lon, sog, cog):
    w = BitWriter()
    w.put(msg_type, 6)
    w.put(0, 2)           # repeat
    w.put(mmsi, 30)
    w.put(0, 4)           # navigation status
    w.put(-128 & 0xFF, 8) if False else w.put(0, 8)  # ROT not available? use 0
    w.put(int(round(sog * 10)), 10)
    w.put(1, 1)           # position accuracy
    w.put(int(round(lon * 600000)) & ((1 << 28) - 1), 28)
    w.put(int(round(lat * 600000)) & ((1 << 27) - 1), 27)
    w.put(int(round(cog * 10)), 12)
    w.put(int(round(cog)) % 360, 9)
    w.put(0, 6)           # UTC second
    w.put(0, 3)           # maneuver
    w.put(0, 3)           # spare
    w.put(0, 1)           # RAIM
    w.put(0, 19)          # radio
    payload, fill = armor(w.bits)
    return [checksum(f"AIVDM,1,1,,A,{payload},{fill}")]


def encode_static(mmsi, name, sequence=3):
    w = BitWriter()
    w.put(5, 6)
    w.put(0, 2)
    w.put(mmsi, 30)
    w.put(0, 2)           # AIS version
    w.put(0, 30)          # IMO
    w.text("", 7)         # call sign
    w.text(name.upper()[:20], 20)
    w.put(70, 8)          # ship type: cargo
    w.put(10, 9)
    w.put(90, 9)
    w.put(10, 6)
    w.put(10, 6)
    w.put(1, 4)           # EPFD GPS
    w.put(0, 20)          # ETA
    w.put(50, 8)          # draught in dm
    w.text("", 20)        # destination
    w.put(0, 1)
    w.put(0, 1)
    payload, fill = armor(w.bits)
    # Split into two fragments at a 6-char boundary
    cut = (len(payload) // 2 // 6 * 6) or 6
    p1, p2 = payload[:cut], payload[cut:]
    return [
        checksum(f"AIVDM,2,1,{sequence},A,{p1},0"),
        checksum(f"AIVDM,2,2,{sequence},A,{p2},{fill}"),
    ]


def send(lines, host, port, delay):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        for line in lines:
            s.sendto((line + "\r\n").encode(), (host, port))
            print("sent:", line)
            time.sleep(delay)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--udp", default="127.0.0.1:10110")
    ap.add_argument("--mmsi", type=int, default=477000001)
    ap.add_argument("--type", type=int, default=1, choices=(1, 2, 3))
    ap.add_argument("--lat", type=float, default=22.123456)
    ap.add_argument("--lon", type=float, default=114.012345)
    ap.add_argument("--sog", type=float, default=12.3, help="knots")
    ap.add_argument("--cog", type=float, default=45.0, help="degrees")
    ap.add_argument("--name", default="")
    ap.add_argument("--scenario", choices=("closing", "parallel", ""), default="")
    ap.add_argument("--delay", type=float, default=0.2)
    args = ap.parse_args()

    host, port = args.udp.rsplit(":", 1)
    lines = []
    if args.name:
        lines += encode_static(args.mmsi, args.name)

    if args.scenario == "closing":
        # Own ship heading south, target 1 nm south heading north at 20 kn.
        lines += encode_position(477000001, 1, args.lat + 0.009,
                                 args.lon, 20.0, 180.0)
        lines += encode_static(477000002, "TARGET ALFA", 1)
        lines += encode_position(477000002, 3, args.lat - 0.009,
                                 args.lon, 20.0, 0.0)
    elif args.scenario == "parallel":
        lines += encode_position(477000001, 1, args.lat, args.lon,
                                 12.0, 90.0)
        lines += encode_position(477000002, 1, args.lat + 0.002,
                                 args.lon + 0.002, 12.0, 90.0)
    else:
        lines += encode_position(args.mmsi, args.type, args.lat, args.lon,
                                 args.sog, args.cog)
    send(lines, host, int(port), args.delay)


if __name__ == "__main__":
    main()
