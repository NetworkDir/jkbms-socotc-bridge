# JK BMS → Super Soco TC bridge (Arduino Nano)

[![build](https://github.com/NetworkDir/jkbms-socotc-bridge/actions/workflows/build.yml/badge.svg)](https://github.com/NetworkDir/jkbms-socotc-bridge/actions/workflows/build.yml)

An Arduino Nano project that lets a **Super Soco TC** run on a battery pack with a **JK BMS** instead of
the original battery, while the bike's ECU still believes the original battery is installed.
The Nano reads the JK BMS over its **UART** port and answers the battery polls on the bike's **RS485** bus
in the original Soco BMS format. If no JK data is available it reports **83 V / 100 %**,
so the bike always runs.

Built on the protocol work of [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater).
Protocol notes are in [`docs/`](docs/).

---

## Contents

- [Features](#features)
- [Parts](#parts)
- [Wiring](#wiring)
- [Setup](#setup)
- [How it works](#how-it-works)
- [Data mapping](#data-mapping)
- [Settings](#settings-configh)
- [Serial Monitor and LED](#serial-monitor-and-led)
- [Tools](#tools)
- [Repository layout](#repository-layout)
- [Known limitations](#known-limitations)
- [References](#references)
- [License](#license)

Detailed documentation:

| Document | Contents |
|---|---|
| [docs/WIRING.md](docs/WIRING.md) | Wiring diagrams, pins, safety notes |
| [docs/PROTOCOL_SOCOTC.md](docs/PROTOCOL_SOCOTC.md) | Super Soco RS485 protocol and timing measured on the bike |
| [docs/PROTOCOL_JK_UART.md](docs/PROTOCOL_JK_UART.md) | JK BMS UART (`4E 57`) protocol |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Troubleshooting and lessons learned during development |

---

## Features

- Answers the ECU's battery poll with the JK's **real** values: voltage, SOC, temperature, current, cycle count, alarms.
- If there is no JK data, or it stops for 10 s, it sends **83 V / 100 %**. The bike never drops into the "no battery" error (Err 94).
- **D0/D1 are not used.** USB stays free, so you can watch the status in the Serial Monitor and upload new code without unplugging anything.
- The JK is read during the ~91 ms window in which the ECU is silent, so the two links never get in each other's way.
- The JK UART (115200) is bit-banged without a library, with interrupts disabled, timed by the Timer1 counter. In simulation it read correctly even with the JK's clock off by ±2 %.
- Length-based Soco parser: a `0x0D` inside the data (e.g. 13 °C) does not break it.

## Parts

| Part | Note |
|---|---|
| Arduino Nano (ATmega328P, 5 V) | Clones work too (CH340) |
| MAX485 TTL ↔ RS485 module | DE and RE pins bridged |
| JK BMS | Classic JK-B / JK-BD series with a 4-pin UART socket (tested: HW 11.XW, SW 11.272, 20S) |
| 4-pin JST 1.25 mm cable | For the JK UART socket |
| (Optional) 1 kΩ + 2 kΩ resistors | To bring Nano D5 → JK RX down to 3.3 V |

## Wiring

```
                 ┌────────────── Arduino Nano ──────────────┐
 MAX485 RO  ───▶ │ D8                                    D4 │ ◀─── JK TX
 MAX485 DI  ◀─── │ D9                                    D5 │ ───▶ JK RX
 MAX485 DE+RE ◀─ │ D2                                   GND │ ──── JK GND
 MAX485 VCC ◀─── │ 5V                                       │      JK VBAT → not connected!
 MAX485 GND ──── │ GND                                      │
                 └──────────────────────────────────────────┘
 MAX485 A / B ──── Super Soco RS485 A / B
```

| Nano | Connection |
|---|---|
| D8 | MAX485 **RO** |
| D9 | MAX485 **DI** |
| D2 | MAX485 **DE + RE** (both pins bridged) |
| D4 | JK **TX** (the pin next to VBAT on the 4-pin socket) |
| D5 | JK **RX** (the pin next to GND). Optional: 1 kΩ in series + 2 kΩ from JK RX to GND |
| GND | MAX485 GND + JK GND |
| 5V | MAX485 VCC |

**JK 4-pin UART socket:** `GND – RX – TX – VBAT`. VBAT carries the **pack voltage** and must not be connected to anything.
Some JKs label this socket "RS485" or "GPS", but it is a 3.3 V TTL UART inside.

**RS485 A/B:** Swapping them does not damage anything; there is simply no communication. The sniffer tool makes the
correct polarity easy to find (see [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md#a-and-b-swapped)).

> ⚠️ **Ground note:** The JK's UART GND is on the BMS **B-** side, while the scooter's RS485 bus is referenced to **P-**.
> The Nano ties the two together. When the JK opens its discharge MOSFETs for protection, this connection can bypass them.
> Details and an isolation option (ADuM1201 + B0503S): [docs/WIRING.md](docs/WIRING.md#ground-and-isolation).

Full diagrams and power notes: [docs/WIRING.md](docs/WIRING.md)

## Setup

1. In the Arduino IDE open **Tools → Manage Libraries** and install **AltSoftSerial** (Paul Stoffregen).
2. Open `JK_Soco_Final/JK_Soco_Final.ino`. Keep all the `.h` files next to it.
3. Board: **Arduino Nano**, processor: **ATmega328P**. If the upload fails, choose **ATmega328P (Old Bootloader)**.
4. Upload. D0/D1 are free, so there is no need to unplug anything.
5. Open the Serial Monitor at **115200** baud and switch the ignition on.

## How it works

```
ECU polling pattern (measured on the bike: one poll every 120 ms, full cycle ~358 ms):

  ECU→Dashboard  ECU→Controller  ECU→Battery ─ WE answer ─┬─ [ ~91 ms silence ] ─ ECU→Dashboard ...
                                                          └─ JK is read here (≤ 60 ms)
```

1. The Nano listens to the RS485 bus at 9600 baud (D8/D9, AltSoftSerial).
2. When the ECU's battery poll (`C5 5C 5A AA 01 97 96 0D`) arrives, it sends the 17-byte answer 1 ms later.
3. Right after the first answer, and then after every 3rd answer (~1 s), while the ECU is silent it:
   - disables interrupts,
   - sends the "read all data" request to the JK on D5 (`4E 57 00 13 … 06 03 … 68 00 00 01 29`),
   - reads the JK's ~300-byte reply bit by bit on D4 (60 ms max),
   - re-enables interrupts, verifies the reply's checksum and decodes it.
4. The following answers use the JK's values. If there is no JK data, or it is older than 10 s, 83 V / 100 % is sent.

The ECU only raises Err 94 after **3–4 seconds** without battery answers, so an occasional missed poll does no harm.

## Data mapping

| Soco byte | Meaning | JK source | Without JK |
|---|---|---|---|
| 0 | Voltage (V) | `0x83` total voltage, rounded to the nearest volt | 83 |
| 1 | SOC (%) | `0x85` | 100 |
| 2 | Temperature (°C, signed) | `0x81` / `0x82` (`TEMP_SOURCE`) | 25 |
| 3 | Current (A, signed, + = charging) | `0x84`, clamped to −128…127 | 0 |
| 4–5 | Charge cycles | `0x87` | 0 |
| 6–7 | Discharge cycles | `0x87` (the JK does not count them separately) | 0 |
| 8 | Error bits | bit0 ← charge/cell over-voltage (Err 92), bit1 ← charge over-current (Err 93), bit2 ← discharge over-current (Err 90) | 0 |
| 9 | Charging | 1 = current ≥ 1 A for 60 s | 0 |

## Settings (`config.h`)

| Setting | Default | Description |
|---|---|---|
| `JK_EVERY_N_CYCLES` | 3 | Read the JK every N battery answers (3 ≈ 1 s) |
| `JK_RX_WINDOW_MS` | 60 | How long to wait for the JK reply. Must stay below 91 ms |
| `JK_FRAME_SOURCE` | 0x03 | Source field of the request. 0x03 is tested; try 0x02 if it does not work |
| `JK_STALE_MS` | 10000 | Fall back to the defaults if no JK data arrives for this long |
| `DEFAULT_VOLTAGE_cV` | 8300 | Voltage without JK (0.01 V) |
| `DEFAULT_SOC` | 100 | SOC without JK (%) |
| `DEFAULT_TEMP_C` | 25 | Temperature without JK |
| `TEMP_SOURCE` | 0 | 0 = max(T1,T2), 1 = T1, 2 = T2, 3 = max(T1,T2,MOSFET) |
| `VOLTAGE_OFFSET_V` | 0 | Fixed offset added to the voltage |
| `REPORT_ERRORS` | 1 | Pass JK alarms through to the Soco error byte |
| `REPORT_CHARGING_FLAG` | 1 | Report "charging" on sustained charge current |
| `CHARGE_DETECT_cA` / `_MS` | 100 / 60000 | Counts as charging at 1.00 A for 60 s |
| `RESPONSE_DELAY_US` | 1000 | Delay between the ECU poll and the start of the answer |
| `DEBUG_SERIAL` | 1 | Print status lines to the Serial Monitor |

## Serial Monitor and LED

The Serial Monitor (115200) prints one line per JK read:

```
JK OK  82.80V  0A  97%  25C  20 cells | took 32 ms | ECU answers 4, jk ok/fail 2/0
NO JK (sending 83V 100%)  got 0 bytes | ECU answers 7, jk ok/fail 0/3
```

| LED (D13) | Meaning |
|---|---|
| On, briefly off on every answer | ECU and JK OK |
| Fast blink (5 Hz) | ECU OK, no JK data (sending 100 %) |
| Slow blink (1 Hz) | No polls from the ECU (check A/B) |

## Tools

| Tool | Purpose |
|---|---|
| [`tools/SocoTC_Sniffer`](tools/SocoTC_Sniffer/SocoTC_Sniffer.ino) | Listens to the RS485 bus and prints every telegram with a timestamp and the gap before it, plus a summary every 5 s. Can optionally answer the battery poll with 72 V / 80 %. Use it to find the A/B polarity and to measure timing. Same wiring as the main project (D8/D9/D2). |
| [`tools/SocoTC_Fixed`](tools/SocoTC_Fixed/SocoTC_Fixed.ino) | Simplest emulator, no JK: always 83 V / 100 %. Same wiring (D8/D9/D2). |
| [`tools/SocoTC_Voltage`](tools/SocoTC_Voltage/SocoTC_Voltage.ino) | Emulator without any BMS link: measures the pack voltage on **A0** through a 270k / 10k divider and sends that voltage plus an SOC estimated from it (Li-ion curve between `CELL_EMPTY_MV` and `CELL_FULL_MV`, `CELLS_S` = 20). Falls back to 83 V / 100 % if the reading is invalid. Same RS485 wiring (D8/D9/D2). |
| [`tools/soco_master_sim.py`](tools/soco_master_sim.py) | Emulates the ECU from a PC through a USB-RS485 adapter and decodes the answer (`pip install pyserial`). For bench testing without the bike. |
| [`tests/host_test.cpp`](tests/host_test.cpp) | PC tests for the protocol code: a real JK response, a byte-exact comparison with the original Soco BMS answer, the parser. `cd tests && g++ -std=c++11 -I../JK_Soco_Final host_test.cpp -o host_test && ./host_test` |

## Repository layout

```
├── JK_Soco_Final/          Main Arduino sketch
│   ├── JK_Soco_Final.ino   Main program (timing, JK UART driver, RS485)
│   ├── config.h            All settings
│   ├── jk_bms.h            JK UART protocol: request, receiver, decoder
│   ├── soco_tc.h           Super Soco RS485 protocol: parser, answer
│   └── convert.h           JK → Soco value conversion
├── tools/                  Sniffer, fixed-value and voltage-measuring emulators, PC test tool
├── tests/                  Protocol tests that run on a PC
└── docs/                   Protocol, wiring and troubleshooting docs
```

## Known limitations

- The Soco protocol was reverse engineered. The exact effect of bytes 6–7 (discharge cycles) and of the error bits on the ECU is not known for sure.
- The effect of the "charging" flag on the ECU is unknown. The 60 s requirement keeps regenerative braking from being counted as charging; set `REPORT_CHARGING_FLAG 0` if you don't want the flag at all.
- The code targets the `4E 57` UART protocol of the classic JK-B / JK-BD series. The JK-PB (inverter) series uses Modbus and is not supported.
- While the JK is being read (~35–60 ms) `millis()` stands still; nothing timer-based runs during that time.
- The JK's own protections keep working in every case; the bridge only passes information along.

## References

- [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater) — SocoTC battery answer, the original emulator idea
- [ldsrc.blogspot.com — Soco TC RS485 decode](https://ldsrc.blogspot.com/2023/12/soco-tc-rs485-decode.html)
- [stprograms/SuperSoco485Monitor](https://github.com/stprograms/SuperSoco485Monitor) — telegram structure, field meanings, error codes
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) — JK BMS UART protocol and sample frames
- [PaulStoffregen/AltSoftSerial](https://github.com/PaulStoffregen/AltSoftSerial)

## License

[MIT](LICENSE). You are working with a high-voltage battery pack and a motorcycle; use this at your own risk.
Read the ground note in [docs/WIRING.md](docs/WIRING.md) before wiring anything up.
