# Troubleshooting

These notes come from real problems that came up on the bike while the project was being developed.

## Quick table

| Symptom | Cause / fix |
|---|---|
| Serial Monitor shows `NO JK … got 0 bytes` | The JK is not answering: swap the D4 ↔ D5 wires, check GND, try `JK_FRAME_SOURCE` `0x02`, wake the JK up (button or Bluetooth) |
| `NO JK` but the byte count is > 0 | The JK answers but the frame is incomplete/corrupt. Save the line; try raising `JK_RX_WINDOW_MS` to 70–80 (never above 91) |
| `ECU answers` count does not go up, LED blinks slowly | ECU polls are not received: A/B swapped, RO → D8 wiring, common GND |
| Sniffer shows junk lines starting with `1D D4 …` | [A and B swapped](#a-and-b-swapped) |
| Serial Monitor shows `NWh(` / `NWh)` | Only with the JK on D0/D1: that is the Nano's own request to the JK (the TX also reaches USB), not the JK's reply |
| Err 94 on the dashboard | Battery answers stopped for ≥3–4 s: MAX485 wiring, A/B, Nano power |
| Err 92 / 93 / 90 on the dashboard | The JK really has an alarm (check the app), or set `REPORT_ERRORS 0` |
| Code won't upload | If anything is connected to D0 the upload fails and the Nano keeps running the **old code**. D0 is free in this project. If needed, choose "ATmega328P (Old Bootloader)" |

## A and B swapped

On RS485, swapping A/B inverts the signal. The Nano still receives bytes, but they are meaningless. The byte counts
stay right, the values are wrong. Example seen on the bike:

| Seen in the sniffer | Actually |
|---|---|
| `1D D4 6A A5 D1 9A 1E` (7 bytes) | ECU → battery poll `C5 5C 5A AA 01 97 96 0D` |
| `1D D4 64 65 …` (29 bytes) | Dashboard poll (21) + its answer (8) |
| `1D D4 62 65 FF FF FB E5 …` (26 bytes) | Controller poll (9) + its answer (17) |

Fix: swap the A and B wires on the MAX485. No code change needed.

## JK never answers on D0

In the first attempts the JK was wired to the Nano's hardware serial port (D0/D1) and no data arrived at all. The
Nano's TX worked (the request went out), but not a single byte could be read on D0. Sending the same request to the
same JK from a PC through a USB-TTL adapter got an immediate reply.

**Cause:** On the Nano board, the USB chip's (CH340) TX pin is connected to D0 through a 1 kΩ resistor and keeps
pulling the line up to 5 V. The JK's 3.3 V TX output is most likely weak (series resistor), so it cannot overcome
this pull and the ATmega328P never sees a "0" bit. The USB-TTL adapter has no such pull on its input, which is why
it works there. On most Nano clones the USB chip is powered from the board's 5 V, so this happens even without a
USB cable plugged in.

**Fix:** Connect the JK to a pin other than D0. In this project the JK is on D4/D5.

For the same reason the MAX485 was moved from D0/D1 to D8/D9 as well. While the D0/D1 attempts failed, the same
MAX485 worked on D8/D9 (AltSoftSerial) on the first try.

## Why a custom UART on D4/D5?

- **AltSoftSerial** (D8/D9) receives 115200 reliably, but only one instance can exist and the MAX485 is already on it.
- **SoftwareSerial** is fine at 9600, but at 115200 it drops bytes in the JK's ~300-byte continuous reply.
- Solution: the JK is read in the ~91 ms window in which the ECU is silent. During that window interrupts are
  disabled and D4/D5 are bit-banged using Timer1's 16 MHz counter (`jkSendByte`, `jkReceive`). One bit is 139
  clocks; each bit is sampled in its middle. In simulation it read correctly even with the JK's clock off by ±2 %.

## Measuring the JK socket

- Put the black probe on **the socket's own GND pin**. Correct result: the pin at one end is the pack voltage (VBAT), the two middle pins are ~3.3 V.
- If the black probe touches the battery negative or P-, you can get misleading results such as "battery voltage on every pin".
- If you see tens of volts on the middle pins, do not connect the Nano.

## The JK's RS485

This type of JK has no separate RS485 port; the 4-pin socket is a TTL UART. Real RS485 needs JK's RS485 adapter
board. With the adapter it would also be possible to put the JK on the scooter's RS485 bus (ECU at 9600, JK at
115200; the Nano would switch baud rates during the ECU's quiet window), but this project uses the UART route.

## Diagnosing with the sniffer

`tools/SocoTC_Sniffer` uses the same wiring as the main project (D8/D9/D2). Serial Monitor (115200):

```
11570 ms  +90.8 ms   ECU->Battery    poll  C5 5C 5A AA 01 97 96 0D
11579 ms  +2.7 ms    OURS->ECU  batt resp  B6 6B AA 5A 0A 48 50 19 … -> 72V 80% 25C
=== SUMMARY  cycle=358.x/… ms | batt.ans->gap=90.8/… ms | …
```

- `cycle`: one full ECU cycle (≈358 ms)
- `batt.ans->gap`: how long the bus stays idle after the battery answer (the JK read window, ≈91 ms)
- `batt.no-answer wait`: how long the ECU waits when nobody answers
- If another battery/emulator on the bus is answering, set `ANSWER_BATTERY 0` in the sniffer.
