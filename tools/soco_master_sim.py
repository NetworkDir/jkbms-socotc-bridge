#!/usr/bin/env python3
"""
Super Soco ECU taklidi: USB-RS485 adaptor uzerinden bataryaya sorgu gonderir,
ceviricinin cevabini cozup ekrana yazar. Motosiklet olmadan masada test icin.

Kurulum : pip install pyserial
Kullanim: python soco_master_sim.py COM5          (Windows)
          python3 soco_master_sim.py /dev/ttyUSB0  (Linux)
"""
import sys
import time

import serial

QUERY = bytes.fromhex("C5 5C 5A AA 01 97 96 0D")
ERRORS = {0x01: "Err92 sarj kesildi", 0x02: "Err93 sarj akimi yuksek", 0x04: "Err90 desarj akimi yuksek"}


def s8(b):
    return b - 256 if b > 127 else b


def decode(frame):
    if len(frame) != 17 or frame[:4] != b"\xB6\x6B\xAA\x5A" or frame[4] != 10 or frame[16] != 0x0D:
        return "gecersiz cerceve: " + frame.hex(" ").upper()
    p = frame[5:15]
    chk = 10
    for b in p:
        chk ^= b
    if chk != frame[15]:
        return "checksum hatasi: " + frame.hex(" ").upper()
    errs = [txt for bit, txt in ERRORS.items() if p[8] & bit]
    return (f"{p[0]} V  {p[1]} %  {s8(p[2])} C  {s8(p[3])} A  "
            f"dongu {p[4] << 8 | p[5]}/{p[6] << 8 | p[7]}  "
            f"hata 0x{p[8]:02X} {' '.join(errs)}  sarj {p[9]}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = serial.Serial(sys.argv[1], 9600, timeout=0.2)
    print("Sorgu gonderiliyor, cikmak icin Ctrl+C")
    while True:
        port.reset_input_buffer()
        port.write(QUERY)
        data = port.read(40)              # bazi adaptorler kendi sorgusunu da geri okur
        i = data.find(b"\xB6\x6B")
        if i < 0:
            print("cevap yok" if not data else "taninmayan veri: " + data.hex(" ").upper())
        else:
            print(decode(data[i:i + 17]))
        time.sleep(0.5)


if __name__ == "__main__":
    main()
