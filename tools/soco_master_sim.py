#!/usr/bin/env python3
"""
Super Soco ECU emulator: sends battery polls through a USB-RS485 adapter and
decodes the bridge's answer. For bench testing without the motorcycle.

Install: pip install pyserial
Usage:   python soco_master_sim.py COM5          (Windows)
         python3 soco_master_sim.py /dev/ttyUSB0  (Linux)
"""
import sys
import time

import serial

QUERY = bytes.fromhex("C5 5C 5A AA 01 97 96 0D")
ERRORS = {0x01: "Err92 charging stopped", 0x02: "Err93 charge current high", 0x04: "Err90 discharge current high"}


def s8(b):
    return b - 256 if b > 127 else b


def decode(frame):
    if len(frame) != 17 or frame[:4] != b"\xB6\x6B\xAA\x5A" or frame[4] != 10 or frame[16] != 0x0D:
        return "invalid frame: " + frame.hex(" ").upper()
    p = frame[5:15]
    chk = 10
    for b in p:
        chk ^= b
    if chk != frame[15]:
        return "checksum error: " + frame.hex(" ").upper()
    errs = [txt for bit, txt in ERRORS.items() if p[8] & bit]
    return (f"{p[0]} V  {p[1]} %  {s8(p[2])} C  {s8(p[3])} A  "
            f"cycles {p[4] << 8 | p[5]}/{p[6] << 8 | p[7]}  "
            f"error 0x{p[8]:02X} {' '.join(errs)}  charging {p[9]}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = serial.Serial(sys.argv[1], 9600, timeout=0.2)
    print("Sending polls, press Ctrl+C to quit")
    while True:
        port.reset_input_buffer()
        port.write(QUERY)
        data = port.read(40)              # some adapters echo their own query back
        i = data.find(b"\xB6\x6B")
        if i < 0:
            print("no answer" if not data else "unrecognized data: " + data.hex(" ").upper())
        else:
            print(decode(data[i:i + 17]))
        time.sleep(0.5)


if __name__ == "__main__":
    main()
