# Super Soco TC RS485 protocol

Sources: [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater),
[stprograms/SuperSoco485Monitor](https://github.com/stprograms/SuperSoco485Monitor) and measurements taken on the
bike with `tools/SocoTC_Sniffer`. The protocol was reverse engineered; the meaning of some fields is not certain.

## Physical layer

- RS485, two-wire (A/B), **9600 baud, 8N1**
- Master/slave: the ECU polls, the other devices answer. Without an ECU the dashboard takes over the master role.

## Telegram structure

| Byte | 0 | 1 | 2 | 3 | 4 | 5 … 4+n | 5+n | 6+n |
|---|---|---|---|---|---|---|---|---|
| | T0 | T1 | Destination | Source | Length (n) | Data | Checksum | `0D` |

- `C5 5C` = request (from the master), `B6 6B` = response
- **Checksum** = `n XOR data[0] XOR … XOR data[n-1]`
- `0D` is the end byte, but it can also show up inside the data (e.g. 13 °C). The parser has to work from the length byte.

## Addresses

| Address | Device |
|---|---|
| `AA` | ECU (master) |
| `BA` | Dashboard |
| `DA` | Motor controller |
| `5A` | Battery (BMS) |

## Telegrams

| Telegram | Bytes 2–3 | Data length | Example (captured on the bike) |
|---|---|---|---|
| ECU → Dashboard | `BA AA` | 14 | `C5 5C BA AA 0E 50 00 00 00 08 00 00 00 01 01 00 00 00 70 26 0D` |
| Dashboard → ECU | `AA BA` | 1 | `B6 6B AA BA 01 00 01 0D` |
| ECU → Controller | `DA AA` | 2 | `C5 5C DA AA 02 00 00 02 0D` |
| Controller → ECU | `AA DA` | 10 | `B6 6B AA DA 0A 01 00 06 00 00 13 00 00 02 01 1D 0D` |
| ECU → Battery | `5A AA` | 1 | `C5 5C 5A AA 01 97 96 0D` |
| Battery → ECU | `AA 5A` | 10 | `B6 6B AA 5A 0A 42 4D 1B 00 00 05 00 03 00 00 18 0D` (original BMS) |

## Battery answer (data, 10 bytes)

| Byte | Field | Type | Description |
|---|---|---|---|
| 0 | Voltage | uint8 | Volts |
| 1 | SOC | uint8 | % |
| 2 | Temperature | int8 | °C |
| 3 | Current | int8 | A, negative = discharge |
| 4–5 | Charge cycles | uint16 BE | |
| 6–7 | Discharge cycles | uint16 BE | |
| 8 | Error | bit field | see below |
| 9 | Status | uint8 | 1 = charging, 0 = idle (4 = discharging, never seen from the original BMS) |

Original BMS example: `42 4D 1B 00 00 05 00 03 00 00` → 66 V, 77 %, 27 °C, 0 A, 5 charge / 3 discharge cycles.

### Error byte (byte 8) → dashboard error codes

| Bit | Dashboard code | Meaning |
|---|---|---|
| 0 | Err 92 | BMS stopped charging |
| 1 | Err 93 | Charge current too high |
| 2 | Err 90 | Discharge current too high |
| 5 | Err 89 | (unknown) |
| 7 | Err 88 | (unknown) |

Also: temperature ≥ 60 °C → Err 91, battery not answering → **Err 94**.

## Dashboard poll (ECU → Dashboard, 14 data bytes)

| Byte | Field |
|---|---|
| 0 | SOC (the last value the ECU got from the battery) |
| 1 | Controller current |
| 2 | Speed |
| 3 | Controller temperature level |
| 4–5 | Hour / minute |
| 6–7 | Error codes |
| 8 | Vehicle state (4 = charging, 1 = parked) |
| 9 | Gear |
| 10–11 | Raw speed from the controller |
| 13 | Remaining range (km) |

When the emulator sent 80 %, byte 0 of the ECU's dashboard poll was `50` (80). So the ECU passes the SOC it gets
from the battery straight on to the dashboard.

## Timing measured on the bike

Measured with `tools/SocoTC_Sniffer` (while the emulator answered the battery poll after a 1 ms delay):

```
t (ms)   gap       telegram
11570    +90.8     ECU→Battery     poll    C5 5C 5A AA 01 97 96 0D
11579    + 2.7     US→ECU          answer  B6 6B AA 5A 0A 48 50 19 … 0B 0D
11688    +90.9     ECU→Dashboard   poll    C5 5C BA AA 0E …
11720    + 9.4     Dashboard→ECU   answer  B6 6B AA BA 01 00 01 0D
11808    +79.9     ECU→Controller  poll    C5 5C DA AA 02 00 00 02 0D
11819    + 0.7     Controller→ECU  answer  B6 6B AA DA 0A …
11928    +90.8     ECU→Battery     poll    …
```

- The ECU sends a poll every **120 ms**, in the order dashboard → controller → battery.
- A full cycle takes ≈ **358 ms**; the battery poll repeats at that interval.
- After the battery answer the bus is idle for **~91 ms**. The JK is read in this window.
- When the battery answers stop, **Err 94** appears after about **3–4 s**; an occasional missed poll is harmless.
- If nobody answers, the ECU still moves on to the next device after 120 ms.
