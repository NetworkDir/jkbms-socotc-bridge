# Wiring

## Overview

```
  JK BMS                         Arduino Nano                        MAX485               Super Soco
 (4-pin UART)                                                                              RS485 bus
 ┌─────────┐                ┌──────────────────────┐               ┌──────────┐
 │ GND     │────────────────│ GND              GND │───────────────│ GND      │
 │ RX      │◀──┬──[1k]──────│ D5               5V  │───────────────│ VCC      │
 │         │  [2k]          │                      │               │          │
 │         │   └─GND        │                  D8  │◀──────────────│ RO       │
 │ TX      │───────────────▶│ D4               D9  │──────────────▶│ DI       │
 │ VBAT    │  (NC!)         │                  D2  │──────┬───────▶│ DE       │
 └─────────┘                │                  D13 │ LED  └───────▶│ RE       │
                            │  USB (D0/D1 free)    │               │ A        │──── A
                            └──────────────────────┘               │ B        │──── B
                                                                   └──────────┘
```

The `[1k]` / `[2k]` divider is optional. It makes the JK's RX input see ~3.3 V instead of 5 V.
It also worked without the divider in testing, but it is recommended for long-term use.

## Pin table

| Nano pin | Connection | Direction | Note |
|---|---|---|---|
| D2 | MAX485 DE + RE | output | High = transmit, low = listen |
| D4 | JK TX | input | Internal pull-up enabled |
| D5 | JK RX | output | High when idle |
| D8 | MAX485 RO | input | AltSoftSerial RX (fixed pin) |
| D9 | MAX485 DI | output | AltSoftSerial TX (fixed pin) |
| D13 | On-board LED | output | Status indicator |
| D0 / D1 | — | — | **Not used** (USB). Do not connect the JK here, see [TROUBLESHOOTING](TROUBLESHOOTING.md#jk-never-answers-on-d0) |
| D10 | — | — | Cannot be used for PWM because of AltSoftSerial |

## JK BMS 4-pin UART socket

```
  ┌──────────────────────┐
  │  GND   RX   TX  VBAT │      JST 1.25 mm, 4-pin
  └──────────────────────┘
     │     │    │    └── pack voltage (e.g. 84 V). LEAVE IT UNCONNECTED!
     │     │    └─────── JK output  → Nano D4
     │     └──────────── JK input   ← Nano D5
     └────────────────── GND        → Nano GND
```

- Some JK models label this socket "RS485" or "GPS", but it is a **3.3 V TTL UART** inside. Real RS485 needs JK's separate RS485 adapter board.
- **When measuring the pins**, put the black probe on the socket's own GND pin. Correct readings: VBAT = pack voltage, RX/TX ≈ 3.3 V. If the black probe touches the battery negative or P-, you can get misleading readings.
- The 3-pin socket (if present) is for CAN or the heater, not RS485/UART. The 6-pin socket is for the display and the button.

## MAX485 module

| MAX485 | Connection |
|---|---|
| VCC | Nano 5V |
| GND | Nano GND |
| RO | Nano D8 |
| DI | Nano D9 |
| DE + RE | Bridged together → Nano D2 |
| A / B | Super Soco RS485 A / B |

- **A/B polarity:** Swapping them does no harm; there is simply no communication. If the sniffer output shows "junk" lines starting with `1D D4 …`, A and B are swapped.
- **120 Ω termination:** Most modules have it as R7. If you run into communication problems, try removing it.

## Super Soco side

The Super Soco's internal RS485 bus has 4 devices: ECU (`AA`), dashboard (`BA`), motor controller (`DA`) and battery (`5A`).
Use the A/B lines on the original battery's connector. On the TC Max, the 4-pin JST SM connector under the seat is on
the same bus (pin 2 = B, pin 3 = A, pin 4 = GND; source: SuperSoco485Monitor).

## Power

- Power the Nano from the scooter's **switched 12 V** line: step it down to 5 V with a buck converter into the 5V pin, or feed 7–12 V into VIN.
- On the bench, USB power is enough.
- **Do not** use VBAT on the JK socket to power the Nano (it is the pack voltage).

## Ground and isolation

The JK BMS is a common-port BMS with its discharge MOSFETs on the negative side:

```
 Cells (−) ── B- ──[ JK MOSFETs ]── P- ── Scooter GND (ECU, RS485)
               │
        JK UART GND
```

- The JK's UART GND is on the **B-** side, while the scooter's RS485 reference is on the **P-** side.
- The Nano connects the JK GND to the scooter GND. In normal operation the only difference between B- and P- is the small voltage drop across the MOSFETs.
- **When the JK opens its discharge MOSFETs for protection**, this connection can bypass the MOSFETs through the Nano's GND line. The protection may then not fully work, current may flow through the Nano's GND line, or the RS485 chips may get damaged.

For a safer setup, isolate the JK UART link:

```
 Nano 5V ──▶ ADuM1201 VDD1          B0503S (5V → 3.3V isolated) ──▶ ADuM1201 VDD2
 Nano GND ─▶ ADuM1201 GND1          B0503S 0V output ───────────▶ ADuM1201 GND2 ──▶ JK GND
 Nano D5 ──▶ ADuM side-1 input  ══▶  ADuM side-2 output ──▶ JK RX
 Nano D4 ◀── ADuM side-1 output ◀══  ADuM side-2 input  ◀── JK TX
```

Each side of the ADuM1201 has one input and one output channel; check the input→output direction on the module's label.
Since the JK side of the isolator runs from 3.3 V, no extra level shifting is needed.
